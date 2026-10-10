#version 450
// Cover Flow: one cover, its reflection, its shadow or the glow behind it, as
// a unit quad made from the vertex index: no vertex buffer. The quad stands on
// the floor (y = 0) and is 1 tall.
layout(push_constant) uniform Push {
	mat4 mvp;
	vec4 params; // x: reflection strength (0 for the cover), y: brightness, z: alpha, w: kind (0 cover, 1 selected, 2 shadow, 3 glow; +10: a tall cover)
	vec4 extra;  // xyz: accent colour, w: time in seconds
} pc;
layout(location = 0) out vec2 outUV;    // across the quad: its shape
layout(location = 1) out vec2 outTexUV; // into the cover: the middle three quarters for a tall one
const vec2 corners[6] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
                               vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));
void main()
{
	vec2 c = corners[gl_VertexIndex];
	outUV = vec2(c.x, 1.0 - c.y);
	const float crop = pc.params.w > 9.5 ? 0.125 : 0.0;
	outTexUV = vec2(crop + outUV.x * (1.0 - 2.0 * crop), outUV.y);
	gl_Position = pc.mvp * vec4(c.x - 0.5, c.y, 0.0, 1.0);
}
