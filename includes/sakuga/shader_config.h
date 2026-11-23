#ifndef SHADER_CONFIG_H
#define SHADER_CONFIG_H

#include <glm/glm.hpp>
#include <sakuga/shader.h>

namespace colors
{
    inline glm::vec3 whiteLight(1.0f);                                // #FFFFFF
    inline glm::vec3 sunLight(1.0f, 0.988f, 0.924f);                  // #FFFCEB
    inline glm::vec3 ambientLight(0.706f, 0.843f, 1.0f);              // #B4D7FF

    inline glm::vec3 blackOutline(0.0f);                              // #000000
    inline glm::vec3 whiteOutline(1.0f);                              // #FFFFFF
    inline glm::vec3 darkBrownOutline(0.176f, 0.118f, 0.098f);        // #2D1E19
    inline glm::vec3 darkRedOutline(0.314f, 0.137f, 0.137f);          // #502323
    inline glm::vec3 darkBlueOutline(0.235f, 0.255f, 0.314f);         // #3C4150
}

struct Material
{
    glm::vec3 specular = glm::vec3(0.2f);
    float shininess = 8.0f;
};

struct DirLight
{
    glm::vec3 direction = glm::vec3(-0.504f, -0.2f, -1.8f);
    glm::vec3 ambient = glm::vec3(0.7f);
    glm::vec3 diffuse = glm::vec3(0.7f);
    glm::vec3 specular = glm::vec3(0.5f);
    float ambientIntensity = 1.0f;
    float diffuseIntensity = 1.0f;
    float specularIntensity = 1.0f;
};

struct ShadowMap
{
    bool shadowOnly = false;
    glm::vec3 lightDir = glm::vec3(-0.2f, -1.0f, -0.03f);
};

struct Outline
{
    float width = 0.001f;	// world units: outline.vs.glsl extrudes along the normal in world space
    float depthBias = 0.001f;
    glm::vec3 color = glm::vec3(0.0f);   // #000000
};

// toon color ramp: `levels` flat bands going shadow -> midtone -> highlight.
// lowT / highT are N.L values: the first band edge sits at lowT, the last at highT,
// and any edges in between are spread evenly. At 3 levels: lowT = shadow->mid, highT = mid->light.
struct ColorRamp
{
    glm::vec3 shadow = glm::vec3(0.55f);   // #8C8C8C
    glm::vec3 mid = glm::vec3(0.78f);      // #C7C7C7
    glm::vec3 light = glm::vec3(1.00f);    // #FFFFFF
    int levels = 3;            // 2-8 bands
    float softness = 0.02f;    // 0 = hard anime edge
    float lowT = 0.036f;        // N.L where shadow -> next band (first edge)
    float highT = 0.307f;       // N.L where the last band begins (last edge)
};

struct RimLight
{
    float intensity = 0.55f;                         // 0-1, how solid the rim color is
    glm::vec3 color = glm::vec3(1.0f);               // #FFFFFF
    float power = 3.0f;                              // higher = thinner rim
    glm::vec2 direction = glm::vec2(-0.65f, 1.0f);   // screen-space: x right, y up
};

struct Bloom
{
    bool bloomOnly = false;
    float rimGlow = 0.59f;      // how much of the rim light feeds the glow mask (0 = none)
    float ambientGlow = 0.15f;   // how much of the ambient light feeds the glow mask (0 = none)
    int levels = 4;
};

// ---- uniform buffer layouts (std140) -----------------------------------------
// Only mat4 / vec4 / ivec4 members, so the C++ layout is identical to std140 with no padding
// surprises. These must match FrameBlock / LightingBlock in light.vs.glsl, light.fs.glsl and
// outline.vs.glsl member for member.
namespace ubo
{
    constexpr unsigned int FRAME_BINDING = 0;
    constexpr unsigned int LIGHTING_BINDING = 1;
}

struct FrameData
{
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 lightSpaceMatrix;
    glm::vec4 viewPos;
};

struct LightingData
{
    glm::vec4 dirDirection;
    glm::vec4 dirAmbient;
    glm::vec4 dirDiffuse;
    glm::vec4 dirSpecular;
    glm::vec4 dirColor;
    glm::vec4 matSpecular;
    glm::vec4 rampShadow;
    glm::vec4 rampMid;
    glm::vec4 rampLight;
    glm::vec4 rampParams;
    glm::vec4 rimColor;
    glm::vec4 rimParams;
    glm::ivec4 toggles;
};

static_assert(sizeof(FrameData) == 3 * 64 + 16, "FrameData must match std140 FrameBlock");
static_assert(sizeof(LightingData) == 13 * 16, "LightingData must match std140 LightingBlock");

namespace shader_config
{
    FrameData makeFrameData(const glm::mat4& projection, const glm::mat4& view,
                            const glm::mat4& lightSpaceMatrix, const glm::vec3& viewPos);
    LightingData makeLightingData(const DirLight& dirLight, const glm::vec3& lightColor,
                                    const Material& material, const ColorRamp& ramp, const RimLight& rim,
                                    bool lighting, bool cellShading, bool rimLighting, bool shadowMapping,
                                    const Bloom& bloom);

    void applyOutline(Shader& shader, const Outline& outline);
}

#endif