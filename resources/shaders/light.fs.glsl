#version 330 core
layout (location = 0) out vec4 FragColor;   // lit scene (linear HDR)
layout (location = 1) out vec4 GlowColor;   // glow mask: only what should bloom

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
    float lowT;         // N.L of the first band edge (shadow -> next)
    float highT;        // N.L of the last band edge (-> highlight)
};

// ----------------------------------------------------------------------------
// uniforms
// ----------------------------------------------------------------------------
uniform sampler2D texture_diffuse1;
uniform sampler2D shadowMap;

// per-frame data, uploaded once per frame (see shader_config.h: FrameData / LightingData).
// the block layouts must match light.vs.glsl and outline.vs.glsl exactly.
layout (std140) uniform FrameBlock
{
    mat4 view;
    mat4 projection;
    mat4 lightSpaceMatrix;
    vec4 viewPosition;      // xyz
};

layout (std140) uniform LightingBlock
{
    vec4 dirDirection;      // xyz: direction the light travels
    vec4 dirAmbient;        // xyz, intensity already applied
    vec4 dirDiffuse;
    vec4 dirSpecular;
    vec4 dirColor;
    vec4 matSpecular;       // xyz: color, w: shininess
    vec4 rampShadow;        // xyz: color, w: rim glow strength    
    vec4 rampMid;
    vec4 rampLight;
    vec4 rampParams;        // x: softness, y: lowT, z: highT, w: levels
    vec4 rimColorIntensity; // xyz: color, w: intensity
    vec4 rimParams;         // x: power, yz: screen-space direction (x right, y up)
    ivec4 toggles;          // x: lighting, y: toon shading, z: rim lighting, w: shadow mapping
};

// per-draw flags (the floor and the character differ)
uniform bool gReceiveRimLighting;
uniform bool gReceiveShadowMapping;

// built from the blocks at the start of main()
DirLight dirLight;
Material material;
ColorRamp ramp;

// pixels with alpha below this are discarded (fixes black borders on hair)
const float ALPHA_CUTOFF = 0.5;

// cast shadows darken to this fraction of the ramp's shadow color, so they stay
// visible even on surfaces already in the lowest toon band (e.g. the floor)
const float CAST_SHADOW_DARKEN = 0.6;

// DirLight::ambient default (shader_config.h); toon mode scales overall brightness by
// ambient / AMBIENT_REF, so the default ambient intensity of 1.0 leaves the look unchanged
const float AMBIENT_REF = 0.6;

// ----------------------------------------------------------------------------
// lighting
// ----------------------------------------------------------------------------
void DirLightCalc(DirLight light, vec3 normal, vec3 viewDir,
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
// first band = shadow, middle = midtone, last = highlight.
// band edges are spread evenly from lowT to highT.
vec3 RampColor(float ndl)
{
    int n = clamp(ramp.levels, 2, 8);
    float s = max(ramp.softness, 0.0001);

    float t = 0.0;
    for (int i = 0; i < n - 1; ++i)
    {
        float k = (n > 2) ? float(i) / float(n - 2) : 0.5;
        float edge = mix(ramp.lowT, ramp.highT, k);
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
vec3 RimCalc(vec3 normal, vec3 viewDir)
{
    if (toggles.z == 0 || !gReceiveRimLighting)
        return vec3(0.0);

    float rimFactor = 1.0 - max(dot(normal, viewDir), 0.0);
    rimFactor = pow(rimFactor, max(rimParams.x, 0.0001));
    rimFactor = smoothstep(0.5, 0.55, rimFactor);   // hard toon edge

    // directional mask: only surfaces facing the chosen screen direction
    vec2 n2 = (mat3(view) * normal).xy;
    float nLen = length(n2);
    float dirMask = (nLen > 0.001) ? dot(n2 / nLen, normalize(rimParams.yz)) : 0.0;
    dirMask = smoothstep(0.3, 0.6, dirMask);

    return rimColorIntensity.rgb * rimFactor * dirMask * rimColorIntensity.a;
}

// ----------------------------------------------------------------------------
void main()
{
    dirLight.direction = dirDirection.xyz;
    dirLight.ambient   = dirAmbient.xyz;
    dirLight.diffuse   = dirDiffuse.xyz;
    dirLight.specular  = dirSpecular.xyz;
    dirLight.color     = dirColor.xyz;
    material.specular  = matSpecular.xyz;
    material.shininess = matSpecular.w;
    ramp.shadow   = rampShadow.xyz;
    ramp.mid      = rampMid.xyz;
    ramp.light    = rampLight.xyz;
    ramp.softness = rampParams.x;
    ramp.lowT     = rampParams.y;
    ramp.highT    = rampParams.z;
    ramp.levels   = int(rampParams.w + 0.5);

    bool gLightingEnabled = toggles.x != 0;
    bool gCellShadingEnabled = toggles.y != 0;
    bool gShadowMappingEnabled = toggles.w != 0;

    // sample once, discard transparent pixels, reuse the color below
    vec4 texSample = texture(texture_diffuse1, TexCoords);
    if (texSample.a < ALPHA_CUTOFF)
        discard;
    vec3 texColor = texSample.rgb;

    vec3 result;
    vec3 glow = vec3(0.0);

    if (gLightingEnabled)
    {
        vec3 norm = normalize(Normal);
        vec3 viewDir = normalize(viewPosition.xyz - FragPos);
        vec3 lightDir = normalize(-dirLight.direction);

        vec3 ambient, diffuse, specular;
        DirLightCalc(dirLight, norm, viewDir, ambient, diffuse, specular);

        vec3 rim = RimCalc(norm, viewDir);
        // glow mask = rim light + (optionally) the ambient light on the surface's albedo
        glow = rim * rampShadow.w + dirLight.ambient * dirLight.color * texColor * rimParams.w;
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

    // only clamps the bottom end; no negative values
    // gamma correction is now on bloom_composite.fs.glsl
    FragColor = vec4(max(result, 0.0), 1.0);
    GlowColor = vec4(glow, 1.0);
}