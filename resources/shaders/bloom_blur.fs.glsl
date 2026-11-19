#version 330 core
// bloom 2nd pass: gaussian blur (horizontal or vertical)

out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D image;
uniform bool horizontal;

// gaussian (bell-curve) falloff sampled at 5 points
uniform float weight[5] = float[] (0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

vec4 blur5(sampler2D image, vec2 uv, vec2 resolution, vec2 direction) {
  vec4 color = vec4(0.0);
  vec2 off1 = vec2(1.3333333333333333) * direction;
  color += texture(image, uv) * 0.29411764705882354;
  color += texture(image, uv + (off1 / resolution)) * 0.35294117647058826;
  color += texture(image, uv - (off1 / resolution)) * 0.35294117647058826;
  return color; 
}

void main()
{
	vec2 texel = 1.0 / textureSize(image, 0);	// size of one texel
  vec2 dir = horizontal ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);

	// blur texels
	// vec3 result = texture(image, TexCoords).rgb * weight[0];
	// for (int i = 1; i < weight.length(); ++i)
	// {
  //  result += texture(image, TexCoords + dir * float(i)).rgb * weight[i];
	//	result += texture(image, TexCoords - dir * float(i)).rgb * weight[i];
	// }  
  vec2 resolution = vec2(0.5);
	FragColor = vec4(blur5(image, TexCoords, resolution, dir).rgb, 1.0);
	// FragColor = vec4(result, 1.0);
}