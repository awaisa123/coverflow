#version 450
// The backdrop: the theme's picture when it has one (cover-fitted, dimmed),
// otherwise a soft glow in the accent colour fading to black; a darker floor
// below the horizon and a vignette either way.
layout(push_constant) uniform Push {
	mat4 mvp;    // [0][0]: 1 with a picture, [0][1]: picture aspect / screen aspect, [0][2]: dimming
	vec4 params; // xyz: accent colour, w: horizon height (0..1 from the top)
} pc;
layout(set = 0, binding = 0) uniform sampler2D picture;
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;
void main()
{
	vec3 color;
	if (pc.mvp[0][0] > 0.5) {
		// Cover-fit: fill the screen, cropping the picture's long side
		vec2 uv = inUV - 0.5;
		float r = pc.mvp[0][1];
		if (r > 1.0) {
			uv.x /= r;
		} else {
			uv.y *= r;
		}
		color = texture(picture, uv + 0.5).rgb * (1.0 - pc.mvp[0][2]);
	} else {
		vec2 p = inUV - vec2(0.5, pc.params.w - 0.12);
		p.x *= 1.9;
		float glow = exp(-dot(p, p) * 5.0);
		color = mix(vec3(0.012, 0.014, 0.022), pc.params.xyz * 0.55, glow * 0.55);
	}
	// The floor below the horizon is a little darker
	color *= mix(1.0, 0.55, smoothstep(pc.params.w - 0.005, pc.params.w + 0.08, inUV.y));
	// Vignette
	vec2 v = inUV - 0.5;
	color *= 1.0 - dot(v, v) * 0.9;
	outColor = vec4(color, 1.0);
}
