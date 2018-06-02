#line 1
// *****************************************************************************
/**
 * Uniforms
 *
 */

uniform vec3 u_Dir;
uniform float u_Alpha;
uniform float u_PointScale = 96.0 * 1.5; //48.0;

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
 */
#ifdef VERTEX_SHADER
//layout(location = 0) in vec2 i_Position;
layout(location = 0) out vec3 o_Position;
layout(location = 1) out vec3 o_Normal;

void main(void)
{
    vec2 u = vec2(gl_VertexID / 16, gl_VertexID % 16) / 15.0;
    vec3 wi = u_Dir;
    vec3 wo = vec3(0, 0, 1);

#if SCHEME_GGX
    vec3 wm = ggx_sample(u, wi, u_Alpha);

    wo = 2.0 * dot(wi, wm) * wm - wi;
#else
    const float pi = 3.14159;
    float tm = u.x * u.x * pi / 2;
    float pm = u.y * pi * 2;
    float x = sin(tm) * cos(pm);
    float y = sin(tm) * sin(pm);
    float z = cos(tm);
    vec3 wm = vec3(x, y, z);

    wo = 2.0 * dot(wi, wm) * wm - wi;
#endif

    o_Position = (u_Transform.modelView * vec4(wo, 1)).xyz;
	o_Normal   = (u_Transform.modelView * vec4(wo, 0)).xyz;
	gl_Position = u_Transform.modelViewProjection * vec4(wo, 1);
    gl_PointSize = u_PointScale / (1.0 + 4.0 * length(o_Position));
}
#endif // VERTEX_SHADER


// *****************************************************************************
/**
 * Fragment Shader
 *
 */
#ifdef FRAGMENT_SHADER
layout(location = 0) in vec3 i_Position;
layout(location = 1) in vec3 i_Normal;

layout(location = 0) out vec4 o_FragColor;

void main(void)
{
	vec3 wo = normalize(i_Position);
	vec3 wn = normalize(i_Normal);
	vec2 P = vec2(gl_PointCoord.x, 1.0 - gl_PointCoord.y);
	vec2 p = 2.0 * P - 1.0;
	float r = dot(p, p);
    vec4 wnw = u_Transform.viewInv * vec4(wn, 0);
	if (r > 1.0 || wnw.z < 0.0)
			discard;

	float alpha = dot(wn, wo) > 0.1 ? 1.0 : 0.0;

	o_FragColor = vec4(mix(vec3(0.2), vec3(0.7), alpha), 1);
}
#endif // FRAGMENT_SHADER


