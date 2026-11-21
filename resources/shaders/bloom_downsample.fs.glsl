#version 330 core
// dual-filter bloom, downsample: reads a texture twice the size of the target.
// 5 bilinear taps (center weighted 4, four corners weighted 1) /8 = a cheap, alias-free 2x reduction.

out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D image;    // source level (twice the target size)

void main()
{
	// one source texel (= half a target texel), so the corner taps land on the edges of the 2x2 block
	vec2 o = 1.0 / vec2(textureSize(image, 0));

	vec3 sum = texture(image, TexCoords).rgb * 4.0;
	sum += texture(image, TexCoords + vec2(-o.x, -o.y)).rgb;
	sum += texture(image, TexCoords + vec2( o.x, -o.y)).rgb;
	sum += texture(image, TexCoords + vec2(-o.x,  o.y)).rgb;
	sum += texture(image, TexCoords + vec2( o.x,  o.y)).rgb;

	FragColor = vec4(sum / 8.0, 1.0);
}
