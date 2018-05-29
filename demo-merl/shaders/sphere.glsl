#line 1
// *****************************************************************************
/**
 * Uniforms
 *
 */
uniform int u_SamplesPerPass;


struct Transform {
	mat4 model;
	mat4 modelView;
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

uniform sampler2D u_EnvmapSampler;
uniform float u_Alpha;
uniform int u_MerlId = 0;

// ============================================================================

vec3 evalEnvmap(vec3 dir)
{
	float u1 = atan(dir.x, dir.y) / M_PI * 0.5 + 0.5;
	float u2 = 1.0 - acos(dir.z) / M_PI;
	return textureLod(u_EnvmapSampler, vec2(u1, u2), 0.0).rgb;
}

vec3 evalBrdf(vec3 wi, vec3 wo)
{
	float c = clamp(wi.z, 0.0, 1.0);
#if BRDF_NPF
	return getBRDF(u_MerlId, wi, wo, vec3(0, 0, 1)) * c / M_PI;
#elif BRDF_MERL
	return BRDF(wi, wo, vec3(0, 0, 1), vec3(1, 0, 0), vec3(0, 1, 0)) * c;
#else
	return vec3(c / 3.14159265359);
#endif
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
layout(location = 4) flat out int o_SphereId;

void main(void)
{
	o_Position = u_Transform.modelView * i_Position;
	o_TexCoord = i_TexCoord;
	o_Tangent1 = u_Transform.modelView * i_Tangent1;
	o_Tangent2 = u_Transform.modelView * i_Tangent2;
	o_SphereId = gl_InstanceID;

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
layout(location = 4) flat in int i_SphereId;
layout(location = 0) out vec4 o_FragColor;

void main(void)
{
	// extract attributes
	vec3 wx = normalize(i_Tangent1.xyz);
	vec3 wy = normalize(i_Tangent2.xyz);
	vec3 wn = normalize(cross(wx, wy));
	vec3 wo = normalize(-i_Position.xyz);
	mat3 tgInv = mat3(wy, wx, wn);
	mat3 tg = transpose(mat3(wx, wy, wn));

	// express data in tangent space
	wo = tg * wo;
	wn = vec3(0, 0, 1);

	// initialize emitted and outgoing radiance
	vec3 Lo = vec3(0);

// -----------------------------------------------------------------------------
/**
 * Shading with Importance Sampling
 *
 */
#if (SHADE_MC_GGX || SHADE_MC_COS)
		// loop over all samples
	for (int j = 0; j < u_SamplesPerPass; ++j) {
		// compute a uniform sample
		float h1 = hash(gl_FragCoord.xy);
		float h2 = hash(gl_FragCoord.yx);
		vec2 u2 = mod(vec2(h1, h2) + rand(j).xy, vec2(1.0));
#if SHADE_MC_COS
		vec3 wi = u2_to_cos(u2);
		float pdf = pdf_cos(wi);
#elif SHADE_MC_GGX
		vec3 wm = ggx_sample(u2, wo, u_Alpha);
		vec3 wi = 2.0 * dot(wm, wo) * wm - wo;
		float pdf;
		ggx_evalp(wi, wo, u_Alpha, pdf);
#endif
		float pdf_dummy;

		vec3 frp = evalBrdf(wi, wo);
		vec4 tmp = vec4(transpose(tg) * wi, 0);
		vec3 wiWorld = normalize( (u_Transform.viewInv * tmp).xyz );
		vec3 Li = evalEnvmap(wiWorld);

		if (pdf > 0.0)
			Lo+= Li * frp / pdf;
	}

	o_FragColor = vec4(Lo, u_SamplesPerPass);

// -----------------------------------------------------------------------------
/**
 * Shading with MIS
 *
 */
#elif SHADE_MC_MIS
	// loop over all samples
	for (int j = 0; j < u_SamplesPerPass; ++j) {
		// compute a uniform sample
		float h1 = hash(gl_FragCoord.xy);
		float h2 = hash(gl_FragCoord.yx);
		vec2 u2 = mod(vec2(h1, h2) + rand(j).xy, vec2(1.0));

		// importance sample the GGX Approx
		if (true) {
			vec3 wm = ggx_sample(u2, wo, u_Alpha);
			vec3 wi = 2.0 * wm * dot(wo, wm) - wo;
			float pdf1;
			ggx_evalp(wi, wo, u_Alpha, pdf1);
			vec3 frp = evalBrdf(wi, wo);
			vec4 tmp = vec4(transpose(tg) * wi, 0);
			vec3 wiWorld = normalize( (u_Transform.viewInv * tmp).xyz );
			vec3 Li = evalEnvmap(wiWorld);

			// raytrace the sphere light
			if (pdf1 > 0.0) {
				float pdf2 = pdf_cos(wi);
				float misWeight = pdf1 * pdf1;
				float misNrm = pdf1 * pdf1 + pdf2 * pdf2;

				Lo+= Li * frp / pdf1 * misWeight / misNrm;
			}
		}

		// importance sample Cos
		if (true) {
			vec3 wi = u2_to_cos(u2);
			float pdf2 = pdf_cos(wi);
			vec3 frp = evalBrdf(wi, wo);
			vec4 tmp = vec4(transpose(tg) * wi, 0);
			vec3 wiWorld = normalize( (u_Transform.viewInv * tmp).xyz );
			vec3 Li = evalEnvmap(wiWorld);

			if (pdf2 > 0.0) {
				float pdf1;
				ggx_evalp(wi, wo, u_Alpha, pdf1);
				float misWeight = pdf2 * pdf2;
				float misNrm = pdf1 * pdf1 + pdf2 * pdf2;

				Lo+= Li * frp / pdf2 * misWeight / misNrm;
			}
		}
	}

	o_FragColor = vec4(Lo, u_SamplesPerPass);

	// -----------------------------------------------------------------------------
/**
 * Debug Shading
 *
 * Do whatever you like in here.
 */
#elif SHADE_DEBUG
	vec3 wi = normalize(tg * vec3(0, 0, 1));
	Lo = getBRDF(0, wi, wo, vec3(0, 0, 1)) * u_SamplesPerPass;

	vec3 wx2 = normalize((u_Transform.viewInv * i_Tangent1).xyz);
	vec3 wy2 = normalize((u_Transform.viewInv * i_Tangent2).xyz);
	wn = normalize(cross(wy2, wx2));

	Lo = evalEnvmap(wn) * u_SamplesPerPass;
	Lo = BRDF(wi, wo, vec3(0, 0, 1), vec3(1, 0, 0), vec3(0, 1, 0)) * u_SamplesPerPass;
	float dummy;
	Lo = vec3(u_SamplesPerPass) * ggx_evalp(wo, wo, 1.0, dummy);
	o_FragColor = vec4(clamp(wy2, 0.0, 1.0), u_SamplesPerPass);
	o_FragColor = vec4(Lo, u_SamplesPerPass);
// -----------------------------------------------------------------------------
#endif // SHADE

}
#endif // FRAGMENT_SHADER


