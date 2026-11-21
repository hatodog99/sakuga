#version 330 core
// dual-filter bloom, upsample: reads a texture half the size of the target.
// 8 bilinear taps in a tent pattern (4 edge taps weight 1, 4 diagonal taps weight 2) /12.
// the C++ side draws this with additive blending, so each level adds onto the one above it.

out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D image;    // smaller level (half the target size)

void main()
{
	// half a source texel
	vec2 h = 0.5 / vec2(textureSize(image, 0));

	vec3 sum = texture(image, TexCoords + vec2(-2.0 * h.x, 0.0)).rgb;
	sum += texture(image, TexCoords + vec2(-h.x,  h.y)).rgb * 2.0;
	sum += texture(image, TexCoords + vec2(0.0,  2.0 * h.y)).rgb;
	sum += texture(image, TexCoords + vec2( h.x,  h.y)).rgb * 2.0;
	sum += texture(image, TexCoords + vec2( 2.0 * h.x, 0.0)).rgb;
	sum += texture(image, TexCoords + vec2( h.x, -h.y)).rgb * 2.0;
	sum += texture(image, TexCoords + vec2(0.0, -2.0 * h.y)).rgb;
	sum += texture(image, TexCoords + vec2(-h.x, -h.y)).rgb * 2.0;

	FragColor = vec4(sum / 12.0, 1.0);
}
