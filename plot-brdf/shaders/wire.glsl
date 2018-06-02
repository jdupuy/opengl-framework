#line 1
// *****************************************************************************
/**
 * Uniforms
 *
 */
uniform int u_InstanceCount;

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
layout(location = 0) out vec4 o_Position;
layout(location = 1) out vec4 o_Normal;

void main(void)
{
	vec3 position;
	if (gl_InstanceID < u_InstanceCount) {
		// elevation slices
		float u = float(gl_InstanceID + 1) / float(u_InstanceCount + 1);
		float theta = u * 3.14159;
		float sinTheta = sin(theta);
		float cosTheta = cos(theta);

		position = vec3(sinTheta * i_Position.xy, cosTheta);
	} else {
		// azimuthal slices
		int instanceID = gl_InstanceID - u_InstanceCount;
		int instanceCnt = u_InstanceCount;
		float u = float(instanceID) / float(instanceCnt);
		float phi = 3.14159 * u;
		float cosPhi = cos(phi);
		float sinPhi = sin(phi);
		mat2 r = mat2(cosPhi, -sinPhi, sinPhi, cosPhi);

		position = vec3(r * i_Position.xz, i_Position.y);
	}

	vec3 normal = position;
	o_Position = u_Transform.modelView * vec4(normalize(position), 1);
	o_Normal   = u_Transform.modelView * vec4(normalize(normal), 0);

	gl_Position = u_Transform.modelViewProjection * vec4(position, 1);
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
	vec3 wo = normalize(i_Position.xyz);
	vec3 wn = normalize(i_Normal.xyz);
	vec4 wnw = u_Transform.viewInv * vec4(wn, 0);
	if (wnw.z < 0.0)
		discard;
	float alpha = dot(wn, wo) > 0.1 ? 1.0 : 0.5;
	o_FragColor = vec4(mix(vec3(0.5), vec3(0.9), alpha), 1);
}
#endif // FRAGMENT_SHADER


