#include <sakuga/shader_config.h>

namespace shader_config
{
    void applyCamera(Shader& shader, const glm::mat4& projection, const glm::mat4& view, const glm::vec3& viewPos)
    {
        shader.setMat4("projection", projection);
        shader.setMat4("view", view);
        shader.setVec3("viewPos", viewPos);
    }

    void applyLightingToggles(Shader& shader, bool lighting, bool cellShading, bool rimLighting, bool shadowMapping)
    {
        shader.setBool("gLightingEnabled", lighting);
        shader.setBool("gCellShadingEnabled", cellShading);
        shader.setBool("gRimLightingEnabled", rimLighting);
        shader.setBool("gShadowMappingEnabled", shadowMapping);
    }

    void applyMaterial(Shader& shader, const Material& material)
    {
        shader.setVec3("material.specular", material.specular);
        shader.setFloat("material.shininess", material.shininess);
    }

    void applyDirLight(Shader& shader, const DirLight& dirLight, const glm::vec3& color)
    {
        shader.setVec3("dirLight.direction", dirLight.direction);
        shader.setVec3("dirLight.ambient", dirLight.ambient * dirLight.ambientIntensity);
        shader.setVec3("dirLight.diffuse", dirLight.diffuse * dirLight.diffuseIntensity);
        shader.setVec3("dirLight.specular", dirLight.specular * dirLight.specularIntensity);
        shader.setVec3("dirLight.color", color);
    }

    void applyOutline(Shader& shader, const Outline& outline, const glm::mat4& projection, const glm::mat4& view)
    {
        shader.setVec3("outlineColor", outline.color);
        shader.setFloat("outlineWidth", outline.width);
        shader.setFloat("depthBias", outline.depthBias);
        shader.setMat4("projection", projection);
        shader.setMat4("view", view);
    }

    void applyColorRamp(Shader& shader, const ColorRamp& ramp)
    {
        shader.setVec3("ramp.shadow", ramp.shadow);
        shader.setVec3("ramp.mid", ramp.mid);
        shader.setVec3("ramp.light", ramp.light);
        shader.setInt("ramp.levels", ramp.levels);
        shader.setFloat("ramp.softness", ramp.softness);
        shader.setFloat("ramp.lowT", ramp.lowT);
        shader.setFloat("ramp.highT", ramp.highT);
        shader.setFloat("ramp.lowT", ramp.lowT);
        shader.setFloat("ramp.highT", ramp.highT);
    }

    void applyRimLight(Shader& shader, const RimLight& rim)
    {
        shader.setFloat("rimIntensity", rim.intensity);
        shader.setVec3("rimColor", rim.color);
        shader.setFloat("rimPower", rim.power);
        shader.setVec2("rimDirection", rim.direction);
    }
}