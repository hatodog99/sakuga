#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out vec4 FragPosLightSpace;

// shared per-frame data (binding 0, see shader_config.h). must match light.fs.glsl / outline.vs.glsl
layout (std140) uniform FrameBlock
{
    mat4 view;
    mat4 projection;
    mat4 lightSpaceMatrix;
    vec4 viewPosition;      // xyz
};

uniform mat4 model;
uniform mat3 normalMatrix;   // normal matrix, built once per draw on the CPU

void main()
{
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = normalMatrix * aNormal;
    TexCoords = aTexCoords;
    FragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);

    gl_Position = projection * view * vec4(FragPos, 1.0);
}