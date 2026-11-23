#include <sakuga/shader_config.h>

namespace shader_config
{
    FrameData makeFrameData(const glm::mat4& projection, const glm::mat4& view,
                            const glm::mat4& lightSpaceMatrix, const glm::vec3& viewPos)
    {
        FrameData d;
        d.view = view;
        d.projection = projection;
        d.lightSpaceMatrix = lightSpaceMatrix;
        d.viewPos = glm::vec4(viewPos, 0.0f);
        return d;
    }

    LightingData makeLightingData(const DirLight& dirLight, const glm::vec3& lightColor,
                                    const Material& material, const ColorRamp& ramp, const RimLight& rim,
                                    bool lighting, bool cellShading, bool rimLighting, bool shadowMapping,
                                    float ambientGlow)
    {
        LightingData d;
        d.dirDirection = glm::vec4(dirLight.direction, 0.0f);
        d.dirAmbient = glm::vec4(dirLight.ambient * dirLight.ambientIntensity, 0.0f);
        d.dirDiffuse = glm::vec4(dirLight.diffuse * dirLight.diffuseIntensity, 0.0f);
        d.dirSpecular = glm::vec4(dirLight.specular * dirLight.specularIntensity, 0.0f);
        d.dirColor = glm::vec4(lightColor, 0.0f);
        d.matSpecular = glm::vec4(material.specular, material.shininess);
        d.rampShadow = glm::vec4(ramp.shadow, 0.0f);
        d.rampMid = glm::vec4(ramp.mid, 0.0f);
        d.rampLight = glm::vec4(ramp.light, 0.0f);
        d.rampParams = glm::vec4(ramp.softness, ramp.lowT, ramp.highT, (float)ramp.levels);
        d.rimColor = glm::vec4(rim.color, rim.intensity);
        d.rimParams = glm::vec4(rim.power, rim.direction.x, rim.direction.y, 0.0f);
        d.toggles = glm::ivec4(lighting, cellShading, rimLighting, shadowMapping);
        d.rimParams = glm::vec4(rim.power, rim.direction.x, rim.direction.y, ambientGlow);
        return d;
    }

    void applyOutline(Shader& shader, const Outline& outline)
    {
        shader.setVec3("outlineColor", outline.color);
        shader.setFloat("outlineWidth", outline.width);
        shader.setFloat("depthBias", outline.depthBias);
    }
}