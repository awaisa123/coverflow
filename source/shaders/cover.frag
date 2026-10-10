#version 450
layout(push_constant) uniform Push {
	mat4 mvp;
	vec4 params; // x: reflection strength (0 for the cover), y: brightness, z: alpha, w: kind (0 cover, 1 selected, 2 shadow, 3 glow)
	vec4 extra;  // xyz: accent colour, w: time in seconds
} pc;
layout(set = 0, binding = 0) uniform sampler2D coverTexture;
layout(location = 0) in vec2 inUV;
layout(location = 1) in vec2 inTexUV;
layout(location = 0) out vec4 outColor;

// Signed distance to a rounded square filling the quad (negative inside)
float roundedBox(vec2 uv, float radius)
{
	vec2 q = abs(uv - 0.5) - (0.5 - radius);
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

void main()
{
	const float kind = mod(pc.params.w, 10.0);
	// A tall cover is 3:4, so its corners are rounded on that shape
	const vec2 shape = pc.params.w > 9.5 ? vec2(0.75, 1.0) : vec2(1.0);
	if (kind > 2.5) {
		// The glow behind the selected cover: the accent colour fading out from its edge
		float d = roundedBox((inUV - 0.5) * 1.3 + 0.5, 0.06);
		float edge = min(min(inUV.x, 1.0 - inUV.x), min(inUV.y, 1.0 - inUV.y));
		float glow = exp(-max(d, 0.0) * 16.0) * smoothstep(0.0, 0.1, edge) * (0.85 + 0.15 * sin(pc.extra.w * 2.2));
		outColor = vec4(pc.extra.rgb * 1.15, glow * 0.55 * pc.params.z);
		return;
	}
	if (kind > 1.5) {
		// The soft shadow on the floor under a cover
		vec2 p = (inUV - 0.5) * 2.0;
		outColor = vec4(0.0, 0.0, 0.0, exp(-dot(p, p) * 2.6) * 0.6 * pc.params.z);
		return;
	}
	vec2 q = abs((inUV - 0.5) * shape) - (0.5 * shape - 0.035);
	float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - 0.035;
	float mask = 1.0 - smoothstep(-0.003, 0.002, d);
	vec3 color = texture(coverTexture, inTexUV).rgb * pc.params.y;
	float alpha = pc.params.z * mask;
	if (pc.params.x > 0.0) {
		// The reflection fades out going down from the floor (inUV.y = 1 at the floor)
		alpha *= pc.params.x * smoothstep(0.45, 1.0, inUV.y);
	} else {
		// A faint gloss across the top
		color += vec3(0.07) * smoothstep(0.62, 0.0, inUV.y + inUV.x * 0.3);
		if (kind > 0.5) {
			// The selected cover: a thin accent edge
			float edge = smoothstep(-0.016, -0.009, d);
			color = mix(color, pc.extra.rgb * 0.6 + vec3(0.4), edge);
		}
	}
	outColor = vec4(color, alpha);
}
