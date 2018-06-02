#line 1
// *****************************************************************************
/**
 * Uniforms
 *
 */

uniform float u_Exposure;
uniform float u_Gamma;

uniform float u_Alpha;
uniform vec3 u_Dir;
uniform vec4 u_Color;
uniform sampler1D u_CmapSampler;

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

// ============================================================================

vec3 evalBrdf(vec3 wi, vec3 wo)
{
	float c = clamp(wi.z, 0.0, 1.0);
#if BRDF_MERL
	return BRDF(wi, wo, vec3(0, 0, 1), vec3(1, 0, 0), vec3(0, 1, 0));
#else
	float pdf;
	return vec3(ggx_evalp(wi, wo, u_Alpha, pdf)) / wi.z;
#endif
}

float sRGB(float linear) {
	if (linear > 1.0) {
		return 1.0;
	} else if (linear < 0.0) {
		return 0.0;
	} else if (linear < 0.0031308) {
		return 12.92 * linear;
	} else {
		return 1.055 * pow(linear, 0.41666) - 0.055;
	}
}
vec3 tonemap(vec3 x) {
	vec3 tmp = exp2(u_Exposure) * x;
	return vec3(sRGB(tmp.x), sRGB(tmp.y), sRGB(tmp.z));
}


// *****************************************************************************
/**
 * Vertex Shader
 *
 * The shader outputs attributes relevant for shading in view space.
 */
#ifdef VERTEX_SHADER
layout(location = 0) out vec2 o_TexCoord;

void main(void)
{
	o_TexCoord = vec2(gl_VertexID & 1, gl_VertexID >> 1 & 1);
	gl_Position.xy = 2 * o_TexCoord - 1;
	gl_Position.zw = vec2(-0.95, 1);
}
#endif // VERTEX_SHADER

// *****************************************************************************
/**
 * Fragment Shader
 *
 */
#ifdef FRAGMENT_SHADER
layout(location = 0) in vec2 i_TexCoord;
layout(location = 0) out vec4 o_FragColor;

#define M_PI 3.14159

void main(void)
{
	float alpha = u_Alpha;
	float pdf = 0.0, dummy;
	vec3 wi = u_Dir;
	vec2 u = i_TexCoord;//floor(i_TexCoord * 32.0) / 32.0;
    vec3 wo = vec3(0, 0, 1);

#if SCHEME_GGX
    vec3 wm = ggx_sample(u, wi, alpha * 0.65);

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

#if SHADE_BRDF
    vec3 fr = evalBrdf(wi, wo);
    o_FragColor = vec4(u_Color.a * tonemap(fr), u_Color.a);

#elif SHADE_CMAP
    vec3 wr = 2 * wi.z * vec3(0, 0, 1) - wi;
	vec3 fr = evalBrdf(wi, wo) / (evalBrdf(wi, wr));
	float avg = dot(fr, vec3(1.0 / 3.0));
	vec3 rgb = texture(u_CmapSampler, avg).rgb;
    //if (avg == 0) rgb*= 0.0;
	o_FragColor = vec4(rgb, u_Color.a);

#elif SHADE_COLOR
    o_FragColor = u_Color;

#else
    o_FragColor = vec4(1, 0, 0, 1);
#endif
}
#endif // FRAGMENT_SHADER


