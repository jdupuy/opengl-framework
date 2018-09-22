/* dc_frustum - v1.0 - public domain frustum culling GLSL code
	(Created by Jonathan Dupuy 2014.11.13)

*/
#ifndef DCF_INCLUDE_DC_FRUSTUM_GLSL
#define DCF_INCLUDE_DC_FRUSTUM_GLSL
#line 7

//////////////////////////////////////////////////////////////////////////////
//
// Frustum Culling API
//

bool dj_culltest(mat4 mvp, vec3 bmin, vec3 bmax);

//
//
//// end header file /////////////////////////////////////////////////////
#endif // DCF_INCLUDE_DC_FRUSTUM_GLSL

// *************************************************************************************************
// Frustum Implementation

struct dc__frustum {
	vec4 planes[6];
};

/**
 * Extract Frustum Planes from MVP Matrix
 *
 * Based on "Fast Extraction of Viewing Frustum Planes from the World-
 * View-Projection Matrix", by Gil Gribb and Klaus Hartmann.
 * This procedure computes the planes of the frustum and normalizes 
 * them.
 */
void dcf__load(out dc__frustum frustum, mat4 mvp)
{
	for (int i = 0; i < 3; ++i)
	for (int j = 0; j < 2; ++j) {
		frustum.planes[i*2+j].x = mvp[0][3] + (j == 0 ? mvp[0][i] : -mvp[0][i]);
		frustum.planes[i*2+j].y = mvp[1][3] + (j == 0 ? mvp[1][i] : -mvp[1][i]);
		frustum.planes[i*2+j].z = mvp[2][3] + (j == 0 ? mvp[2][i] : -mvp[2][i]);
		frustum.planes[i*2+j].w = mvp[3][3] + (j == 0 ? mvp[3][i] : -mvp[3][i]);
		frustum.planes[i*2+j]*= length(frustum.planes[i*2+j].xyz);
	}
}

/**
 * Negative Vertex of an AABB
 *
 * This procedure computes the negative vertex of an AABB
 * given a normal.
 * See the View Frustum Culling tutorial @ LightHouse3D.com
 * http://www.lighthouse3d.com/tutorials/view-frustum-culling/geometric-approach-testing-boxes-ii/
 */
vec3 dcf__nvertex(vec3 bmin, vec3 bmax, vec3 n)
{
	bvec3 b = greaterThan(n, vec3(0));
	return mix(bmin, bmax, b);
}

/**
 * Frustum-AABB Culling Test
 *
 * This procedure returns true if the AABB is either inside, or in
 * intersection with the frustum, and false otherwise.
 * The test is based on the View Frustum Culling tutorial @ LightHouse3D.com
 * http://www.lighthouse3d.com/tutorials/view-frustum-culling/geometric-approach-testing-boxes-ii/
 */
bool dj_culltest(mat4 mvp, vec3 bmin, vec3 bmax)
{
	float a = 1.0;
	dc__frustum f;

	dcf__load(f, mvp);
	for (int i = 0; i < 6 && a >= 0.0; ++i) {
		vec3 n = dcf__nvertex(bmin, bmax, f.planes[i].xyz);

		a = dot(vec4(n, 1.0), f.planes[i]);
	}

	return (a >= 0.0);
}




