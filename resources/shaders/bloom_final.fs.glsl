#version 330 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D scene;        // resolved scene color (already gamma corrected by light.fs)
uniform sampler2D bloomBlur;    // blurred bright regions
uniform bool bloomEnabled;
uniform float bloomIntensity;

void main()
{
    vec3 color = texture(scene, TexCoords).rgb;
    if (bloomEnabled)
    {
        vec3 bloom = texture(bloomBlur, TexCoords).rgb * bloomIntensity;
        // screen blend: glows without clipping to pure white
        color = 1.0 - (1.0 - color) * (1.0 - clamp(bloom, 0.0, 1.0));
    }
    FragColor = vec4(color, 1.0);
}
