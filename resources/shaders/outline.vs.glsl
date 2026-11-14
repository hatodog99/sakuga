#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

out vec2 TexCoords;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

uniform float outlineWidth;   // world units, e.g. 0.002 - 0.01
uniform float depthBias;      // try 0.0005 - 0.005

// distance from the camera at which depthBias has its "face value" (default camera is ~3 away)
const float REF_DISTANCE = 3.0;

void main()
{
    TexCoords = aTexCoords;

    // push the vertex out along its normal in world space
    mat3 normalMat = transpose(inverse(mat3(model)));
    vec3 worldNormal = normalize(normalMat * aNormal);
    vec3 worldPos = vec3(model * vec4(aPos, 1.0)) + worldNormal * outlineWidth;

    vec4 clipPos = projection * view * vec4(worldPos, 1.0);

    // depthBias * REF_DISTANCE / w, so it is bigger when the camera is close
    // and smaller when it is far. positive = farther from camera
    clipPos.z += depthBias * REF_DISTANCE;

    gl_Position = clipPos;
}