#version 450
// Cover Flow: one cover (or its reflection) as a unit quad, made from the vertex
// index: no vertex buffer. The quad stands on the floor (y = 0) and is 1 tall.
layout(push_constant) uniform Push {
	mat4 mvp;
	vec4 params; // x: reflection, y: brightness, z: alpha, w: selected
} pc;
layout(location = 0) out vec2 outUV;
const vec2 corners[6] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
                               vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));
void main()
{
	vec2 c = corners[gl_VertexIndex];
	outUV = vec2(c.x, 1.0 - c.y);
	gl_Position = pc.mvp * vec4(c.x - 0.5, c.y, 0.0, 1.0);
}
