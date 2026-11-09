#version 330 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D image;
uniform bool horizontal;
uniform float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main()
{
    vec2 texOffset = 1.0 / textureSize(image, 0);   // size of one texel
    vec2 dir = horizontal ? vec2(texOffset.x, 0.0) : vec2(0.0, texOffset.y);

    vec3 result = texture(image, TexCoords).rgb * weight[0];
    for (int i = 1; i < 5; ++i)
    {
        result += texture(image, TexCoords + dir * float(i)).rgb * weight[i];
        result += texture(image, TexCoords - dir * float(i)).rgb * weight[i];
    }
    FragColor = vec4(result, 1.0);
}
