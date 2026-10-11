#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

out vec2 TexCoords;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

uniform float outlineWidth;

void main()
{
    vec3 FragPos = vec3(model * vec4(aPos, 1.0));
    vec3 Normal = normalize(mat3(transpose(inverse(model))) * aNormal);
    TexCoords = aTexCoords;

    gl_Position = projection * view * vec4(FragPos + Normal * outlineWidth, 1.0);
}   