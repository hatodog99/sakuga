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

uniform vec3 viewPos;
uniform DirLight dirLight;
uniform PointLight pointLights[NR_POINT_LIGHTS];
uniform SpotLight spotLight;
uniform Material material;
uniform sampler2D texture_diffuse1;

// lighting
uniform bool gLightingEnabled;

// toon shading attributes
uniform bool gCellShadingEnabled;
const float toon_color_levels = 3.0;
const float toon_scale_factor = 1.0 / toon_color_levels;
uniform float toon_bias = 0.26;

// rim lighting attributes
uniform bool gRimLightingEnabled;
uniform vec3 rimColor = vec3(1.0);
uniform float rimPower = 3.0;
uniform float rimIntensity = 0.5;

// pixels with alpha below this are discarded (fixes black borders on hair/feathers)
const float ALPHA_CUTOFF = 0.5;

float computeToonIntensity(float dotProduct);
void CalcDirLight(DirLight light, vec3 normal, vec3 viewDir,
                   out vec3 ambient, out vec3 diffuse, out vec3 specular);
void CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 texColor,
                   out vec3 ambient, out vec3 diffuse, out vec3 specular);
vec3 CalcSpotLight(SpotLight light, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 texColor);

void main()
{
    // sample once, discard transparent pixels, reuse the color below
    vec4 texSample = texture(texture_diffuse1, TexCoords);
    if (texSample.a < ALPHA_CUTOFF)
        discard;
    vec3 texColor = texSample.rgb;

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
            float toon_intensity = dot(totalDiffuse, vec3(0.299, 0.587, 0.114));
            toon_intensity = clamp(toon_intensity, 0.0, 1.0);

            float toon = ceil(pow(toon_intensity, toon_bias) * float(toon_color_levels)) * toon_scale_factor;

            totalDiffuse = (toon_intensity > 0.0) ? totalDiffuse * (toon / toon_intensity) : vec3(0.0);
            // totalSpecular = vec3(0.0);
        }
    
        vec3 rim = vec3(0.0);
        if (gRimLightingEnabled)
        {
            float rimFactor = 1.0 - max(dot(norm, viewDir), 0.0);
            rimFactor = pow(rimFactor, max(rimPower, 0.001));
            // rimFactor = smoothstep(0.5, 0.55, rimFactor);   // hard toon edge; remove for a soft glow
            rim = rimColor * rimFactor * rimIntensity;
        }

        vec3 result = (totalAmbient + totalDiffuse + totalSpecular + rim) * texColor;
        result = clamp(result, 0.0, 1.0);
        FragColor = vec4(result, 1.0);
    }
    else
    {
        FragColor = vec4(texColor, 1.0);
    }
}

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