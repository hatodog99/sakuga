#version 330 core
// bloom last pass: scene + blurred bright regions, then linear -> sRGB

out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D scene;
uniform sampler2D bloomBlur;
uniform bool bloomEnabled;
uniform float bloomIntensity;
uniform bool bloomOnly;

const float GAMMA = 2.2;

void main()
{
	vec3 color = texture(scene, TexCoords).rgb;
	vec3 bloom = bloomEnabled ? texture(bloomBlur, TexCoords).rgb * bloomIntensity : vec3(0.0);

	vec3 result = bloomOnly ? bloom : color + bloom;		// additive blend, still linear
	result = clamp(result, 0.0, 1.0);						// clamp to 0..1
	FragColor = vec4(pow(result, vec3(1.0 / GAMMA)), 1.0);	// apply gamma correction
}