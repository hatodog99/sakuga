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
    glm::vec3 direction = glm::vec3(-0.7f, -0.2f, -1.8f);
    glm::vec3 ambient = glm::vec3(0.6f);
    glm::vec3 diffuse = glm::vec3(0.5f);
    glm::vec3 specular = glm::vec3(0.5f);
    float ambientIntensity = 1.57f;
    float diffuseIntensity = 1.0f;
    float specularIntensity = 1.0f;
};

struct ShadowMap
{
    glm::vec3 lightDir = glm::vec3(-0.2f, -1.0f, -0.03f);
};

struct Outline
{
    float width = 0.0033f;	// world units: outline.vs.glsl extrudes along the normal in world space
    float depthBias = 0.0105f;
    glm::vec3 color = glm::vec3(0.0f);   // #14142B
};

// toon color ramp: `levels` flat bands going shadow -> midtone -> highlight
struct ColorRamp
{
    glm::vec3 shadow = glm::vec3(0.55f);   // #8C8C8C
    glm::vec3 mid = glm::vec3(0.78f);   // #C7C7C7
    glm::vec3 light = glm::vec3(1.00f);   // #FFFFFF
    int levels = 3;            // 2-8 bands
    float softness = 0.02f;    // 0 = hard anime edge
};

struct RimLight
{
    float intensity = 0.55f;                         // 0-1, how solid the rim color is
    glm::vec3 color = glm::vec3(1.0f);               // #FFFFFF
    float power = 3.0f;                              // higher = thinner rim
    glm::vec2 direction = glm::vec2(-0.65f, 1.0f);   // screen-space: x right, y up
};

namespace shader_config
{
    void applyCamera(Shader& shader, const glm::mat4& projection, const glm::mat4& view, const glm::vec3& viewPos);
    void applyLightingToggles(Shader& shader, bool lighting, bool cellShading, bool rimLighting, bool shadowMapping);
    void applyMaterial(Shader& shader, const Material& material);
    void applyDirLight(Shader& shader, const DirLight& dirLight, const glm::vec3& color);
    void applyOutline(Shader& shader, const Outline& outline, const glm::mat4& projection, const glm::mat4& view);
    void applyColorRamp(Shader& shader, const ColorRamp& ramp);
    void applyRimLight(Shader& shader, const RimLight& rim);
}

#endif