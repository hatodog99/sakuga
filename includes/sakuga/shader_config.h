#ifndef SHADRE_CONFIG_H
#define SHADRE_CONFIG_H

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
    inline glm::vec3 darkRedOutline(0.314f, 0.137f, 0.137f);         // #502323
    inline glm::vec3 darkBlueOutline(0.235f, 0.255f, 0.314f);          // #3C4150
}

struct Material
{
    glm::vec3 specular = glm::vec3(0.2f);
    float shininess = 8.0f;
};

struct DirLight
{
    glm::vec3 direction = glm::vec3(-0.7f, -0.2f, -1.8f);
    glm::vec3 ambient = glm::vec3(0.3f);
    glm::vec3 diffuse = glm::vec3(0.3f);
    glm::vec3 specular = glm::vec3(0.5f);
};  

struct Outline
{
    float width = 0.005f;	// world units: outline.vs.glsl extrudes along the normal in world space
    float depthBias = 0.02;
};

namespace shader_config
{
    void applyCamera(Shader& shader, const glm::mat4& projection, const glm::mat4& view, const glm::vec3& viewPos);
    void applyLightingToggles(Shader& shader, bool lighting, bool cellShading, bool rimLighting);
    void applyMaterial(Shader& shader, const Material& material);
    void applyDirLight(Shader& shader, const DirLight& dirLight, glm::vec3& color);
    void applyOutline(Shader& shader, Outline& outline, glm::vec3& color, const glm::mat4& projection, const glm::mat4& view);
}

#endif