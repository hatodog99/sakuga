#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform vec3 outlineColor;
uniform sampler2D texture_diffuse1;

const float ALPHA_CUTOFF = 0.5;

// the composite pass gamma-encodes the whole scene buffer, so pre-darken the outline color
// (sRGB -> linear) to cancel it out and keep the picked color unchanged on screen
const vec3 gamma = vec3(2.2);

void main()
{
	if (texture(texture_diffuse1, TexCoords).a < ALPHA_CUTOFF)
		discard;
		
	// convert outline color to linear
	FragColor = vec4(pow(outlineColor, gamma), 1.0);
}