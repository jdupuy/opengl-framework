#line 1
// *****************************************************************************
/**
 * Uniforms
 *
 */

uniform vec3 u_Dir;

struct Transform {
	mat4 modelView;
	mat4 projection;
	mat4 modelViewProjection;
	mat4 viewInv;
};

layout(std140, row_major, binding = BUFFER_BINDING_TRANSFORMS)
uniform TransformBuffer {
	Transform u_Transform;
};

// *****************************************************************************
/**
 * Vertex Shader
 *
 * The shader outputs attributes relevant for shading in view space.
 */
#ifdef VERTEX_SHADER
void main(void)
{
    vec3 p = gl_VertexID * u_Dir * 1.5;
	gl_Position = u_Transform.modelViewProjection * vec4(p, 1);
}
#endif // VERTEX_SHADER

// *****************************************************************************
/**
 * Fragment Shader
 *
 */
#ifdef FRAGMENT_SHADER
layout(location = 0) out vec4 o_FragColor;

void main(void)
{
	o_FragColor = vec4(vec3(0), 1);
}
#endif // FRAGMENT_SHADER


