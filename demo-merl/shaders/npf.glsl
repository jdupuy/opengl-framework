uniform sampler2D u_NpfSampler;

const float M_PI = 3.14159265359;
const float M_PI_2 = 1.57079632679;

// decode the simple HDR format in the PNG
vec3 uberTextureLookup( vec2 uv ) {
	vec4 color = texture( u_NpfSampler, uv );
	return color.xyz;
}

bool isNan(float val) {
	return (val <= 0.0 || 0.0 <= val) ? false : true;
}

bool isInf(float val) {
	return (val >= 9999.99990 || val <= -9999.99990);
}

float dotProduct(vec3 dir1, vec3 dir2)
{
	float dot = dot(dir1, dir2);
	return clamp(dot, 1e-5, 1.0);
}

vec3 lookupInterpolatedG1(int n, float theta)
{
	float fb = theta / M_PI_2 * 90.0 ;
    // find bin centers
    float f0 = floor(fb-0.5) + 0.5 ;
    float f1 = f0 + 1.0 ;
    // find bin indexes t0 and t1
    float t0 = floor(f0) ;
    float t1 = floor(f1) ;
    // ignores the first bin
    t0 = max(1.0, t0);
    t1 = max(2.0, t1);
    // find theta at bin centers
    float theta0 = (t0+0.5) * M_PI_2 / 90.0;
    float theta1 = (t1+0.5) * M_PI_2 / 90.0;
    //find the weights
    float w0 = 1.0 - (fb - f0) ;
    float w1 = 1.0 - w0 ; // 1.0 - (f1 - fb)
    // sample
    vec3 G1 = vec3(0.0);
    vec2 uv;
    uv.y = float(n)/256.;
	if (t1 > 90.0-1.0)
    {
    	uv.x = (t0+2.+90.)/512.;
        G1 = uberTextureLookup(uv) * w0 ;
    }
    else
    {
    	uv.x = (t0+2.+90.)/512.;
    	vec3 g0 = uberTextureLookup(uv);
    	uv.x = (t1+2.+90.)/512.;
    	vec3 g1 = uberTextureLookup(uv);
        G1 = g0 * w0 + g1 * w1;
    }
    return G1;
}

// material index goes from 0 to 99
// each texture row contains: rhoD rhoS D[90] G1[90] F[90]
vec3 getBRDF(int n, vec3 dirIn, vec3 dirOut, vec3 dirNormal) {
	if (dirIn.z < 0 || dirOut.z < 0)
		return vec3(0);

	vec3 dirH = normalize(dirIn + dirOut);
	float thetaH = acos(dot(dirH, dirNormal));
	float thetaD = acos(dot(dirIn, dirH));
	float thetaI = acos(dot(dirIn, dirNormal));
	float thetaO = acos(dot(dirOut, dirNormal));

	// BRDF texture has a fixed size of 512x256
	vec2 uv;
	uv.y = float(n)/256.;

	uv.x = 0.0/512.;
	vec3 rhoD = uberTextureLookup(uv);

	uv.x = 1.0/512.;
	vec3 rhoS = uberTextureLookup(uv);

	int iD = int(clamp(sqrt(thetaH / M_PI_2
	             * 90.0 * 90.0), 0.0, 89.0));
	uv.x = float(iD+2)/512.;
	vec3 D = uberTextureLookup(uv);

	vec3 G1I = lookupInterpolatedG1(n, thetaI);

	vec3 G1O = lookupInterpolatedG1(n, thetaO);

	int iF = int(clamp(thetaD / M_PI_2 * 90.0, 0.0, 89.0));
	uv.x = float(iF+2+90+90)/512.;
	vec3 F = uberTextureLookup(uv);

	vec3 BRDF = (rhoD) + (rhoS) * D * F *
	            (G1I / cos(thetaI)) * (G1O / cos(thetaO));

	if(isInf(length(BRDF)) || isNan(length(BRDF)))
	{
		return vec3(0.0);
	}

	return BRDF;
}
