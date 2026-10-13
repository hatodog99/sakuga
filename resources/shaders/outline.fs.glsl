#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

unform bool gOutlineEnabled;
uniform vec3 outlineColor;
uniform sampler2D texture_diffuse1;

const float ALPHA_CUTOFF = 0.5;

void main()
{
	if (gOutlineEnabled)
	{
		if (texture(texture_diffuse1, TexCoords).a < ALPHA_CUTOFF)
			discard;
			
		FragColor = vec4(outlineColor, 1.0);
	}
}