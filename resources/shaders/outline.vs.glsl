#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

out vec2 TexCoords;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

uniform float outlineWidth;   // e.g. 0.002 - 0.01
uniform float aspect;         // viewport width / height
uniform float depthBias;      // try 0.0005 - 0.005

void main()
{
    TexCoords = aTexCoords;

    vec4 clipPos = projection * view * model * vec4(aPos, 1.0);

    mat3 normalMat = transpose(inverse(mat3(view * model)));
    vec3 viewNormal = normalize(normalMat * aNormal);
    vec2 dir = normalize((projection * vec4(viewNormal, 0.0)).xy);
    dir.x /= aspect;

    clipPos.xy += dir * outlineWidth * clipPos.w;
    clipPos.z  += depthBias * clipPos.w;   // positive = farther from camera
    gl_Position = clipPos;
}