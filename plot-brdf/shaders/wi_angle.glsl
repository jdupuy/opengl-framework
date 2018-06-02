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
layout(location = 0) in vec4 i_Position;

void main(void)
{
	int cnt = VERTEX_CNT;
	vec3 position = vec3(0);

	if (gl_VertexID < cnt - 1) {
		vec2 azimuthal = normalize(u_Dir.xy);
		float u = float(gl_VertexID) / float(cnt - 2);
		float cosTheta = cos(acos(u_Dir.z) * u);
		float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
		vec2 p = vec2(sinTheta, 0);
		mat2 R = mat2(azimuthal.x, azimuthal.y,
		              -azimuthal.y, azimuthal.x);

		position = vec3(R * p, cosTheta);
	}

	gl_Position = u_Transform.modelViewProjection 
                * vec4(position * 1.001, 1);
}
#endif // VERTEX_SHADER

// *****************************************************************************
/**
 * Fragment Shader
 *
 */
#ifdef FRAGMENT_SHADER
layout(location = 0) in vec4 i_Position;
layout(location = 1) in vec4 i_Normal;
layout(location = 0) out vec4 o_FragColor;

void main(void)
{
	const vec3 blue  = vec3(60.0/255.0, 128.0/255.0, 1.0);
	o_FragColor = vec4(vec3(0), 1);
}
#endif // FRAGMENT_SHADER


