#version 450
// The backdrop: the theme's picture when it has one (cover-fitted, dimmed),
// otherwise two soft, slowly drifting glows in the accent colour; a darker
// floor below the horizon with a faint line of light along it, and a vignette.
layout(push_constant) uniform Push {
	mat4 mvp;    // [0][0]: 1 with a picture, 2 a game's artwork layer; [0][1]: picture aspect / screen aspect, [0][2]: dimming, [0][3]: the layer's opacity
	vec4 params; // xyz: accent colour, w: horizon height (0..1 from the top)
	vec4 extra;  // xyz: accent colour, w: time in seconds
} pc;
layout(set = 0, binding = 0) uniform sampler2D picture;
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;
void main()
{
	const float t = pc.extra.w;
	const float horizon = pc.params.w;
	vec3 color;
	const float mode = pc.mvp[0][0];
	if (mode > 0.5) {
		vec2 uv = inUV - 0.5;
		float r = pc.mvp[0][1];
		if (r > 1.0) {
			uv.x /= r;
		} else {
			uv.y *= r;
		}
		color = texture(picture, uv + 0.5).rgb * (1.0 - pc.mvp[0][2]);
		if (mode > 1.5) {
			// A game's artwork takes on the navy of the rest
			float l = dot(color, vec3(0.299, 0.587, 0.114));
			color = mix(color, vec3(0.05, 0.12, 0.30) + l * vec3(0.35, 0.55, 1.0), 0.45);
		}
	} else {
		vec3 accent = pc.params.xyz;
		vec2 a = inUV - vec2(0.5 + 0.08 * sin(t * 0.11), horizon - 0.14 + 0.03 * sin(t * 0.17));
		a.x *= 1.8;
		vec2 b = inUV - vec2(0.5 - 0.22 * sin(t * 0.07 + 1.0), horizon - 0.32);
		b.x *= 1.4;
		float glowA = exp(-dot(a, a) * 5.0);
		float glowB = exp(-dot(b, b) * 7.0);
		// Deep navy, lit from behind the row in the accent colour
		color = mix(vec3(0.031, 0.047, 0.078), vec3(0.035, 0.070, 0.150), (1.0 - inUV.y) * 0.8); // #080C14 rising to a deeper blue
		color += accent * 0.55 * glowA;
		color += accent * vec3(0.55, 0.75, 1.0) * 0.22 * glowB;
		// Soft rays fanning up from behind the selected cover
		vec2 c = inUV - vec2(0.5, horizon - 0.08);
		float ang = atan(c.x, -c.y);
		float rays = pow(0.5 + 0.5 * sin(ang * 9.0 + t * 0.15), 6.0) * exp(-length(c) * 2.2) * smoothstep(0.0, -0.1, c.y);
		color += accent * 0.10 * rays;
	}
	// The floor below the horizon is darker, with a faint line of light along it
	color *= mix(1.0, 0.5, smoothstep(horizon - 0.005, horizon + 0.08, inUV.y));
	color += pc.params.xyz * 0.10 * exp(-abs(inUV.y - horizon) * 160.0) * exp(-pow((inUV.x - 0.5) * 2.2, 2.0));
	// Vignette
	vec2 v = inUV - 0.5;
	color *= 1.0 - dot(v, v) * 0.95;
	// A whisper of grain against banding
	float n = fract(sin(dot(inUV * 1000.0, vec2(12.9898, 78.233))) * 43758.5453);
	color += (n - 0.5) / 255.0;
	outColor = vec4(color, mode > 1.5 ? pc.mvp[0][3] : 1.0);
}
