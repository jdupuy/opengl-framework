// --------------------------------------------------
// Uniforms
// --------------------------------------------------
uniform vec3 u_ClearColor;
uniform sampler2D u_EnvmapSampler;

// --------------------------------------------------
// Vertex shader
// --------------------------------------------------
#ifdef VERTEX_SHADER
layout(location = 0) out vec2 o_TexCoord;
void main() {
	// draw a full screen quad
	vec2 p = vec2(gl_VertexID & 1, gl_VertexID >> 1 & 1) * 2.0 - 1.0;
	o_TexCoord = vec2(gl_VertexID & 1, gl_VertexID >> 1 & 1);
	gl_Position = vec4(p, 0.999, 1); // make sure the cubemap is visible
}
#endif

// --------------------------------------------------
// Fragment shader
// --------------------------------------------------
#ifdef FRAGMENT_SHADER
layout(location = 0) in vec2 i_TexCoord;
layout(location = 0) out vec4 o_FragColor;

void main() {
	o_FragColor = vec4(u_ClearColor, 1);
	o_FragColor = texture(u_EnvmapSampler, i_TexCoord + 0*vec2(-0.25, -0.25));
}
#endif


