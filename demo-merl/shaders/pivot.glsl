#line 1
/* pivot.glsl - public domain GLSL library
by Jonathan Dupuy

	This file provides utility functions for the sphere light 
	shading technique described in my paper "A Spherical Cap Preserving 
	Parameterization for Spherical Distributions"..
*/

// Spherical Cap
struct cap {
	vec3 dir; // direction
	float z;  // cos of the aperture angle
};

// Sphere
struct sphere {
	vec3 pos; // center
	float r;  // radius
};

// Mappings
vec3 u2_to_cap(vec2 u, cap c);
vec3 u2_to_cos(vec2 u);
vec3 u2_to_s2(vec2 u);
vec3 u2_to_h2(vec2 u);
vec3 u2_to_ps2(vec2 u, vec3 r_p);
vec3 u2_to_ph2(vec2 u, vec3 r_p);
vec3 u2_to_pcap(vec2 u, cap c, vec3 r_p);
vec3 r3_to_pr3(vec3 r, vec3 r_p);
vec3 s2_to_ps2(vec3 r, vec3 r_p);
cap cap_to_pcap(cap c, vec3 r_p);

// PDFs
float pdf_cap(vec3 wk, cap c);
float pdf_cos(vec3 wk);
float pdf_s2(vec3 wk);
float pdf_h2(vec3 wk);
float pdf_ps2(vec3 wk, vec3 r_p);
float pdf_pcap(vec3 wk, cap c, vec3 r_p);
float pdf_pcap_fast(vec3 wk, cap c_std, vec3 r_p);

// solid angles
float cap_solidangle(cap c);
float cap_solidangle(cap c1, cap c2);

// Approximate BRDF shading
float GGXSphereLightingPivotApprox(sphere s, vec3 wo, vec3 pivot);

//
//
//// end header file ///////////////////////////////////////////////////////////


// Frisvad's method to build an orthonomal basis around a direction w
void basis(vec3 w, out vec3 t1, out vec3 t2)
{
	if (w.z < -0.9999999) {
		t1 = vec3( 0, -1, 0);
		t2 = vec3(-1,  0, 0);
	} else {
		const float a = 1.0 / (1.0 + w.z);
		const float b = -w.x * w.y * a;
		t1 = vec3(1.0 - w.x * w.x * a, b, -w.x);
		t2 = vec3(b, 1.0 - w.y * w.y * a, -w.y);
	}
}

#define TWOPI 6.283185307

float cap_solidangle(cap c)
{
	return TWOPI - TWOPI * c.z;
}

// Based on Oat and Sander's 2008 technique
float cap_solidangle(cap c1, cap c2)
{
	float r1 = acos(c1.z);
	float r2 = acos(c2.z);
	float rd = acos(dot(c1.dir, c2.dir));
	float fArea = 0.0;

	if (rd <= max(r1, r2) - min(r1, r2)) {
		// One cap in completely inside the other
		fArea = TWOPI - TWOPI * max(c1.z, c2.z);
	} else if (rd >= r1 + r2) {
		// No intersection exists
		fArea = 0;
	} else {
		float fDiff = abs(r1 - r2);
		float den = r1 + r2 - fDiff;
		float x = 1.0 - clamp((rd - fDiff) / den, 0.0, 1.0);
		fArea = smoothstep(0.0, 1.0, x);
		fArea*= TWOPI - TWOPI * max(c1.z, c2.z);
	}

	return fArea;
}


float GGXSphereLightingPivotApprox(sphere s, vec3 wo, vec3 pivot)
{
	// compute the spherical cap produced by the sphere
	float tmp = clamp(s.r * s.r / dot(s.pos, s.pos), 0.0, 1.0);
	cap c = cap(normalize(s.pos), sqrt(1.0 - tmp));

	// integrate
	cap c1 = cap_to_pcap(c, pivot);
	cap c2 = cap_to_pcap(cap(vec3(0, 0, 1), 0.0), pivot);
	float res = cap_solidangle(c1, c2) * /*1/4pi*/0.079577472;
	return clamp(res, 0.0, 1.0);
}

// -----------------------------------------------------------------------------
// sample warps

/* Sphere */
vec3 u2_to_s2(vec2 u)
{
	float z = 2.0 * u.x - 1.0; // in [-1, 1)
	float sin_theta = sqrt(1.0 - z * z);
	float phi = TWOPI * u.y; // in [0, 2pi)
	float x = sin_theta * cos(phi);
	float y = sin_theta * sin(phi);

	return vec3(x, y, z);
}

/* Hemisphere */
vec3 u2_to_h2(vec2 u)
{
	float z = u.x; // in [0, 1)
	float sin_theta = sqrt(1.0 - z * z);
	float phi = TWOPI * u.y; // in [0, 2pi)
	float x = sin_theta * cos(phi);
	float y = sin_theta * sin(phi);

	return vec3(x, y, z);
}

/* Spherical Cap */
vec3 u2_to_cap(vec2 u, cap c)
{
	// generate the sample in the basis aligned with the cap
	float z = (1.0 - c.z) * u.x + c.z; // in [cap_cos, 1)
	float sin_theta = sqrt(1.0 - z * z);
	float phi = TWOPI * u.y; // in [0, 2pi)
	float x = sin_theta * cos(phi);
	float y = sin_theta * sin(phi);

	// compute basis vectors
	vec3 t1, t2;
	basis(c.dir, t1, t2);
	mat3 xf = mat3(t1, t2, c.dir);

	// warp the sample in the proper basis
	return normalize(xf * vec3(x, y, z));
}

/* Disk */
vec2 u2_to_disk(vec2 u)
{
	float r = sqrt(u.x);           // in [0, 1)
	float phi = TWOPI * u.y; // in [0, 2pi)
	return r * vec2(cos(phi), sin(phi));
}

/* Clamped Cosine */
vec3 u2_to_cos(vec2 u)
{
	// project a disk sample back to the hemisphere
	vec2 d = u2_to_disk(u);
	float z = sqrt(1.0 - dot(d, d));
	return vec3(d, z);
}

/* Pivot 3D Transformation */
vec3 r3_to_pr3(vec3 r, vec3 r_p)
{
	vec3 tmp = r - r_p;
	vec3 cp1 = cross(r, r_p);
	vec3 cp2 = cross(tmp, cp1);
	float dp = dot(r, r_p) - 1.f;
	float qf = dp * dp + dot(cp1, cp1);

	return ((dp * tmp - cp2) / qf);
}
vec3 s2_to_ps2(vec3 wk, vec3 r_p)
{
	return r3_to_pr3(wk, r_p);
}

/* Pivot Transformed Sphere Sample */
vec3 u2_to_ps2(vec2 u, vec3 r_p)
{
	vec3 std = u2_to_s2(u);
	return s2_to_ps2(std, r_p);
}

/* Pivot Transformed Hemisphere Sample */
vec3 u2_to_ph2(vec2 u, vec3 r_p)
{
	vec3 std = u2_to_h2(u);
	return s2_to_ps2(std, r_p);
}

/* Pivot Transformed Cap Sample */
vec3 u2_to_pcap(vec2 u, cap c, vec3 r_p)
{
	vec3 std = u2_to_cap(u, c);
	return s2_to_ps2(std, r_p);
}

/* Pivot 2D Transformation */
vec2 r2_to_pr2(vec2 r, float r_p)
{
	vec2 tmp1 = vec2(r.x - r_p, r.y);
	vec2 tmp2 = r_p * r - vec2(1, 0);
	float x = dot(tmp1, tmp2);
	float y = tmp1.y * tmp2.x - tmp1.x * tmp2.y;
	float qf = dot(tmp2, tmp2);

	return (vec2(x, y) / qf);
}

/* Pivot Transformed Cap */
cap cap_to_pcap(cap c, vec3 r_p)
{
	// extract pivot length and direction
	float pivot_mag = length(r_p);
	// special case: the pivot is at the origin
	if (pivot_mag < 0.001)
		return cap(-c.dir, c.z);
	vec3 pivot_dir = r_p / pivot_mag;

	// 2D cap dir
	float cos_phi = dot(c.dir, pivot_dir);
	float sin_phi = sqrt(1.0 - cos_phi * cos_phi);

	// 2D basis = (pivotDir, PivotOrthogonalDirection)
	vec3 pivot_ortho_dir;
	if (abs(cos_phi) < 0.9999) {
		pivot_ortho_dir = (c.dir - cos_phi * pivot_dir) / sin_phi;
	} else {
		pivot_ortho_dir = vec3(0, 0, 0);
	}

	// compute cap 2D end points
	float cap_sin = sqrt(1.0 - c.z * c.z);
	float a1 = cos_phi * c.z;
	float a2 = sin_phi * cap_sin;
	float a3 = sin_phi * c.z;
	float a4 = cos_phi * cap_sin;
	vec2 dir1 = vec2(a1 + a2, a3 - a4);
	vec2 dir2 = vec2(a1 - a2, a3 + a4);

	// project in 2D
	vec2 dir1_xf = r2_to_pr2(dir1, pivot_mag);
	vec2 dir2_xf = r2_to_pr2(dir2, pivot_mag);

	// compute the cap 2D direction
	float area = dir1_xf.x * dir2_xf.y - dir1_xf.y * dir2_xf.x;
	float s = area > 0.0 ? 1.0 : -1.0;
	vec2 dir_xf = s * normalize(dir1_xf + dir2_xf);

	// compute the 3D cap parameters
	vec3 cap_dir = dir_xf.x * pivot_dir + dir_xf.y * pivot_ortho_dir;
	float cap_cos = dot(dir_xf, dir1_xf);

	return cap(cap_dir, cap_cos);
}


// -----------------------------------------------------------------------------
// PDFs
float pdf_cap(vec3 wk, cap c)
{
	// make sure the sample lies in the the cap
	if (dot(wk, c.dir) >= c.z) {
		return 1.0 / cap_solidangle(c);
	}
	return 0.0;
}

float pdf_cos(vec3 wk)
{
	return clamp(wk.z, 0.0, 1.0) * /* 1/pi */0.318309886;
}

float pdf_s2(vec3 wk)
{
	return /* 1/4pi */ 0.079577472;
}

float pdf_h2(vec3 wk)
{
	return wk.z > 0.0 ?/* 1/2pi */ 0.159154943 : 0.0;
}

float pivot_jacobian(vec3 wk, vec3 r_p)
{
	float num = 1.0 - dot(r_p, r_p);
	vec3 tmp = wk - r_p;
	float den = dot(tmp, tmp);

	return (num * num) / (den * den);
}

float pdf_ps2(vec3 wk, vec3 r_p)
{
	float std = pdf_s2(s2_to_ps2(wk, r_p));
	float J = pivot_jacobian(wk, r_p);
	return std * J;
}

float pdf_pcap_fast(vec3 wk, cap c_std, vec3 r_p)
{
	float std = pdf_cap(s2_to_ps2(wk, r_p), c_std);
	float J = pivot_jacobian(wk, r_p);
	return std * J;
}

float pdf_pcap(vec3 wk, cap c, vec3 r_p)
{
	return pdf_pcap_fast(wk, cap_to_pcap(c, r_p), r_p);
}

#undef TWOPI


