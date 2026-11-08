#version 330 core
out vec4 FragColor;

struct Material {
    vec3 specular;
    float shininess;
}; 

struct DirLight {
    vec3 direction;
	
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;

    vec3 color;
};

struct PointLight {
    vec3 position;
    
    float constant;
    float linear;
    float quadratic;
	
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;

    vec3 color;
};

struct SpotLight {
    vec3 position;
    vec3 direction;
    float cutOff;
    float outerCutOff;
  
    float constant;
    float linear;
    float quadratic;
  
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;       

    vec3 color;
};

#define NR_POINT_LIGHTS 1

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in vec4 FragPosLightSpace;

// shadow stuff
uniform sampler2D diffuseTexture;
uniform sampler2D shadowMap;

uniform vec3 lightPos;
uniform vec3 viewPos;

uniform DirLight dirLight;
uniform PointLight pointLights[NR_POINT_LIGHTS];
uniform SpotLight spotLight;
uniform Material material;
uniform sampler2D texture_diffuse1;

// attributes
// ----------------------------------------------------------------------------
// lighting
uniform bool gLightingEnabled;

// toon shading
uniform bool gCellShadingEnabled;
const float toon_color_levels = 3.0;
const float toon_scale_factor = 1.0 / toon_color_levels;
uniform float toon_bias = 0.26;

// rim lighting
uniform bool gRimLightingEnabled;
uniform bool gReceiveRimLighting;
uniform mat4 view;
uniform vec2 rimDirection = vec2(-0.65, 1.0);   // screen-space direction: x right, y up; top-left
uniform vec3 rimColor = vec3(1.0);
uniform float rimPower = 3.0;
uniform float rimIntensity = 0.5;

// shadows
uniform bool gShadowMappingEnabled;
uniform bool gReceiveShadowMapping;

// pixels with alpha below this are discarded (fixes black borders on hair/feathers)
const float ALPHA_CUTOFF = 0.5;

float gamma = 2.2f;

void CalcDirLight(DirLight light, vec3 normal, vec3 viewDir,
                   out vec3 ambient, out vec3 diffuse, out vec3 specular)
{
    vec3 lightDir = normalize(-light.direction);
    float diff = max(dot(normal, lightDir), 0.0);

    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
    spec *= step(0.0, diff);   // no highlight on surfaces facing away from the light

    ambient = light.ambient * light.color;
    diffuse = light.diffuse * diff * light.color;
    specular = light.specular * spec * material.specular * light.color;
}


void CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 texColor,
                     out vec3 ambient, out vec3 diffuse, out vec3 specular)
{
    vec3 lightDir = normalize(light.position - fragPos);
    float diff = max(dot(normal, lightDir), 0.0);

    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
    spec *= step(0.0, diff);   // no highlight on surfaces facing away from the light

    float distance = length(light.position - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * (distance * distance));    

    ambient = light.ambient * attenuation * light.color;
    diffuse = light.diffuse * diff * attenuation * light.color;
    specular = light.specular * spec * material.specular * attenuation * light.color;
}

vec3 CalcSpotLight(SpotLight light, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 texColor)
{
    vec3 lightDir = normalize(light.position - fragPos);
    float diff = max(dot(normal, lightDir), 0.0);

    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
    spec *= step(0.0, diff);   // no highlight on surfaces facing away from the light

    float distance = length(light.position - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * (distance * distance));    

    float theta = dot(lightDir, normalize(-light.direction)); 
    float epsilon = light.cutOff - light.outerCutOff;
    float intensity = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);

    vec3 ambient = light.ambient * texColor;
    vec3 diffuse = light.color * light.diffuse * diff * texColor;
    vec3 specular = light.specular * spec * material.specular;
    ambient *= attenuation * intensity;
    diffuse *= attenuation * intensity;
    specular *= attenuation * intensity;

    return (ambient + diffuse + specular);
}

float ShadowCalc(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir)
{
    if (!gReceiveShadowMapping) return 0.0;

    // calculate perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w; 
    projCoords = projCoords * 0.5 + 0.5;    // transform to [0,1] range for texture coords & depth values
    if (projCoords.z > 1.0)
        return 0.0;

    float closestDepth = texture(shadowMap, projCoords.xy).r;   // get closest depth value from light's POV
    float currentDepth = projCoords.z;

    // check whether current frag pos is in shadow
    // float bias = max(0.002 * (1.0 - dot(normal, lightDir)), 0.005);
    float bias = 0.0;

    // check whether current frag pos is in shadow
    // float shadow = currentDepth - bias > closestDepth  ? 1.0 : 0.0;
    // PCF
    float shadow = 0.0;
    vec2 texelSize = 3.0 / textureSize(shadowMap, 0);
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;
    return shadow;
    // return smoothstep(0.3, 0.7, shadow);
}

void main()
{
    // sample once, discard transparent pixels, reuse the color below
    vec4 texSample = texture(texture_diffuse1, TexCoords);
    if (texSample.a < ALPHA_CUTOFF)
        discard;
    vec3 texColor = texSample.rgb;

    vec3 result;

    if (gLightingEnabled)
    {
        vec3 norm = normalize(Normal);
        vec3 viewDir = normalize(viewPos - FragPos);
    
        vec3 totalAmbient = vec3(0.0);
        vec3 totalDiffuse = vec3(0.0);
        vec3 totalSpecular = vec3(0.0);

        vec3 a, d, s;

        // dir light
        CalcDirLight(dirLight, norm, viewDir, a, d, s);
        totalAmbient += a;
        totalDiffuse += d;
        totalSpecular += s;
    
        // point lights
        /*
        for(int i = 0; i < NR_POINT_LIGHTS; i++)
        {
            CalcPointLight(pointLights[i], norm, FragPos, viewDir, texColor, a, d, s);
            totalAmbient += a;
            totalDiffuse += d;
            totalSpecular += s;
        }
        */

        // spot light
        // result += CalcSpotLight(spotLight, norm, FragPos, viewDir, texColor);

        if (gCellShadingEnabled)
        {
            float r = 0.299;
            float g = 0.587;
            float b = 0.114;
            float toon_intensity = dot(totalDiffuse, vec3(r, g, b));
            toon_intensity = clamp(toon_intensity, 0.0, 1.0);

            float toon = ceil(pow(toon_intensity, toon_bias) * float(toon_color_levels)) * toon_scale_factor;

            totalDiffuse = (toon_intensity > 0.0) ? totalDiffuse * (toon / toon_intensity) : vec3(0.0);
            totalSpecular = vec3(0.0);  // get rid of specular hightlights
        }
    
        vec3 rim = vec3(0.0);
        if (gRimLightingEnabled)
        {
            float rimFactor = 1.0 - max(dot(norm, viewDir), 0.0);
            rimFactor = pow(rimFactor, max(rimPower, 0.0001));
            
            // smoothstep(float edge0, float edge1, genType x);
            rimFactor = smoothstep(0.5, 0.55, rimFactor);

            // directional mask: only surfaces facing the chosen screen direction
            vec3 viewNormal = normalize(mat3(view) * norm);
            float dirMask = dot(normalize(viewNormal.xy), normalize(rimDirection));
            dirMask = smoothstep(0.3, 0.6, dirMask);

            rim = gReceiveRimLighting ? rimColor * rimFactor * dirMask * rimIntensity : vec3(0.0);
        }
        
        vec3 lightDir = normalize(-dirLight.direction);
        float shadow = gShadowMappingEnabled ? ShadowCalc(FragPosLightSpace, norm, lightDir) : 0.0;
        result = (totalAmbient + (1.0 - shadow) * totalDiffuse) * texColor + (1.0 - shadow) * totalSpecular + rim;
    }
    else
    {
        result = texColor;
    }

    result = clamp(result, 0.0, 1.0);
    FragColor = vec4(pow(result, vec3(1.0/gamma)), 1.0); // linear -> sRGB
}