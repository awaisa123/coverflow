#version 450
layout(push_constant) uniform Push {
	mat4 mvp;
	vec4 params; // x: reflection strength (0 for the cover itself), y: brightness, z: alpha, w: selected
} pc;
layout(set = 0, binding = 0) uniform sampler2D coverTexture;
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;
void main()
{
	vec3 color = texture(coverTexture, inUV).rgb * pc.params.y;
	float alpha = pc.params.z;
	if (pc.params.x > 0.0) {
		// The reflection fades out going down from the floor (inUV.y = 1 at the floor)
		alpha *= pc.params.x * smoothstep(0.45, 1.0, inUV.y);
	} else if (pc.params.w > 0.5) {
		// A thin bright edge round the selected cover
		float edge = min(min(inUV.x, 1.0 - inUV.x), min(inUV.y, 1.0 - inUV.y));
		color = mix(vec3(1.0), color, smoothstep(0.004, 0.009, edge));
	}
	outColor = vec4(color, alpha);
}
