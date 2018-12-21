#line 1
// *****************************************************************************
/**
 * Uniforms
 *
 */
uniform int u_SamplesPerPass;


struct Transform {
	mat4 modelView;
	mat4 projection;
	mat4 modelViewProjection;
	mat4 viewInv;
};

layout(std140, row_major, binding = BUFFER_BINDING_TRANSFORMS)
uniform Transforms {
	Transform u_Transform;
};

layout(std140, binding = BUFFER_BINDING_RANDOM)
uniform Random {
	vec4 value[64];
} u_Random;

vec4 rand(int idx) { return u_Random.value[idx]; }
float hash(vec2 p)
{
	float h = dot(p, vec2(127.1, 311.7));
	return fract(sin(h) * 43758.5453123);
}

uniform float u_Exposure;
uniform float u_Gamma;

uniform float u_Alpha;
uniform vec3 u_Dir;
uniform vec4 u_Color;
uniform sampler1D u_CmapSampler;

// ============================================================================

vec3 evalBrdf(vec3 wi, vec3 wo)
{
	float c = clamp(wi.z, 0.0, 1.0);
#if BRDF_MERL
    return BRDF(wo, wi, vec3(0, 0, 1), vec3(1, 0, 0), vec3(0, 1, 0));
#else
	float pdf;
    return vec3(ggx_evalp(wo, wi, u_Alpha, pdf));
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



// ============================================================================

// *****************************************************************************
/**
 * Vertex Shader
 *
 * The shader outputs attributes relevant for shading in view space.
 */
#ifdef VERTEX_SHADER
layout(location = 0) in vec4 i_Position;
layout(location = 1) in vec4 i_TexCoord;
layout(location = 2) in vec4 i_Tangent1;
layout(location = 3) in vec4 i_Tangent2;
layout(location = 0) out vec4 o_Position;
layout(location = 1) out vec4 o_TexCoord;
layout(location = 2) out vec4 o_Tangent1;
layout(location = 3) out vec4 o_Tangent2;

void main(void)
{
	o_Position = u_Transform.viewInv * u_Transform.modelView * i_Position;
	o_TexCoord = i_TexCoord;
	o_Tangent1 = u_Transform.viewInv * u_Transform.modelView * i_Tangent1;
	o_Tangent2 = u_Transform.viewInv * u_Transform.modelView * i_Tangent2;

	gl_Position = u_Transform.modelViewProjection * i_Position;
}
#endif // VERTEX_SHADER

// *****************************************************************************
/**
 * Fragment Shader
 *
 */
#ifdef FRAGMENT_SHADER
layout(location = 0) in vec4 i_Position;
layout(location = 1) in vec4 i_TexCoord;
layout(location = 2) in vec4 i_Tangent1;
layout(location = 3) in vec4 i_Tangent2;
layout(location = 0) out vec4 o_FragColor;

void main(void)
{
	vec3 wi = u_Dir;
	vec3 wo = normalize(i_Position.xyz);
// -----------------------------------------------------------------------------
/**
 * Shading with Importance Sampling
 *
 */
#if SHADE_BRDF
	vec3 fr = evalBrdf(wi, wo);

	o_FragColor = vec4(u_Color.a * tonemap(fr), u_Color.a);

// -----------------------------------------------------------------------------
/**
 * Color Shading
 *
 * Outputs a flat color
 */
#elif SHADE_CMAP
	vec3 wr = 2 * wi.z * vec3(0, 0, 1) - wi;
	vec3 fr = evalBrdf(wi, wo) / (evalBrdf(wi, wr));
	float avg = dot(fr, vec3(1.0 / 3.0));
	vec3 rgb = texture(u_CmapSampler, avg).rgb;

	o_FragColor = vec4(rgb, u_Color.a);


// -----------------------------------------------------------------------------
/**
 * Color Shading
 *
 * Outputs a flat color
 */
#elif SHADE_COLOR
	o_FragColor = u_Color;


// -----------------------------------------------------------------------------
/**
 * Debug Shading
 *
 * Do whatever you like in here.
 */
#elif SHADE_DEBUG
#if 0
	float h = hash(gl_FragCoord.xy);
	float u = mod(h + rand(0).x, 1.0);
	if (u > 0.5)
		o_FragColor = vec4(vec3(1, 0, 1), 0.5);
	else
		o_FragColor = vec4(vec3(0, 1, 0), 0.5);
#else
	o_FragColor = u_Color;
#endif

// -----------------------------------------------------------------------------
/**
 * Default Shading: flat red
 *
 * 
 */
#else
	o_FragColor = vec4(1, 0, 0, 1);


// -----------------------------------------------------------------------------
#endif // SHADE

}
#endif // FRAGMENT_SHADER


