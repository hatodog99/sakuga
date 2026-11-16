#version 330 core
// bloom 1st pass: keep only the part of each pixel that is brighter than "threshold"
// the scene is linear HDR here, so values above 1.0 are real and can be thresholded.

out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D scene;
uniform float threshold;	// luminance where bloom starts
uniform float knee;			// 0...1; how softly bloom fades in below the threshold; (0 = hard cut) 

// from Rec 709 standard: reflects how bright/sensitive each RGB color is to the naked eye
const vec3 brightnessWeights = vec3(0.2126, 0.7152, 0.0722);

void main()
{
	vec3 color = texture(scene, TexCoords).rgb;
	float brightness = dot(color, brightnessWeights);

	// soft knee: quadratic ramp across [threshold - k, threshold + k] so bloom fades in
    // instead of popping on/off at the threshold
	float k = threshold * knee + 1e-4;
	float soft = clamp(brightness - threshold + k, 0.0, 2.0 * k);
	soft = soft * soft / (4.0 * k);

	float contribution = max(soft, brightness - threshold) / max(brightness, 1e-4);
	FragColor = vec4(color * contribution, 1.0);
}