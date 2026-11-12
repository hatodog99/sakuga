#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in vec4 FragPosLightSpace;

// ----------------------------------------------------------------------------
// structs
// ----------------------------------------------------------------------------
struct Material {
    vec3 specular;
    float shininess;
};

struct DirLight {
    vec3 direction;     // direction the light travels
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    vec3 color;
};

struct ColorRamp {
    vec3 shadow;
    vec3 mid;
    vec3 light;
    int levels;         // number of flat bands (2-8)
    float softness;     // 0 = hard edge
};

// ----------------------------------------------------------------------------
// uniforms
// ----------------------------------------------------------------------------
uniform sampler2D texture_diffuse1;
uniform sampler2D shadowMap;

uniform mat4 view;
uniform vec3 viewPos;

uniform DirLight dirLight;
uniform Material material;
uniform ColorRamp ramp;

// toggles
uniform bool gLightingEnabled;
uniform bool gCellShadingEnabled;
uniform bool gRimLightingEnabled;
uniform bool gReceiveRimLighting;
uniform bool gShadowMappingEnabled;
uniform bool gReceiveShadowMapping;

// rim lighting
uniform vec2 rimDirection = vec2(-0.65, 1.0);   // screen space: x right, y up
uniform vec3 rimColor = vec3(1.0);
uniform float rimPower = 3.0;
uniform float rimIntensity = 0.5;

// pixels with alpha below this are discarded (fixes black borders on hair)
const float ALPHA_CUTOFF = 0.5;
const float GAMMA = 2.2;

// cast shadows darken to this fraction of the ramp's shadow color, so they stay
// visible even on surfaces already in the lowest toon band (e.g. the floor)
const float CAST_SHADOW_DARKEN = 0.6;

// DirLight::ambient default (shader_config.h); toon mode scales overall brightness by
// ambient / AMBIENT_REF, so the default ambient intensity of 1.0 leaves the look unchanged
const float AMBIENT_REF = 0.6;

// ----------------------------------------------------------------------------
// lighting
// ----------------------------------------------------------------------------
void CalcDirLight(DirLight light, vec3 normal, vec3 viewDir,
                  out vec3 ambient, out vec3 diffuse, out vec3 specular)
{
    vec3 lightDir = normalize(-light.direction);
    float diff = max(dot(normal, lightDir), 0.0);

    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
    spec *= step(0.0, diff);   // no highlight on surfaces facing away from the light

    ambient  = light.ambient * light.color;
    diffuse  = light.diffuse * diff * light.color;
    specular = light.specular * spec * material.specular * light.color;
}

// splits N.L into `levels` flat bands and picks the ramp color for each:
// first band = shadow, middle = midtone, last = highlight
vec3 RampColor(float ndl)
{
    int n = clamp(ramp.levels, 2, 8);
    float s = max(ramp.softness, 0.0001);

    float t = 0.0;
    for (int i = 1; i < n; ++i)
    {
        float edge = float(i) / float(n);
        t += smoothstep(edge - s, edge + s, ndl);
    }
    t /= float(n - 1);

    return (t < 0.5) ? mix(ramp.shadow, ramp.mid, t * 2.0)
                     : mix(ramp.mid, ramp.light, (t - 0.5) * 2.0);
}

// ----------------------------------------------------------------------------
// shadows
// ----------------------------------------------------------------------------
float ShadowCalc(vec4 fragPosLightSpace)
{
    if (!gReceiveShadowMapping)
        return 0.0;

    // perspective divide, then [-1,1] -> [0,1] for texture coords and depth
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    // beyond the light's far plane: not in shadow
    if (projCoords.z > 1.0)
        return 0.0;

    float currentDepth = projCoords.z;
    float bias = 0.0;   // only the character is in the depth map, so no self-shadowing on the floor

    // PCF: average a 3x3 block of samples
    float shadow = 0.0;
    vec2 texelSize = 3.0 / textureSize(shadowMap, 0);
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

// ----------------------------------------------------------------------------
// rim light
// ----------------------------------------------------------------------------
vec3 CalcRim(vec3 normal, vec3 viewDir)
{
    if (!gRimLightingEnabled || !gReceiveRimLighting)
        return vec3(0.0);

    float rimFactor = 1.0 - max(dot(normal, viewDir), 0.0);
    rimFactor = pow(rimFactor, max(rimPower, 0.0001));
    rimFactor = smoothstep(0.5, 0.55, rimFactor);   // hard toon edge

    // directional mask: only surfaces facing the chosen screen direction
    vec2 n2 = (mat3(view) * normal).xy;
    float nLen = length(n2);
    float dirMask = (nLen > 0.001) ? dot(n2 / nLen, normalize(rimDirection)) : 0.0;
    dirMask = smoothstep(0.3, 0.6, dirMask);

    return rimColor * rimFactor * dirMask * rimIntensity;
}

// ----------------------------------------------------------------------------
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
        vec3 lightDir = normalize(-dirLight.direction);

        vec3 ambient, diffuse, specular;
        CalcDirLight(dirLight, norm, viewDir, ambient, diffuse, specular);

        vec3 rim = CalcRim(norm, viewDir);
        float shadow = gShadowMappingEnabled ? ShadowCalc(FragPosLightSpace) : 0.0;

        if (gCellShadingEnabled)
        {
            // toon: ramp color by N.L, cast shadows blend to a darkened ramp shadow color
            float ndl = max(dot(norm, lightDir), 0.0);
            float ambientScale = dot(dirLight.ambient, vec3(1.0 / 3.0)) / AMBIENT_REF;
            vec3 toon = mix(RampColor(ndl), ramp.shadow * CAST_SHADOW_DARKEN, shadow)
                      * dirLight.color * ambientScale;
            result = toon * texColor + rim;
        }
        else
        {
            // shadow also dims the ambient term, otherwise it is invisible on surfaces
            // that get little diffuse light (e.g. the floor)
            vec3 shadowedAmbient = ambient * mix(1.0, CAST_SHADOW_DARKEN, shadow);
            result = (shadowedAmbient + (1.0 - shadow) * diffuse) * texColor
                   + (1.0 - shadow) * specular
                   + rim;
        }
    }
    else
    {
        result = texColor;
    }

    result = clamp(result, 0.0, 1.0);
    FragColor = vec4(pow(result, vec3(1.0 / GAMMA)), 1.0);   // linear -> sRGB
}
