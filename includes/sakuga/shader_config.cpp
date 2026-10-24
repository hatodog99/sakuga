#include <sakuga/shader_config.h>

namespace shader_config
{
    void applyCamera(Shader& shader, const glm::mat4& projection, const glm::mat4& view, const glm::vec3& viewPos)
    {
        shader.setMat4("projection", projection);
        shader.setMat4("view", view);
        shader.setVec3("viewPos", viewPos);
    }

    void applyLightingToggles(Shader& shader, bool lighting, bool cellShading, bool rimLighting)
    {
        shader.setBool("gLightingEnabled", lighting);
        shader.setBool("gCellShadingEnabled", cellShading);
        shader.setBool("gRimLightingEnabled", rimLighting);
    }

    void applyMaterial(Shader& shader, const Material& material)
    {
        shader.setVec3("material.specular", material.specular);
        shader.setFloat("material.shininess", material.shininess);
    }

    void applyDirLight(Shader& shader, const DirLight& dirLight, glm::vec3& color)
    {
        shader.setVec3("dirLight.direction", dirLight.direction);
        shader.setVec3("dirLight.ambient", dirLight.ambient);
        shader.setVec3("dirLight.diffuse", dirLight.diffuse);
        shader.setVec3("dirLight.specular", dirLight.specular);
        shader.setVec3("dirLight.color", color);
    }

    void applyOutline(Shader& shader, Outline& outline, glm::vec3& color, const glm::mat4& projection, const glm::mat4& view)
    {
        shader.setVec3("outlineColor", color);
        shader.setFloat("outlineWidth", outline.width);
        shader.setFloat("depthBias", outline.depthBias);
        shader.setMat4("projection", projection);
        shader.setMat4("view", view);
    }
}