#line 1
/* ggx.glsl - public domain GLSL library
by Jonathan Dupuy

	This file provides utility functions for GGX BRDFs, which are used
	in the sphere light shading technique described in my paper
	"A Spherical Cap Preserving Parameterization for Spherical Distributions".
*/

// Evaluate GGX BRDF (brdf times cosine)
float ggx_evalp(vec3 wi, vec3 wo, float alpha, out float pdf);

// Importance sample a visible microfacet normal from direction wi
// Note: the algorithm I use is an improvement over Eric Heit'z
// algorithm. It relies on Shirley's concentric mapping and produces less
// distortion.
vec3 ggx_sample(vec2 u, vec3 wi, float alpha);

//
//
//// end header file ///////////////////////////////////////////////////////////

// *****************************************************************************
/**
 * GGX Functions
 *
 */

#define PI 3.141592654

// -----------------------------------------------------------------------------
// Evaluation
float ggx_evalp(vec3 wi, vec3 wo, float alpha, out float pdf)
{
	if (wo.z > 0.0 && wi.z > 0.0) {
		vec3 wh = normalize(wi + wo);
		vec3 wh_xform = vec3(wh.xy / alpha, wh.z);
		vec3 wi_xform = vec3(wi.xy * alpha, wi.z);
		vec3 wo_xform = vec3(wo.xy * alpha, wo.z);
		float wh_xform_mag = length(wh_xform);
		float wi_xform_mag = length(wi_xform);
		float wo_xform_mag = length(wo_xform);
		wh_xform/= wh_xform_mag; // normalize
		wi_xform/= wi_xform_mag; // normalize
		wo_xform/= wo_xform_mag; // normalize
		float sigma_i = 0.5 + 0.5 * wi_xform.z;
		float sigma_o = 0.5 + 0.5 * wo_xform.z;
		float Gi = clamp(wi.z, 0.0, 1.0) / (sigma_i * wi_xform_mag);
		float Go = clamp(wo.z, 0.0, 1.0) / (sigma_o * wo_xform_mag);
		float J = alpha * alpha * wh_xform_mag * wh_xform_mag * wh_xform_mag;
		float Dvis = clamp(dot(wo_xform, wh_xform), 0.0, 1.0) / (sigma_o * PI * J);
		float Gcond = Gi / (Gi + Go - Gi * Go);
		float cos_theta_d = dot(wh, wo);

		pdf = (Dvis / (cos_theta_d * 4.0));
		return pdf * Gcond;
	}
	pdf = 0.0;
	return 0.0;
}

// -----------------------------------------------------------------------------
// uniform to concentric disk
vec2 ggx__u2_to_d2(vec2 u)
{
	/* Concentric map code with less branching (by Dave Cline), see
	   http://psgraphics.blogspot.ch/2011/01/improved-code-for-concentric-map.html */
	float r1 = 2 * u.x - 1;
	float r2 = 2 * u.y - 1;
	float phi, r;

	if (r1 == 0 && r2 == 0) {
		r = phi = 0;
	} else if (r1 * r1 > r2 * r2) {
		r = r1;
		phi = (PI / 4) * (r2 / r1);
	} else {
		r = r2;
		phi = (PI / 2) - (r1 / r2) * (PI / 4);
	}

	return r * vec2(cos(phi), sin(phi));
}

// -----------------------------------------------------------------------------
// uniform to half a concentric disk
vec2 ggx__u2_to_hd2(vec2 u)
{
	vec2 v = vec2((1 + u.x) / 2, u.y);
	return ggx__u2_to_d2(v);
}

// -----------------------------------------------------------------------------
// uniform to microfacet normal projected onto concentric disk
vec2 ggx__u2_to_md2(vec2 u, float zi)
{
	float a = 1.0f / (1.0f + zi);

	if (u.x > a) {
		float xu = (u.x - a) / (1.0f - a); // remap to [0, 1]

		return vec2(zi, 1) * ggx__u2_to_hd2(vec2(xu, u.y));
	} else {
		float xu = (u.x - a) / a; // remap to [-1, 0]

		return ggx__u2_to_hd2(vec2(xu, u.y));
	}
}

// -----------------------------------------------------------------------------
// concentric disk to microfacet normal
vec3 ggx__d2_to_h2(vec2 d, float zi, float z_i)
{
	vec3 z = vec3(z_i, 0, zi);
	vec3 y = vec3(0, 1, 0);
	vec3 x = vec3(zi, 0, -z_i); // cross(z, y)
	float tmp = clamp(1 - dot(d, d), 0.0, 1.0);
	vec3 wm = x * d.x + y * d.y + z * sqrt(tmp);

	return vec3(wm.x, wm.y, clamp(wm.z, 0.0, 1.0));
}

// -----------------------------------------------------------------------------
vec3 ggx__u2_to_h2_std_radial(vec2 u, float zi, float z_i)
{
	return ggx__d2_to_h2(ggx__u2_to_md2(u, zi), zi, z_i);
}

// -----------------------------------------------------------------------------
// standard GGX variate exploiting rotational symmetry
vec3 ggx__u2_to_h2_std(vec2 u, vec3 wi)
{
	float zi = wi.z;
	float z_i = sqrt(wi.x * wi.x + wi.y * wi.y);
	vec3 wm = ggx__u2_to_h2_std_radial(u, zi, z_i);

	// rotate for non-normal incidence
	if (z_i > 0) {
		float nrm = 1 / z_i;
		float c = wi.x * nrm;
		float s = wi.y * nrm;
		float x = c * wm.x - s * wm.y;
		float y = s * wm.x + c * wm.y;

		wm = vec3(x, y, wm.z);
	}

	return wm;
}

// -----------------------------------------------------------------------------
// warp the domain to match the standard GGX distribution
// (note: works with anisotropic roughness)
vec3 ggx__u2_to_h2(vec2 u, vec3 wi, float r1, float r2)
{
	vec3 wi_std = normalize(vec3(r1, r2, 1) * wi);
	vec3 wm_std = ggx__u2_to_h2_std(u, wi_std);
	vec3 wm = normalize(vec3(r1, r2, 1) * wm_std);

	return wm;
}

// -----------------------------------------------------------------------------
// importance sample: map the unit square to the hemisphere
vec3 ggx_sample(vec2 u, vec3 wi, float alpha)
{
	return ggx__u2_to_h2(u, wi, alpha, alpha);
}

#undef PI

