/* dj_brdf.h - public domain BRDF toolkit
by Jonathan Dupuy

   Do this:
      #define DJ_BRDF_IMPLEMENTATION 1
   before you include this file in *one* C++ file to create the implementation.

   INTERFACING

   define DJB_ASSERT(x) to avoid using assert.h.
   define DJB_LOG(format, ...) to use your own logger (default prints to stdout)

   QUICK NOTE
       1. This is research code. I have done my best to make sure that my
       algorithms are robust and accurate, but some bugs may remain!

   ACKNOWLEDGEMENTS
       - Jiri Filip (provided code for djb::utia)
       - Joel Kronander (provided parameters for djb::abc)
       - Mahdi Bagher, Cyril Soler, Nicolas Holzschuch (provided parameters for djb::sgd)

*/

#ifndef DJB_INCLUDE_DJ_BRDF_H
#define DJB_INCLUDE_DJ_BRDF_H

#include <vector>
#include <string>
#include <memory>
#include <valarray>

namespace djb {

/* Floating point precision */
#if DJB_USE_DOUBLE_PRECISION
typedef double float_t;
#else
typedef float float_t;
#endif

// *****************************************************************************
/* Exception API */
struct exc : public std::exception {
	exc(const char *fmt, ...);
	virtual ~exc() throw() {}
	const char *what() const throw() {return m_str.c_str();}
	std::string m_str;
};

// *****************************************************************************
/* Standalone vec2 and vec3 utilities */
struct vec2 {
	explicit vec2(float_t x = 0): x(x), y(x) {}
	vec2(float_t x, float_t y) : x(x), y(y) {}
	float_t x, y;
};
struct vec3 {
	explicit vec3(float_t x = 0): x(x), y(x), z(x) {}
	vec3(float_t x, float_t y, float_t z) : x(x), y(y), z(z) {}
	float_t& operator[](int i) {return (&x)[i];}
	const float_t& operator[](int i) const {return (&x)[i];}
	float_t x, y, z;
};

// *****************************************************************************
/* Standalone mat3 utility */
struct mat3 {
	mat3(float_t m11, float_t m12, float_t m13,
	     float_t m21, float_t m22, float_t m23,
	     float_t m31, float_t m32, float_t m33);
	mat3(const vec3 &r1, const vec3 &r2, const vec3 &r3);
	explicit mat3(float_t diag = 1);
	vec3& operator[](int i) {return r[i];}
	const vec3& operator[](int i) const {return r[i];}
	private: vec3 r[3];
};

// *****************************************************************************
/* BRDF API */
class brdf {
public:
	// spectrum
	typedef std::valarray<float_t> value_type;

	// evaluate f_r * cos
	virtual value_type eval(const vec3 &wi, const vec3 &wo,
	                        const void *user_args = nullptr) const = 0;
	virtual value_type eval_hd(const vec3 &wh, const vec3 &wd,
	                           const void *user_args = nullptr) const;
	// zero (a BRDF *must* return a spectrum of proper size)
	virtual const value_type zero_value() const = 0;
	// batch evaluation f_r * cos
	typedef std::pair<vec3, vec3> io_pair;
	virtual std::vector<value_type> eval_batch(const std::vector<io_pair> &wiwo,
	                                           const void *user_args = nullptr) const;
	// importance sample f_r * cos from two uniform numbers
	virtual value_type sample(const vec2 &u,
	                          const vec3 &wi,
	                          vec3 *wo = nullptr,
	                          float_t *pdf = nullptr,
	                          const void *user_args = nullptr) const;
	// evaluate the PDF of a sample
	virtual float_t pdf(const vec3 &wi, const vec3 &wo,
	                    const void *user_args = nullptr) const;
	// mappings
	virtual vec3 u2_to_s2(const vec2 &u, const vec3 &wi,
	                      const void *user_args = nullptr) const;
	virtual vec2 s2_to_u2(const vec3 &wo, const vec3 &wi,
	                      const void *user_args = nullptr) const;
	// utilities
	static void io_to_hd(const vec3 &wi, const vec3 &wo, vec3 *wh, vec3 *wd);
	static void hd_to_io(const vec3 &wh, const vec3 &wd, vec3 *wi, vec3 *wo);
	static vec2 u2_to_d2(const vec2 &u);
	static vec2 d2_to_u2(const vec2 &d);
	// ctor / dtor
	brdf() {}
	virtual ~brdf() {}
};

// *****************************************************************************
/* Fresnel API */
namespace fresnel {
	/* Utilities */
	void ior_to_f0(float_t ior, float_t *f0);
	void ior_to_f0(const brdf::value_type &ior, brdf::value_type *f0);
	void f0_to_ior(float_t f0, float_t *ior);
	void f0_to_ior(const brdf::value_type &f0, brdf::value_type *ior);

	/* Interface */
	class ptr;
	class impl {
		friend class ptr;
		virtual impl *clone() const = 0;
	public:
		virtual ~impl() {}
		virtual const brdf::value_type zero_value() const = 0;
		virtual brdf::value_type eval(float_t zd) const = 0;
	};

	/* Container (performs automatic deep copies) */
	class ptr {
		const impl *m_f;
	public:
		~ptr() {delete m_f;}
		ptr(): m_f(nullptr) {}
		explicit ptr(const impl &f): m_f(f.clone()) {}
		ptr(const ptr &p): m_f(p.m_f->clone()) {}
		ptr& operator=(const ptr &p) {
			delete m_f; m_f = p.m_f->clone(); return *this;
		}
		const impl* operator->() const {return m_f;}
		const impl& operator*() const {return *m_f;}
	};

	/* Ideal Specular Reflection */
	template <int N>
	class ideal : public impl {
		impl *clone() const {return new ideal();}
	public:
		brdf::value_type eval(float_t) const {
			return brdf::value_type(float_t(1), N);
		}
		const brdf::value_type zero_value() const {
			return brdf::value_type(float_t(0), N);
		}
	};

	/* Fresnel for Unpolarized Light */
	class unpolarized : public impl {
		brdf::value_type ior; // index of refraction
		impl *clone() const {return new unpolarized(*this);}
	public:
		unpolarized(const brdf::value_type &ior): ior(ior) {}
		brdf::value_type eval(float_t zd) const;
		const brdf::value_type zero_value() const {
			return brdf::value_type(float_t(0), ior.size());
		}
	};

	/* Schlick's Fresnel */
	class schlick : public impl {
		brdf::value_type f0; // backscattering fresnel
		impl *clone() const {return new schlick(*this);}
	public:
		schlick(const brdf::value_type &f0);
		brdf::value_type eval(float_t zd) const;
		const brdf::value_type zero_value() const {
			return brdf::value_type(float_t(0), f0.size());
		}
	};

	/* SGD's Fresnel */
	class sgd : public impl {
		brdf::value_type f0, f1;
		impl *clone() const {return new sgd(*this);}
	public:
		sgd(const vec3 &f0, const vec3 &f1);
		brdf::value_type eval(float_t zd) const;
		const brdf::value_type zero_value() const {
			return brdf::value_type(float_t(0), 3);
		}
	};
} // namespace fresnel

// *****************************************************************************
/* Microfacet API */
class microfacet : public brdf {
	fresnel::ptr m_fresnel;
public:
	/* microfacet parameters */
	struct args {
		mat3 mtra, minv; // xform matrices
		float_t detm; // precomputed matrix determinant
		// Ctor (prefer factories for construction)
		args(float_t ax = 1, float_t ay = 1, float_t cor = 0,
		     float_t txn = 0, float_t tyn = 0);
		// Factories
		static args standard();
		static args isotropic(float_t a);
		static args elliptic(float_t a1, float_t a2, float_t phi_a = 0);
		static args normalmap(const vec3 &n, float_t a);
		static args normalmap(const vec3 &n,
		                      float_t a1, float_t a2, float_t phi_a = 0);
		static args p22args(float_t ax, float_t ay, float_t cor,
		                    float_t txn, float_t tyn);
	};
	// Ctor / Dtor
	microfacet(const fresnel::impl &f = fresnel::ideal<1>());
	virtual ~microfacet() {}
	// microfacet eval interface
	virtual float_t sigma_std(const vec3 &wi) const = 0;
	virtual float_t ndf_std(const vec3 &wm) const = 0;
	float_t vndf_std(const vec3 &wm, const vec3 &wi) const;
	float_t sigma(const vec3 &wi,
	              const args &args = args::standard()) const;
	float_t ndf(const vec3 &wm,
	              const args &args = args::standard()) const;
	float_t vndf(const vec3 &wm, const vec3 &wi,
	             const args &args = args::standard()) const;
	float_t g1(const vec3 &wm, const vec3 &wi,
	           const args &args = args::standard()) const;
	float_t g2(const vec3 &wm, const vec3 &wi, const vec3 &wo,
	           const args &args = args::standard()) const;
	float_t gcd(const vec3 &wm, const vec3 &wo, const vec3 &wi,
	             const args &args = args::standard()) const;
	// microfacet sample interface
	virtual vec3 u2_to_h2_std(const vec2 &u, const vec3 &wi) const = 0;
	virtual vec2 h2_to_u2_std(const vec3 &wm, const vec3 &wi) const = 0;
	vec3 u2_to_h2(const vec2 &u, const vec3 &wi,
	              const args &user_args = args::standard()) const;
	vec2 h2_to_u2(const vec3 &wm, const vec3 &wi,
	              const args &user_args = args::standard()) const;
	vec3 h2_to_s2(const vec3 &wm, const vec3 &wi) const;
	vec3 s2_to_h2(const vec3 &wo, const vec3 &wi) const;
	// brdf zero value
	const brdf::value_type zero_value() const {
		return m_fresnel->zero_value();
	}
	// brdf eval interface
	brdf::value_type eval(const vec3 &wi, const vec3 &wo,
	                      const void *user_args = nullptr) const;
	float_t pdf(const vec3 &wi, const vec3 &wo,
	            const void *user_args = nullptr) const;
	// brdf sample interface
	brdf::value_type sample(const vec2 &u,
	                        const vec3 &wi,
	                        vec3 *wo = nullptr,
	                        float_t *pdf = nullptr,
	                        const void *user_args = nullptr) const;
	// brdf mapping interface
	vec3 u2_to_s2(const vec2 &u, const vec3 &wi,
	              const void *user_args = nullptr) const;
	vec2 s2_to_u2(const vec3 &wo, const vec3 &wi,
	              const void *user_args = nullptr) const;
	// mutators
	void set_fresnel(const fresnel::impl &f) {m_fresnel = fresnel::ptr(f);}
	// accessors
	const fresnel::impl& get_fresnel() const {return *m_fresnel;}
};

/* Radial microfacet NDF */
class radial : public microfacet {
public:
	// Ctor / Dtor
	radial(const fresnel::impl &f = fresnel::ideal<1>()):
		microfacet(f) {}
	virtual ~radial() {}
	// radial eval interface
	virtual float_t ndf_std_radial(float_t zm) const = 0;
	virtual float_t sigma_std_radial(float_t zi) const = 0;
	// radial sample interface
	virtual vec3 u2_to_h2_std_radial(const vec2 &u,
	                                 float_t zi,
	                                 float_t z_i) const = 0;
	virtual vec2 h2_to_u2_std_radial(const vec3 &wm,
	                                 float_t zi,
	                                 float_t z_i) const = 0;
	// microfacet eval interface
	float_t ndf_std(const vec3 &wm) const;
	float_t sigma_std(const vec3 &wi) const;
	// microfacet mapping interface
	vec3 u2_to_h2_std(const vec2 &u, const vec3 &wi) const;
	vec2 h2_to_u2_std(const vec3 &wm, const vec3 &wi) const;
};

/* GGX microfacet NDF */
class ggx : public radial {
public:
	// Ctor
	ggx(const fresnel::impl &f = fresnel::ideal<1>()):
		radial(f) {}
	// radial eval interface
	float_t ndf_std_radial(float_t zm) const;
	float_t sigma_std_radial(float_t zi) const;
	// radial sample interface
	vec3 u2_to_h2_std_radial(const vec2 &u, float_t zi, float_t z_i) const;
	vec2 h2_to_u2_std_radial(const vec3 &wm, float_t zi, float_t z_i) const;
	// mappings (used internally for radial mappings)
	static vec2 u2_to_hd2(const vec2 &u);
	static vec2 hd2_to_u2(const vec2 &d);
	static vec2 u2_to_md2(const vec2 &u, float_t zi);
	static vec2 md2_to_u2(const vec2 &d, float_t zi);
	static vec3 d2_to_h2(const vec2 &d, float_t zi, float_t z_i);
	static vec2 h2_to_d2(const vec3 &h, float_t zi, float_t z_i);
};

/* Beckmann microfacet NDF */
class beckmann : public radial {
public:
	// Ctor
	beckmann(const fresnel::impl &f = fresnel::ideal<1>()):
		radial(f) {}
	// radial eval interface
	float_t ndf_std_radial(float_t zm) const;
	float_t sigma_std_radial(float_t zi) const;
	// radial sample interface
	vec3 u2_to_h2_std_radial(const vec2 &u, float_t zi, float_t z_i) const;
	vec2 h2_to_u2_std_radial(const vec3 &wm, float_t zi, float_t z_i) const;
	// mapppings (used internally for radial mappings)
	float_t cdf2(float_t ty, float_t zi, float_t z_i) const;
	float_t qf2(float_t u, float_t zi, float_t z_i) const;
	float_t cdf3(float_t tx) const;
	float_t qf3(float_t u) const;
	static float_t erfinv(float_t x);
	static vec2 h2_to_r2(const vec3 &wm);
	static vec3 r2_to_h2(const vec2 &twm);
};

/* Tabular microfacet radial NDF */
class tab_r : public radial {
	std::vector<float_t> m_ndf; // tabulated NDF
	std::vector<vec2> m_cdf;    // tabulated VNDF CDF (for sigma and sampling)
public:
	// Ctor
	tab_r(const std::vector<float_t> &ndf = std::vector<float_t>(64, 1));
	explicit tab_r(const brdf &fr, int resolution = 64);
	// radial eval interface
	float_t ndf_std_radial(float_t zm) const;
	float_t sigma_std_radial(float_t zi) const;
	// radial sample interface
	vec3 u2_to_h2_std_radial(const vec2 &u, float_t zi, float_t z_i) const;
	vec2 h2_to_u2_std_radial(const vec3 &wm, float_t zi, float_t z_i) const;
	// utilities
	static microfacet::args extract_beckmann_args(const tab_r &tab);
	static microfacet::args extract_ggx_args(const tab_r &tab);
	// specific queries
	float_t cdf(const vec2 &u, float_t zi) const;
	float_t cdf1(float_t u1, float_t zi) const;
	float_t cdf2(float_t u2, float_t u1, float_t zi) const;
	float_t qf1(float_t u, float_t zi) const;
	float_t qf2(float_t u, float_t qf1, float_t zi) const;
	// accessors
	const std::vector<float_t>& get_ndfv() const {return m_ndf;}
private:
	// internal routines
	void configure();
	void extract_ndf(const brdf &brdf, int resolution);
	void normalize_ndf();
	void compute_cdf();
	vec2 cdfv(const vec2 &u, float_t zi) const;
	// mappings
	static vec2 h2_to_u2(const vec3 &wm);
	static vec3 u2_to_h2(const vec2 &u);
};

/* Anisotropic tabulated microfacet NDF */
class tab : public microfacet {
	std::vector<float_t> m_ndf; // tabulated NDF
	std::vector<vec2> m_cdf; // tabulated VNDF CDF (for sigma and sampling)
	int m_zres, m_pres; // resolution: elevation and polar
public:
	// Ctor
	tab(const std::vector<float_t> &ndf = std::vector<float_t>(64 * 64, 1),
	    int zres = 64, int pres = 64);
	explicit tab(const brdf &fr, int zres = 64, int pres = 64);
	// microfacet eval interface
	float_t ndf_std(const vec3 &wm) const;
	float_t sigma_std(const vec3 &wi) const;
	// microfacet sample interface
	vec3 u2_to_h2_std(const vec2 &u, const vec3 &wi) const;
	vec2 h2_to_u2_std(const vec3 &wm, const vec3 &wi) const;
	// utilities
	static microfacet::args extract_ggx_args(const tab &tab);
	// specific queries
	float_t cdf(const vec2 &u, const vec3 &wi) const;
	float_t cdf1(float_t u1, const vec3 &wi) const;
	float_t cdf2(float_t u2, float_t u1, const vec3 &wi) const;
	float_t qf1(float_t u, const vec3 &wi) const;
	float_t qf2(float_t u, float_t qf1, const vec3 &wi) const;
	// accessors
	const std::vector<float_t>& get_ndfv(int *zres, int *pres) const;
private:
	// eval extraction
	void configure();
	void extract_ndf(const brdf &brdf);
	void normalize_ndf();
	vec2 cdfv(const vec2 &u, const vec3 &wi) const;
	// sample extraction
	void compute_cdf();
};

// *****************************************************************************
/* RGB BRDF Helper */
class brdf_rgb : public brdf {
public:
	brdf_rgb() {}
	virtual ~brdf_rgb() {}
	const brdf::value_type zero_value() const {
		return brdf::value_type(float_t(0), 3);
	}
};

// *****************************************************************************
/* MERL BRDF */
class merl : public brdf_rgb {
	std::vector<double> m_samples;
public:
	explicit merl(const char *path_to_file);
	brdf::value_type eval(const vec3 &wi, const vec3 &wo,
	                      const void *user_param = NULL) const;
	const std::vector<double>& get_samples() const {return m_samples;}
};

// *****************************************************************************
/* UTIA BRDF */
class utia : public brdf_rgb {
	std::vector<double> m_samples;
	double m_norm;
public:
	explicit utia(const char *filename);
	brdf::value_type eval(const vec3 &wi, const vec3 &wo,
	                      const void *user_param = NULL) const;
	const std::vector<double>& get_samples() const {return m_samples;}
private:
	void normalize();
};

// *****************************************************************************
/* SGD BRDF */
class sgd : public brdf_rgb {
	struct data {
		const char *name;
		const char *otherName;
		double rhoD[3];
		double rhoS[3];
		double alpha[3];
		double p[3];
		double f0[3];
		double f1[3];
		double kap[3];
		double lambda[3];
		double c[3];
		double k[3];
		double theta0[3];
		double error[3];
	};
	static const data s_data[100];
	fresnel::ptr m_fresnel;
	const data *m_data;
public:
	explicit sgd(const char *name);
	brdf::value_type eval(const vec3 &wi, const vec3 &wo,
	                      const void *user_param = NULL) const;
	brdf::value_type ndf(const vec3 &wh) const;
	brdf::value_type gaf(const vec3 &wh, const vec3 &wi, const vec3 &wo) const;
	brdf::value_type g1(const vec3 &wi) const;
};

// *****************************************************************************
/* ABC BRDF */
class abc : public brdf_rgb {
	struct data {
		const char *name;
		double kD[3];
		double A[3];
		double B;
		double C;
		double ior;
	};
	static const data s_data[100];
	fresnel::ptr m_fresnel;
	const data *m_data;
public:
	explicit abc(const char *name);
	brdf::value_type eval(const vec3 &wi, const vec3 &wo,
	                      const void *user_param = NULL) const;
	brdf::value_type ndf(const vec3 &h) const;
	float_t gaf(const vec3 &h, const vec3 &i, const vec3 &o) const;
};

// *****************************************************************************
/* Non-Parametric Factor Microfacet BRDF */
class npf : public brdf_rgb {
	static const char *s_list[100];
	std::vector<vec3> m_uber_texture;
	int m_id;
	mutable bool m_first;
	vec3 uberTextureLookup(const vec2 &uv) const;
	vec3 uberTextureLookupInt(int x) const;
	vec3 lookupInterpolatedG1(int n, float_t theta) const;
public:
	explicit npf(const char *uber_texture, const char *name);
	brdf::value_type eval(const vec3 &wi, const vec3 &wo,
	                      const void *user_param = NULL) const;
};

} // namespace djb

//
//
//// end header file /////////////////////////////////////////////////////
#endif // DJB_INCLUDE_DJ_BRDF_H

//#if 1
#ifdef DJ_BRDF_IMPLEMENTATION
#include <cmath>
#include <cstdarg>
#include <iostream>     // std::ios, std::istream, std::cout
#include <fstream>      // std::filebuf
#include <cstring>      // memcpy
#include <cstdint>      // uint32_t
#include <limits>       // inf

#ifndef DJB_ASSERT
#	include <cassert>
#	define DJB_ASSERT(x) assert(x)
#endif

#ifndef DJB_LOG
#	include <cstdio>
#	define DJB_LOG(format, ...) fprintf(stdout, format, ##__VA_ARGS__)
#endif

#ifdef _MSC_VER
#	pragma warning(disable: 4244) // possible loss of data
#endif

namespace djb {

// *****************************************************************************
// std math
using std::sqrt;
using std::cos;
using std::sin;
using std::tan;
using std::atan2;
using std::acos;
using std::modf;
using std::exp;
using std::log;
using std::pow;
using std::fabs;
using std::erf;
using std::floor;
using std::ceil;

float_t m_pi() {return float_t(3.1415926535897932384626433832795);}

// *****************************************************************************
// utility API
template<typename T> static T min(const T &a, const T &b) {return a < b ? a : b;}
template<typename T> static T max(const T &a, const T &b) {return a > b ? a : b;}
template<typename T> static T clamp(const T &x, const T &a, const T &b) {return min(b, max(a, x));}
template<typename T> static T sat(const T &x) {return clamp(x, T(0), T(1));}
template<typename T> static T sqr(const T &x) {return x * x;}
template<typename T> static int sgn(T x) {return (T(0) < x) - (x < T(0));}
template<typename T>
static T max3(const T &x, const T &y, const T &z)
{
	T m = x;

	(m < y) && (m = y);
	(m < z) && (m = z);

	return m;
}

template<typename T>
static T inversesqrt(const T &x)
{
	DJB_ASSERT(x > T(0));
	return (T(1) / sqrt(x));
}

exc::exc(const char *fmt, ...)
{
	char buf[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, 256, fmt, args);
	va_end(args);

	m_str = std::string(buf);
}

// *****************************************************************************
// Vector API

#define OP operator
#define V3 vec3
V3 OP*(float_t a, const V3 &b) {return V3(a * b.x, a * b.y, a * b.z);}
V3 OP*(const V3 &a, float_t b) {return V3(b * a.x, b * a.y, b * a.z);}
V3 OP/(const V3 &a, float_t b) {return (1 / b) * a;}
V3 OP*(const V3 &a, const V3 &b) {return V3(a.x * b.x, a.y * b.y, a.z * b.z);}
V3 OP/(const V3 &a, const V3 &b) {return V3(a.x / b.x, a.y / b.y, a.z / b.z);}
V3 OP+(const V3 &a, const V3 &b) {return V3(a.x + b.x, a.y + b.y, a.z + b.z);}
V3 OP-(const V3 &a, const V3 &b) {return V3(a.x - b.x, a.y - b.y, a.z - b.z);}
V3& OP+=(V3 &a, const V3 &b) {a.x+= b.x; a.y+= b.y; a.z+= b.z; return a;}
V3& OP*=(V3 &a, const V3 &b) {a.x*= b.x; a.y*= b.y; a.z*= b.z; return a;}
V3& OP*=(V3 &a, float_t b) {a.x*= b; a.y*= b; a.z*= b; return a;}
#undef V3

#define V2 vec2
V2 OP*(float_t a, const V2 &b) {return V2(a * b.x, a * b.y);}
V2 OP*(const V2 &a, float_t b) {return V2(b * a.x, b * a.y);}
V2 OP/(const V2 &a, float_t b) {return (1 / b) * a;}
V2 OP*(const V2 &a, const V2 &b) {return V2(a.x * b.x, a.y * b.y);}
V2 OP+(const V2 &a, const V2 &b) {return V2(a.x + b.x, a.y + b.y);}
V2 OP-(const V2 &a, const V2 &b) {return V2(a.x - b.x, a.y - b.y);}
V2& OP+=(V2 &a, const V2 &b) {a.x+= b.x; a.y+= b.y; return a;}
V2& OP*=(V2 &a, const V2 &b) {a.x*= b.x; a.y*= b.y; return a;}
V2& OP*=(V2 &a, float_t b) {a.x*= b; a.y*= b; return a;}
#undef V2
#undef OP

static float_t dot(const vec2 &a, const vec2 &b)
{
	return (a.x * b.x + a.y * b.y);
}

static float_t dot(const vec3 &a, const vec3 &b)
{
	return (a.x * b.x + a.y * b.y + a.z * b.z);
}

static vec3 cross(const vec3 &a, const vec3 &b)
{
	return vec3(a.y * b.z - a.z * b.y,
	            a.z * b.x - a.x * b.z,
	            a.x * b.y - a.y * b.x);
}

static vec3 normalize(const vec3 &v)
{
	float_t mag_sqr = dot(v, v);
	return (inversesqrt(mag_sqr) * v);
}

// *****************************************************************************
// Matrix 3x3 API
mat3::mat3(
	float_t m11, float_t m12, float_t m13,
	float_t m21, float_t m22, float_t m23,
	float_t m31, float_t m32, float_t m33
) {
	r[0] = vec3(m11, m12, m13);
	r[1] = vec3(m21, m22, m23);
	r[2] = vec3(m31, m32, m33);
}

mat3::mat3(const vec3 &r1, const vec3 &r2, const vec3 &r3)
{
	r[0] = r1;
	r[1] = r2;
	r[2] = r3;
}

mat3::mat3(float_t diag)
{
	r[0] = vec3(diag, 0, 0);
	r[1] = vec3(0, diag, 0);
	r[2] = vec3(0, 0, diag);
}

float_t det(const mat3 &m)
{
	const float_t d1 = m[1][1] * m[2][2] - m[2][1] * m[1][2];
	const float_t d2 = m[2][1] * m[0][2] - m[0][1] * m[2][2];
	const float_t d3 = m[0][1] * m[1][2] - m[1][1] * m[0][2];

	return m[0][0] * d1 - m[1][0] * d2 + m[2][0] * d3;
}

mat3 transpose(const mat3 &m)
{
	vec3 r1 = vec3(m[0][0], m[1][0], m[2][0]);
	vec3 r2 = vec3(m[0][1], m[1][1], m[2][1]);
	vec3 r3 = vec3(m[0][2], m[1][2], m[2][2]);

	return mat3(r1, r2, r3);
}

vec3 operator*(const mat3 &m, const vec3 &r)
{
	return vec3(dot(m[0], r), dot(m[1], r), dot(m[2], r));
}

//---------------------------------------------------------------------------
// rotate vector along one axis (Rodrguez formula)

// TODO: remove
static void xyz_to_theta_phi(const vec3 &wi, float_t *theta, float_t *phi)
{
	if (wi.z > (float_t)0.99999) {
		(*theta) = (*phi) = 0.0;
	} else if (wi.z < (float_t)-0.99999) {
		(*theta) = m_pi();
		(*phi) = 0.0;
	} else {
		(*theta) = acos(wi.z);
		(*phi) = atan2(wi.y, wi.x);
	}
}

static vec3 rotate_vector(const vec3 &r, const vec3 &axis, float_t rad)
{
#if 1
	float_t cos_angle = cos(rad);
	float_t sin_angle = sin(rad);
	vec3 out = cos_angle * r;
	float_t tmp1 = dot(axis, r);
	float_t tmp2 = tmp1 * (1 - cos_angle);
	out+= axis * tmp2;
	out+= sin_angle * cross(axis, r);

	return out;
#else
	float_t c = cos(rad), s = sin(rad);
	vec3 r1 = axis * dot(r, axis);
	vec3 r2 = r - r1;
	vec3 r3 = cross(r2, axis);

	return c * r2 - s * r3 + r1;
#endif
}


// *****************************************************************************
// BRDF API

// -----------------------------------------------------------------------------
// mappings
void brdf::io_to_hd(const vec3 &wi, const vec3 &wo, vec3 *wh, vec3 *wd)
{
	const vec3 y = vec3(0, 1, 0);
	const vec3 z = vec3(0, 0, 1);
	float_t theta_h, phi_h;

	(*wh) = normalize(wi + wo);
	xyz_to_theta_phi(*wh, &theta_h, &phi_h);
	vec3 tmp = rotate_vector(wi, z, -phi_h);
	(*wd) = normalize(rotate_vector(tmp, y, -theta_h));
}

void brdf::hd_to_io(const vec3 &wh, const vec3 &wd, vec3 *wi, vec3 *wo)
{
	const vec3 y = vec3(0, 1, 0);
	const vec3 z = vec3(0, 0, 1);
	float_t theta_h, phi_h;

	xyz_to_theta_phi(wh, &theta_h, &phi_h);
	vec3 tmp = rotate_vector(wd, y, theta_h);
	(*wi) = normalize(rotate_vector(tmp, z, phi_h));
	(*wo) = normalize(2 * dot((*wi), wh) * wh - (*wi));
}

vec2 brdf::u2_to_d2(const vec2 &u)
{
	/* Concentric map code with less branching (by Dave Cline), see
	   http://psgraphics.blogspot.ch/2011/01/improved-code-for-concentric-map.html */
	float_t r1 = 2 * u.x - 1;
	float_t r2 = 2 * u.y - 1;
	float_t phi, r;

	if (r1 == 0 && r2 == 0) {
		r = phi = 0;
	} else if (r1 * r1 > r2 * r2) {
		r = r1;
		phi = (m_pi() / 4) * (r2 / r1);
	} else {
		r = r2;
		phi = (m_pi() / 2) - (r1 / r2) * (m_pi() / 4);
	}

	return r * vec2(cos(phi), sin(phi));
}

vec2 brdf::d2_to_u2(const vec2 &d)
{
	float_t r = sqrt(d.x * d.x + d.y * d.y);
	float_t phi = atan2(d.y, d.x);
	float_t a, b;

	if (phi < -m_pi() / 4) {
		phi += 2 * m_pi();
	}

	if (phi < m_pi() / 4) {
		a = r;
		b = phi * a / (m_pi() / 4);
	} else if (phi < 3 * m_pi() / 4) {
		b = r;
		a = -(phi - m_pi() / 2) * b / (m_pi() / 4);
	} else if (phi < 5 * m_pi() / 4) {
		a = -r;
		b = (phi - m_pi()) * a / (m_pi() / 4);
	} else {
		b = -r;
		a = -(phi - 3 * m_pi() / 2) * b / (m_pi() / 4);
	}

	return (vec2(a, b) + vec2(1)) / 2;
}

vec3
brdf::u2_to_s2(const vec2 &u, const vec3 &, const void *) const
{
	vec2 d = u2_to_d2(u);
	return vec3(d.x, d.y, sqrt(1 - dot(d, d)));
}

vec2
brdf::s2_to_u2(const vec3 &wo, const vec3 &, const void *) const
{
	DJB_ASSERT(wo.z >= 0 && "invalid outgoing direction");
	vec2 d = vec2(wo.x, wo.y);
	return d2_to_u2(d);
}

// -----------------------------------------------------------------------------
// eval interface
brdf::value_type
brdf::eval_hd(const vec3 &wh, const vec3 &wd, const void *user_args) const
{
	vec3 wi, wo;
	hd_to_io(wh, wd, &wi, &wo);

	return eval(wi, wo, user_args);
}

std::vector<brdf::value_type>
brdf::eval_batch(
	const std::vector<brdf::io_pair> &io,
	const void *user_args
) const {
	std::vector<brdf::value_type> fr(io.size());

	for (int i = 0; i < (int)io.size(); ++i) {
		fr[i] = eval(io[i].first, io[i].second, user_args);
	}

	return fr;
}

float_t brdf::pdf(const vec3 &wi, const vec3 &, const void *) const
{
	if (wi.z > 0)
		return float_t(wi.z / m_pi());
	return 0;
}

// -----------------------------------------------------------------------------
// sample interface
brdf::value_type
brdf::sample(
	const vec2 &u,
	const vec3 &wi,
	vec3 *wo_out,
	float_t *pdf_out,
	const void *user_args
) const {
	if (wi.z > 0) {
		vec3 wo = u2_to_s2(u, wi);
		brdf::value_type fr = eval(wi, wo, user_args);
		float_t pdf = this->pdf(wi, wo, user_args);

		if (wo_out) (*wo_out) = wo;
		if (pdf_out) (*pdf_out) = pdf;

		return pdf > 0 ? fr / pdf : zero_value();
	} else {
		if (pdf_out) (*pdf_out) = 0;

		return zero_value();
	}
}

// *****************************************************************************
// Private Spline API
namespace spline {

int iwrap_repeat(int i, int edge)
{
	while (i < 0    ) {i+= edge;};
	while (i >= edge) {i-= edge;};

	return i;
}

int iwrap_edge(int i, int edge)
{
	--edge;
	if      (i > edge) i = edge;
	else if (i < 0   ) i = 0;

	return i;
}

void uwrap_repeat(float_t u, int s, int *x, int *y, float_t *w)
{
	float_t intpart; float_t frac = modf(u * s, &intpart);
	(*x) = iwrap_repeat((int)intpart            , s);
	(*y) = iwrap_repeat((int)intpart + sgn(frac), s);
	(*w) = frac * sgn(frac);
}

void uwrap_edge(float_t u, int s, int *x, int *y, float_t *w)
{
	float_t intpart; float_t frac = modf(u * (s - 1), &intpart);
	(*x) = iwrap_edge((int)intpart            , s);
	(*y) = iwrap_edge((int)intpart + sgn(frac), s);
	(*w) = frac * sgn(frac);
}

typedef void (*uwrap_callback)(float_t, int, int *, int *, float_t *);

template <typename T>
T lerp(const T &x1, const T &x2, float_t u)
{
	return x1 + u * (x2 - x1);
}

template <typename T>
static T
eval(const std::vector<T> &points, int s, uwrap_callback uwrap_cb, float_t u)
{
	int i1, i2; float_t w; (*uwrap_cb)(u, s, &i1, &i2, &w);
	const T &p1 = points[i1];
	const T &p2 = points[i2];

	return lerp(p1, p2, w);
}

template <typename T>
static T
eval2d(
	const std::vector<T> &points,
	int s1, int s2,
	uwrap_callback uwrap_cb1, float_t u1,
	uwrap_callback uwrap_cb2, float_t u2
) {
	// compute weights and indices
	int i1, i2; float_t w1; (*uwrap_cb1)(u1, s1, &i1, &i2, &w1);
	int j1, j2; float_t w2; (*uwrap_cb2)(u2, s2, &j1, &j2, &w2);

	// fetches
	const T &p1 = points[i1 + s1 * j1];
	const T &p2 = points[i2 + s1 * j1];
	const T &p3 = points[i1 + s1 * j2];
	const T &p4 = points[i2 + s1 * j2];

	// return bilinear interpolation
	const T tmp1 = lerp(p1, p2, w1);
	const T tmp2 = lerp(p3, p4, w1);
	return lerp(tmp1, tmp2, w2);
}

template <typename T>
static T
eval3d(
	const std::vector<T> &points,
	int s1, int s2, int s3,
	uwrap_callback uwrap_cb1, float_t u1,
	uwrap_callback uwrap_cb2, float_t u2,
	uwrap_callback uwrap_cb3, float_t u3
) {
	// compute weights and indices
	int i1, i2; float_t w1; (*uwrap_cb1)(u1, s1, &i1, &i2, &w1);
	int j1, j2; float_t w2; (*uwrap_cb2)(u2, s2, &j1, &j2, &w2);
	int k1, k2; float_t w3; (*uwrap_cb3)(u3, s3, &k1, &k2, &w3);

	// fetches
	const T &p1 = points[i1 + s1 * (j1 + s2 * k1)];
	const T &p2 = points[i2 + s1 * (j1 + s2 * k1)];
	const T &p3 = points[i1 + s1 * (j2 + s2 * k1)];
	const T &p4 = points[i2 + s1 * (j2 + s2 * k1)];
	const T &p5 = points[i1 + s1 * (j1 + s2 * k2)];
	const T &p6 = points[i2 + s1 * (j1 + s2 * k2)];
	const T &p7 = points[i1 + s1 * (j2 + s2 * k2)];
	const T &p8 = points[i2 + s1 * (j2 + s2 * k2)];

	// compute linear interpolation
	const T tmp1 = lerp(p1, p2, w1);
	const T tmp2 = lerp(p3, p4, w1);
	const T tmp3 = lerp(p5, p6, w1);
	const T tmp4 = lerp(p7, p8, w1);

	// compute bilinear interpolation
	const T tmp5 = lerp(tmp1, tmp2, w2);
	const T tmp6 = lerp(tmp3, tmp4, w2);

	// return trilinear interpolation
	return lerp(tmp5, tmp6, w3);
}

template <typename T>
static T
eval4d(
	const std::vector<T> &points,
	int s1, int s2, int s3, int s4,
	uwrap_callback uwrap_cb1, float_t u1,
	uwrap_callback uwrap_cb2, float_t u2,
	uwrap_callback uwrap_cb3, float_t u3,
	uwrap_callback uwrap_cb4, float_t u4
) {
	// compute weights and indices
	int i1, i2; float_t w1; (*uwrap_cb1)(u1, s1, &i1, &i2, &w1);
	int j1, j2; float_t w2; (*uwrap_cb2)(u2, s2, &j1, &j2, &w2);
	int k1, k2; float_t w3; (*uwrap_cb3)(u3, s3, &k1, &k2, &w3);
	int l1, l2; float_t w4; (*uwrap_cb4)(u4, s4, &l1, &l2, &w4);

	// fetches
	const T &p01 = points[i1 + s1 * (j1 + s2 * (k1 + s3 * l1))];
	const T &p02 = points[i2 + s1 * (j1 + s2 * (k1 + s3 * l1))];
	const T &p03 = points[i1 + s1 * (j2 + s2 * (k1 + s3 * l1))];
	const T &p04 = points[i2 + s1 * (j2 + s2 * (k1 + s3 * l1))];
	const T &p05 = points[i1 + s1 * (j1 + s2 * (k2 + s3 * l1))];
	const T &p06 = points[i2 + s1 * (j1 + s2 * (k2 + s3 * l1))];
	const T &p07 = points[i1 + s1 * (j2 + s2 * (k2 + s3 * l1))];
	const T &p08 = points[i2 + s1 * (j2 + s2 * (k2 + s3 * l1))];
	const T &p09 = points[i1 + s1 * (j1 + s2 * (k1 + s3 * l2))];
	const T &p10 = points[i2 + s1 * (j1 + s2 * (k1 + s3 * l2))];
	const T &p11 = points[i1 + s1 * (j2 + s2 * (k1 + s3 * l2))];
	const T &p12 = points[i2 + s1 * (j2 + s2 * (k1 + s3 * l2))];
	const T &p13 = points[i1 + s1 * (j1 + s2 * (k2 + s3 * l2))];
	const T &p14 = points[i2 + s1 * (j1 + s2 * (k2 + s3 * l2))];
	const T &p15 = points[i1 + s1 * (j2 + s2 * (k2 + s3 * l2))];
	const T &p16 = points[i2 + s1 * (j2 + s2 * (k2 + s3 * l2))];

	// compute linear interpolation
	const T tmp01 = lerp(p01, p02, w1);
	const T tmp02 = lerp(p03, p04, w1);
	const T tmp03 = lerp(p05, p06, w1);
	const T tmp04 = lerp(p07, p08, w1);
	const T tmp05 = lerp(p09, p10, w1);
	const T tmp06 = lerp(p11, p12, w1);
	const T tmp07 = lerp(p13, p14, w1);
	const T tmp08 = lerp(p15, p16, w1);

	// compute bilinear interpolation
	const T tmp09 = lerp(tmp01, tmp02, w2);
	const T tmp10 = lerp(tmp03, tmp04, w2);
	const T tmp11 = lerp(tmp05, tmp06, w2);
	const T tmp12 = lerp(tmp07, tmp08, w2);

	// compute trilinear interpolation
	const T tmp13 = lerp(tmp09, tmp10, w3);
	const T tmp14 = lerp(tmp11, tmp12, w3);

	// return quadrilinear interpolation
	return lerp(tmp13, tmp14, w4);
}

} // namespace spline

// *****************************************************************************
// Fresnel API implementation
namespace fresnel {

void ior_to_f0(float_t ior, float_t *f0)
{
	DJB_ASSERT(ior > 0 && "Invalid ior");
	DJB_ASSERT(f0 && "Null output ptr");
	float_t tmp = (ior - 1) / (ior + 1);

	(*f0) = tmp * tmp;
}

void ior_to_f0(const brdf::value_type& ior, brdf::value_type *f0)
{
	DJB_ASSERT(f0 && "Null output ptr");
	for (int i = 0; i < (int)ior.size(); ++i)
		ior_to_f0(ior[i], &((*f0)[i]));
}

void f0_to_ior(float_t f0, float_t *ior)
{
	DJB_ASSERT(ior && "Null output ptr");
	if (f0 == 1) {
		(*ior) = 1;
	} else {
		float_t sqrt_f0 = sqrt(f0);

		(*ior) = (1 + sqrt_f0) / (1 - sqrt_f0);
	}
}

void f0_to_ior(const brdf::value_type& f0, brdf::value_type *ior)
{
	DJB_ASSERT(ior && "Null output ptr");
	for (int i = 0; i < (int)f0.size(); ++i)
		f0_to_ior(f0[i], &(*ior)[i]);
}

static float_t unpolarized__eval(float_t zd, float_t ior)
{
	float_t c = zd;
	float_t n = ior;
	float_t g = sqrt(n * n + c * c - 1);
	float_t tmp1 = c * (g + c) - 1;
	float_t tmp2 = c * (g - c) + 1;
	float_t tmp3 = (tmp1 * tmp1) / (tmp2 * tmp2);
	float_t tmp4 = ((g - c) * (g - c)) / ((g + c) * (g + c));

	return ((tmp4 / 2) * (1 + tmp3));
}

brdf::value_type unpolarized::eval(float_t zd) const
{
	DJB_ASSERT(zd >= 0 && zd <= 1 && "Invalid Angle");
	brdf::value_type F(ior.size());

	for (int i = 0; i < (int)ior.size(); ++i)
		F[i] = unpolarized__eval(zd, ior[i]);

	return F;
}

schlick::schlick(const brdf::value_type &f0): f0(f0)
{
}

brdf::value_type schlick::eval(float_t zd) const
{
	DJB_ASSERT(zd >= 0 && zd <= 1 && "Invalid Angle");
	float_t c1 = 1 - zd;
	float_t c2 = c1 * c1;
	float_t c5 = c2 * c2 * c1;

	return f0 + c5 * ((float_t)1 - f0);
}

sgd::sgd(const vec3 &f0, const vec3 &f1)
{
	this->f0 = brdf::value_type(&f0.x, 3);
	this->f1 = brdf::value_type(&f1.x, 3);
}

brdf::value_type sgd::eval(float_t zd) const
{
	DJB_ASSERT(zd >= 0 && zd <= 1 && "Invalid Angle");
	float_t c = zd;

	return f0 - c * f1 + pow(1 - c, 5) * ((float_t)1 - f0);
}

} // namespace fresnel

// *****************************************************************************
// Microfacet API

// -----------------------------------------------------------------------------
/**
 * Compute the PDF parameters associated to the parameters of an ellipse
 */
static void
ellipse_to_p22args(
	float_t a1, float_t a2, float_t phi_a,
	float_t *ax, float_t *ay, float_t *cor
) {
	float_t cos_phi_a = cos(phi_a);
	float_t sin_phi_a = sin(phi_a);
	float_t cos_2phi_a = 2 * cos_phi_a * cos_phi_a - 1; // cos(2x) = 2cos(x)^2 - 1
	float_t a1_sqr = a1 * a1;
	float_t a2_sqr = a2 * a2;
	float_t tmp1 = a1_sqr + a2_sqr;
	float_t tmp2 = a1_sqr - a2_sqr;

	(*ax) = sqrt((tmp1 + tmp2 * cos_2phi_a) / 2);
	(*ay) = sqrt((tmp1 - tmp2 * cos_2phi_a) / 2);
	(*cor) = (a2_sqr - a1_sqr) * cos_phi_a * sin_phi_a / ((*ax) * (*ay));
}

//------------------------------------------------------------------------------
// Microfacet Params

microfacet::args microfacet::args::standard()
{
	return microfacet::args::isotropic(1);
}

microfacet::args microfacet::args::isotropic(float_t a)
{
	return microfacet::args::elliptic(a, a, 0);
}

microfacet::args
microfacet::args::elliptic(float_t a1, float_t a2, float_t phi_a)
{
	return microfacet::args::normalmap(vec3(0, 0, 1), a1, a2, phi_a);
}

microfacet::args microfacet::args::normalmap(const vec3 &n, float_t a)
{
	return microfacet::args::normalmap(n, a, a, 0);
}

microfacet::args
microfacet::args::normalmap(
	const vec3 &n, float_t a1, float_t a2, float_t phi_a
) {
	DJB_ASSERT(n.z > 0 && "invalid normal direction");
	float_t txn = -n.x / n.z;
	float_t tyn = -n.y / n.z;
	float_t ax, ay, cor;
	ellipse_to_p22args(a1, a2, phi_a, &ax, &ay, &cor);

	return microfacet::args::p22args(ax, ay, cor, txn, tyn);
}

microfacet::args
microfacet::args::p22args(
	float_t ax, float_t ay, float_t cor,
	float_t txn, float_t tyn
) {
	return microfacet::args(ax, ay, cor, txn, tyn);
}

microfacet::args::args(
	float_t ax, float_t ay, float_t cor, float_t txn, float_t tyn
) {
	float_t ccor = sqrt(1 - sqr(cor));
	float_t tmp = ay * ccor;
	float_t mtra21 = - cor / (ax * ccor);
	float_t mtra22 = 1 / tmp;
	float_t mtra23 = (ax * tyn - ay * txn * cor) / (ax * tmp);

	minv = mat3(
		ax   , ay * cor, 0,
		0    , tmp     , 0,
		-txn, -tyn     , 1
	);
	mtra = mat3(
		1 / ax, 0     , txn / ax,
		mtra21, mtra22, mtra23,
		0     , 0     ,   1
	);
	detm = 1 / (ax * tmp);
}

//------------------------------------------------------------------------------
// Ctor
microfacet::microfacet(const fresnel::impl &f):
	m_fresnel(f)
{ }

//------------------------------------------------------------------------------
// Eval API

/* Monostatic Shadowing Term */
float_t
microfacet::g1(const vec3 &wm, const vec3 &wi, const args &args) const
{
	if (wm.z > 0)
		return sat(wi.z) / sigma(wi, args);
	return 0;
}

/* Bistatic Shadowing Term */
float_t
microfacet::g2(
	const vec3 &wm,
	const vec3 &wi,
	const vec3 &wo,
	const args &args
) const {
	if (wm.z > 0 && wi.z > 0 && wo.z > 0) {
		float_t zizo = wi.z * wo.z;
		float_t sigma_i = sigma(wi, args);
		float_t sigma_o = sigma(wo, args);
		return zizo / (sigma_i * wo.z + sigma_o * wi.z - zizo);
	}
	return 0;
}

/* Conditional Shadowing Term */
float_t
microfacet::gcd(
	const vec3 &wm,
	const vec3 &wi,
	const vec3 &wo,
	const args &args
) const {
	if (wm.z > 0 && wi.z > 0 && wo.z > 0) {
		float_t sigma_i = sigma(wi, args);
		float_t sigma_o = sigma(wo, args);
		float_t tmp = wo.z * sigma_i;
		return tmp / (wi.z * sigma_o + tmp - wi.z * wo.z);
	}
	return 0;
}

/* VNDF */
float_t microfacet::vndf_std(const vec3 &wm, const vec3 &wi) const
{
	return sat(dot(wm, wi)) * ndf_std(wm) / sigma_std(wi);
}

float_t
microfacet::vndf(const vec3 &wm, const vec3 &wi, const args &args) const
{
	vec3 wm_std = args.mtra * wm;
	vec3 wi_std = normalize(args.minv * wi);
	float_t dpd = dot(wm_std, wi_std);

	if (dpd > 0) {
		float_t nrmsqr = dot(wm_std, wm_std);
		float_t nrm = inversesqrt(nrmsqr);
		float_t dwdw = args.detm / sqr(nrmsqr);
		float_t D = ndf_std(wm_std * nrm);
		float_t sigma = sigma_std(wi_std);

		return D > 0 ? dpd * D / sigma * dwdw : 0;
	}

	return 0;
}

/* NDF */
float_t microfacet::ndf(const vec3 &wm, const args &args) const
{
	vec3 wm_std = args.mtra * wm;
	float_t nrmsqr = dot(wm_std, wm_std);
	return ndf_std(wm_std * inversesqrt(nrmsqr)) * (args.detm / sqr(nrmsqr));
}

/* Projected Area */
float_t microfacet::sigma(const vec3 &wi, const args &args) const
{
	vec3 wi_std = args.minv * wi;
	float_t nrm = sqrt(dot(wi_std, wi_std));
	return sigma_std(wi_std / nrm) * nrm;
}

//------------------------------------------------------------------------------
// BRDF Eval API

brdf::value_type
microfacet::eval(const vec3 &wi, const vec3 &wo, const void *user_args) const
{
	vec3 tmp = wi + wo;
	float_t nrm = dot(tmp, tmp);

	if (nrm > 0) {
		microfacet::args args =
			user_args ? *reinterpret_cast<const microfacet::args *>(user_args)
			          : microfacet::args::standard();
		vec3 wh = tmp * inversesqrt(nrm);
		float_t Dvis = vndf(wh, wi, args);
		float_t Gcd = gcd(wh, wi, wo, args);
		float_t zd = sat(dot(wh, wi));
		brdf::value_type F = m_fresnel->eval(zd);

		return Gcd > 0 ? F * ((Dvis * Gcd) / (4 * zd)) : zero_value();
	}

	return zero_value();
}

float_t
microfacet::pdf(const vec3 &wi, const vec3 &wo, const void *user_args) const
{
	vec3 tmp = wi + wo;
	float_t nrm = dot(tmp, tmp);

	if (/*wi.z > 0 &&*/ nrm > 0) {
		microfacet::args args =
			user_args ? *reinterpret_cast<const microfacet::args *>(user_args)
			          : microfacet::args::standard();
		vec3 wh = tmp * inversesqrt(nrm);
		float_t D = ndf(wh, args);
		float_t s = sigma(wi, args);

		return (D / (4 * s));
	}

	return 0;
}

//------------------------------------------------------------------------------
// BRDF Sample API
brdf::value_type
microfacet::sample(
	const vec2 &u,
	const vec3 &wi,
	vec3 *wo_out,
	float_t *pdf,
	const void *user_args
) const {
	if (wi.z > 0) {
		microfacet::args args =
			user_args ? *reinterpret_cast<const microfacet::args *>(user_args)
			          : microfacet::args::standard();
		vec3 wm = u2_to_h2(u, wi, args);
		vec3 wo = h2_to_s2(wm, wi);
		float_t zd = dot(wi, wm);
		float_t Gcd = gcd(wm, wi, wo, args);
		brdf::value_type F = m_fresnel->eval(zd);

		if (wo_out) (*wo_out) = wo;
		if (pdf) (*pdf) = ndf(wm, args) / (4 * sigma(wi, args));

		return F * Gcd;
	} else {
		if (wo_out) (*wo_out) = vec3(0);
		if (pdf) (*pdf) = 0;

		return zero_value();
	}
}

vec3
microfacet::u2_to_h2(const vec2 &u, const vec3 &wi, const args &args) const
{
	vec3 wi_std = normalize(args.minv * wi);
	vec3 wm_std = u2_to_h2_std(u, wi_std);
	vec3 wm = normalize(transpose(args.minv) * wm_std);

	return wm;
}

vec2
microfacet::h2_to_u2(
	const vec3 &wm, const vec3 &wi, const args &args
) const {
	vec3 wi_std = normalize(args.minv * wi);
	vec3 wm_std = normalize(args.mtra * wm);

	return h2_to_u2_std(wm_std, wi_std);
}

vec3 microfacet::h2_to_s2(const vec3 &wm, const vec3 &wi) const
{
	return 2 * dot(wi, wm) * wm - wi;
}

vec3 microfacet::s2_to_h2(const vec3 &wo, const vec3 &wi) const
{
	return normalize(wi + wo);
}

vec3
microfacet::u2_to_s2(const vec2 &u, const vec3 &wi, const void *user_args) const
{
	//DJB_ASSERT(wi.z >= 0 && "invalid incident direction");
	microfacet::args args =
		user_args ? *reinterpret_cast<const microfacet::args *>(user_args)
		          : microfacet::args::standard();

	return h2_to_s2(u2_to_h2(u, wi, args), wi);
}

vec2
microfacet::s2_to_u2(
	const vec3 &wo, const vec3 &wi, const void *user_args
) const {
	DJB_ASSERT(wi.z >= 0 && "invalid incident direction");
	microfacet::args args =
		user_args ? *reinterpret_cast<const microfacet::args *>(user_args)
		          : microfacet::args::standard();

	return h2_to_u2(s2_to_h2(wo, wi), wi, args);
}

// *****************************************************************************
// Microfacet Radial API

// -----------------------------------------------------------------------------
// microfacet eval interface
float_t radial::ndf_std(const vec3 &wm) const
{
	return ndf_std_radial(wm.z);
}

float_t radial::sigma_std(const vec3 &wi) const
{
	return sigma_std_radial(wi.z);
}

// -----------------------------------------------------------------------------
// microfacet sample interface
vec3 radial::u2_to_h2_std(const vec2 &u, const vec3 &wi) const
{
	float_t zi = wi.z;
	float_t z_i = sqrt(wi.x * wi.x + wi.y * wi.y);
	vec3 wm = u2_to_h2_std_radial(u, zi, z_i);

	// rotate for non-normal incidence
	if (z_i > 0) {
		float_t nrm = 1 / z_i;
		float_t c = wi.x * nrm;
		float_t s = wi.y * nrm;
		float_t x = c * wm.x - s * wm.y;
		float_t y = s * wm.x + c * wm.y;

		wm = vec3(x, y, wm.z);
	}

	return wm;
}

vec2 radial::h2_to_u2_std(const vec3 &wm, const vec3 &wi) const
{
	float_t zi = wi.z;
	float_t z_i = sqrt(wi.x * wi.x + wi.y * wi.y);
	vec3 wm_std = wm;

	// rotate for non-normal incidence
	if (z_i > 0) {
		float_t nrm = 1 / z_i;
		float_t c = wi.x * nrm;
		float_t s = wi.y * nrm;
		float_t x = c * wm.x + s * wm.y;
		float_t y = c * wm.y - s * wm.x;

		wm_std = vec3(x, y, wm.z);
	}

	return h2_to_u2_std_radial(wm_std, zi, z_i);
}

// *****************************************************************************
// Beckmann
float_t beckmann::ndf_std_radial(float_t zm) const
{
	if (zm > 0) {
		float_t rm_sqr = 1 / sqr(zm) - 1;
		return exp(-rm_sqr) / (sqr(sqr(zm)) * m_pi());
	}
	return 0;
}

float_t beckmann::sigma_std_radial(float_t zi) const
{
	if (zi == 1) return 1;
	float_t z_i = sqrt(1 - sat(sqr(zi)));
	float_t nu = zi / z_i;
	float_t tmp = exp(-sqr(nu)) * inversesqrt(m_pi());
	return (zi * (1 + erf(nu)) + z_i * tmp) / 2;
}

vec3 beckmann::u2_to_h2_std_radial(const vec2 &u, float_t zi, float_t z_i) const
{
	// remap u2 to avoid singularities
	float_t u1 = sat(u.x) * (float_t)0.99998 + (float_t)0.00001;
	float_t u2 = sat(u.y) * (float_t)0.99998 + (float_t)0.00001;
	return r2_to_h2(vec2(qf2(u1, zi, z_i), qf3(u2)));
}

vec2
beckmann::h2_to_u2_std_radial(const vec3 &wm, float_t zi, float_t z_i) const
{
	vec2 rm = h2_to_r2(wm);
	return vec2(cdf2(rm.x, zi, z_i), cdf3(rm.y));
}

//------------------------------------------------------------------------------
// mappings

// See: Approximating the erfinv function, by Mike Giles
float_t beckmann::erfinv(float_t u)
{
	if (u == -1)
		return -std::numeric_limits<float>::infinity();
	else if (u == 1)
		return +std::numeric_limits<float>::infinity();
	else {
		float_t w, p;

		w = -logf((1 - u) * (1 + u));
		if (w < (float_t)5.0) {
			w = w - (float_t)2.500000;
			p = (float_t)2.81022636e-08;
			p = (float_t)3.43273939e-07 + p * w;
			p = (float_t)-3.5233877e-06 + p * w;
			p = (float_t)-4.39150654e-06 + p * w;
			p = (float_t)0.00021858087 + p * w;
			p = (float_t)-0.00125372503 + p * w;
			p = (float_t)-0.00417768164 + p * w;
			p = (float_t)0.246640727 + p * w;
			p = (float_t)1.50140941 + p * w;
		} else {
			w = sqrt(w) - (float_t)3.0;
			p = (float_t)-0.000200214257;
			p = (float_t)0.000100950558 + p * w;
			p = (float_t)0.00134934322 + p * w;
			p = (float_t)-0.00367342844 + p * w;
			p = (float_t)0.00573950773 + p * w;
			p = (float_t)-0.0076224613 + p * w;
			p = (float_t)0.00943887047 + p * w;
			p = (float_t)1.00167406 + p * w;
			p = (float_t)2.83297682 + p * w;
		}

		return p * u;
	}
}

float_t beckmann::cdf2(float_t tx, float_t zi, float_t z_i) const
{
	float_t sigma_i = sigma_std_radial(zi);
	float_t tmp1 = z_i * exp(-sqr(tx)) / (2 * sqrt(m_pi()));
	float_t tmp2 = zi * (std::erf(tx) + 1) / 2;

	return (tmp1 + tmp2) / sigma_i;
}

float_t beckmann::cdf3(float_t ty) const
{
	return (std::erf(ty) + 1) / 2;
}

float_t beckmann::qf3(float_t u) const
{
	return erfinv(2 * u - 1);
}

float_t beckmann::qf2(float_t u, float_t zi, float_t z_i) const
{
	if (u == 0)
		return -std::numeric_limits<float>::infinity();
	else if (u == 1)
		return +std::numeric_limits<float>::infinity();
	else {
		const float_t sqrt_pi_inv = 1 / sqrt(m_pi());

		/* The original inversion routine from the paper contained
		   discontinuities, which causes issues for QMC integration
		   and techniques like Kelemen-style MLT. The following code
		   performs a numerical inversion with better behavior */
		float_t cti = zi / z_i;
		float_t tti = z_i / zi;

		/* Search interval -- everything is parameterized
		   in the erf() domain */
		float_t a = -1, c = std::erf(cti);

		/* Start with a good initial guess */
		/* We can do better (inverse of an approximation computed in Mathematica) */
		float_t ti = acos(zi);
		float_t fit = 1 + ti * (-0.876f + ti * (0.4265f - 0.0594f * ti));
		float_t b = c - (1 + c) * std::pow(1 - u, fit);

		/* Normalization factor for the CDF */
		float_t nrm;
		if (z_i > 0)
			nrm = 1 / (1 + c + sqrt_pi_inv * tti * exp(-sqr(cti)));
		else
			nrm = 1 / (1 + c);

		int it = 0;
		while (++it < 10) {
			/* Bisection criterion -- the oddly-looking
			   boolean expression are intentional to check
			   for NaNs at little additional cost */
			if (!(b >= a && b <= c))
				b = 0.5f * (a + c);

			/* Evaluate the CDF and its derivative
			   (i.e. the density function) */
			float_t invErf = erfinv(b);
			float_t value = nrm * (1 + b + sqrt_pi_inv *
				tti * std::exp(-invErf * invErf)) - u;
			float_t derivative = nrm * (1 - invErf * tti);

			if (fabs(value) < 1e-5f)
				break;

			/* Update bisection intervals */
			if (value > 0)
				c = b;
			else
				a = b;

			b -= value / derivative;
		}

		/* Now convert back into a slope value */
		return erfinv(b);
	}
}

vec2 beckmann::h2_to_r2(const vec3 &wm)
{
	return vec2(-wm.x / wm.z, -wm.y / wm.z);
}

vec3 beckmann::r2_to_h2(const vec2 &twm)
{
	return normalize(vec3(-twm.x, -twm.y, 1));
}
// *****************************************************************************
// GGX

//------------------------------------------------------------------------------
// map uniform to the half disk oriented towards the +x direction
vec2 ggx::u2_to_hd2(const vec2 &u)
{
	vec2 v = vec2((1 + u.x) / 2, u.y);
	return brdf::u2_to_d2(v);
}

vec2 ggx::hd2_to_u2(const vec2 &d)
{
	vec2 u = brdf::d2_to_u2(d);
	return vec2(2 * u.x - 1, u.y);
}

//------------------------------------------------------------------------------
// map uniform to a moon, i.e., a restricted portion of the unit disk
vec2 ggx::u2_to_md2(const vec2& u, float_t zi)
{
	float_t a = 1 / (1 + zi);

#if 0
	if (u.x > a) {
		float_t xu = (u.x - a) / (1 - a); // remap to [0, 1]

		return vec2(zi, 1) * u2_to_hd2(vec2(xu, u.y));
	} else {
		float_t xu = (u.x - a) / a; // remap to [-1, 0]

		return u2_to_hd2(vec2(xu, u.y));
	}
#else
	float_t nrm = sqrt(u.x);

	if (u.y > a) {
		float_t uy = (u.y - a) / (1 - a); // remap to [0, 1]
		float_t phi = uy * m_pi() + m_pi();

		return nrm * vec2(-sin(phi) * zi, cos(phi));
	} else {
		float_t uy = u.y / a; // remap to [0, 1]
		float_t phi = uy * m_pi();

		return nrm * vec2(-sin(phi), cos(phi));
	}
#endif
}

// inverse GGX STD mapping
vec2 ggx::md2_to_u2(const vec2& d, float_t zi)
{
	float_t a = 1 / (1 + zi);

#if 0
	if (d.x >= 0) {
		vec2 tmp = hd2_to_u2(vec2(d.x / zi, d.y));

		return vec2(tmp.x * (1 - a) + a, tmp.y);
	} else {
		vec2 tmp = hd2_to_u2(d);

		return vec2(a + a * tmp.x, tmp.y);
	}
#else

	if (d.x >= 0) {
		vec2 tmp = vec2(d.x / zi, d.y);
		float_t x = dot(tmp, tmp);
		float_t phi = atan2(-tmp.x, tmp.y);
		while (phi < 0) phi+= 2*m_pi();
		float_t tmp2 = (phi - m_pi()) / m_pi();
		float_t y = tmp2 * (1 - a) + a;

		return vec2(x, y);
	} else {
		float_t x = dot(d, d);
		float_t tmp = atan2(-d.x, d.y) / m_pi();
		float_t y = tmp * a;

		return vec2(x, y);
	}
#endif
}

//------------------------------------------------------------------------------
// map a point on the oriented disk onto the upper hemisphere
vec3 ggx::d2_to_h2(const vec2 &d, float_t zi, float_t z_i)
{
	vec3 z = vec3(z_i, 0, zi);
	vec3 y = vec3(0, 1, 0);
	vec3 x = vec3(zi, 0, -z_i); // cross(z, y)
	float_t tmp = sat(1 - dot(d, d));
	vec3 wm = x * d.x + y * d.y + z * sqrt(tmp);

	return vec3(wm.x, wm.y, sat(wm.z));
}

// map the upper hemisphere onto the oriented unit disc
vec2 ggx::h2_to_d2(const vec3 &h, float_t zi, float_t z_i)
{
	vec3 x = vec3(zi, 0, -z_i);
	vec3 y = vec3(0, 1, 0);

	return vec2(dot(x, h), dot(y, h));
}

//------------------------------------------------------------------------------
// eval API
float_t ggx::ndf_std_radial(float_t zm) const
{
	if (zm >= 0)
		return (1 / m_pi());
	return 0;
}

float_t ggx::sigma_std_radial(float_t zi) const
{
	return (1 + zi) / 2;
}

//------------------------------------------------------------------------------
// mapping API
vec3 ggx::u2_to_h2_std_radial(const vec2 &u, float_t zi, float_t z_i) const
{
	return d2_to_h2(u2_to_md2(u, zi), zi, z_i);
}

vec2 ggx::h2_to_u2_std_radial(const vec3 &wm, float_t zi, float_t z_i) const
{
	return md2_to_u2(h2_to_d2(wm, zi, z_i), zi);
}

// *****************************************************************************
// tab_r and tab

//------------------------------------------------------------------------------
// ctor
tab_r::tab_r(const std::vector<float_t> &ndf):
	radial(fresnel::ideal<1>()), m_ndf(ndf)
{
	configure();
}

tab_r::tab_r(const brdf &fr, int resolution):
	radial(fresnel::ideal<1>())
{
	m_ndf.reserve(resolution);
	extract_ndf(fr, resolution);
	configure();
}

tab::tab(const std::vector<float_t> &ndf, int zres, int pres):
	microfacet(fresnel::ideal<1>()), m_ndf(ndf), m_zres(zres), m_pres(pres)
{
	configure();
}

tab::tab(const brdf &fr, int zres, int pres):
	microfacet(fresnel::ideal<1>()), m_zres(zres), m_pres(pres)
{
	m_ndf.reserve(zres * pres);
	extract_ndf(fr);
	configure();
}

//------------------------------------------------------------------------------
// configure
void tab_r::configure()
{
	m_cdf.reserve(sqr(m_ndf.size()) * m_ndf.size() * 4);

	compute_cdf();
	normalize_ndf();
}

void tab::configure()
{
	m_cdf.reserve(sqr(m_zres) * sqr(m_pres));

	compute_cdf();
	normalize_ndf();
}

//------------------------------------------------------------------------------
/**
 * Extract the microfacet NDF with power iterations
 *
 * The extraction requires exponentiating a matrix, so a small, self-contained
 * rowmajor matrix API is implemented first.
 */
class matrix {
	std::vector<double> mij;
	int size;
public:
	matrix(int size);
	double& operator()(int i, int j) {return mij[j*size+i];}
	const double& operator()(int i, int j) const {return mij[j*size+i];}
	void transform(const std::vector<double> &v,
	               std::vector<double> &out) const;
	std::vector<double> eigenvector(int iterations) const;
};

matrix::matrix(int size) : mij(size * size, 0), size(size)
{}

void
matrix::transform(const std::vector<double> &v, std::vector<double> &out) const
{
	out.resize(0);

	for (int j = 0; j < size; ++j) {
		out.push_back(0);

		for (int i = 0; i < size; ++i) {
			out[j]+= (*this)(i, j) * v[i];
		}
	}
}

std::vector<double> matrix::eigenvector(int iterations) const
{
	std::vector<double> vec[2];
	int j = 0;

	vec[0].reserve(size);
	vec[1].reserve(size);

	for (int i = 0; i < size; ++i)
		vec[j][i] = 1.0;
	for (int i = 0; i < iterations; ++i) {
		double nrm = 0;

		transform(vec[j], vec[1-j]);

#if 1
		// normalize the vector
		for (int k = 0; k < size; ++k)
			nrm+= sqr(vec[1-j][k]);
		if (nrm > 0) {
			nrm = inversesqrt(nrm);

			for (int k = 0; k < size; ++k)
				vec[1-j][k]*= nrm;
		}
#endif

		j = 1 - j;
	}

	return vec[j];
}

//------------------------------------------------------------------------------
// extract the microfacet NDF with power iterations (isotropic optimization)
void tab_r::extract_ndf(const brdf &brdf, int res)
{
	const int cnt = res - 1;
	const double du = (double)m_pi() / cnt;
	matrix km(cnt);
	std::vector<brdf::io_pair> io;
	std::vector<brdf::value_type> frp_v;

	// evaluate all directions
	for (int i = 0; i < cnt; ++i) {
		float_t u = (float_t)i / (float_t)cnt;
		float_t ti = sqr(u) * m_pi() / 2;
		float_t zi = cos(ti), z_i = sin(ti);
		vec3 wi = vec3(z_i, 0, zi);

		io.push_back(brdf::io_pair(wi, wi));
	}
	frp_v = brdf.eval_batch(io);

	// build kernel
	for (int i = 0; i < cnt; ++i) {
		float_t u = (float_t)i / cnt;
		float_t ti = sqr(u) * m_pi() / 2;
		float_t zi = cos(ti), z_i = sin(ti);
		brdf::value_type frp = frp_v[i];
		float_t frp_i = frp.sum() / frp.size();//dot(frp, vec3(0.2126, 0.7152, 0.0722));
		double kji_tmp = (double)frp_i * du;

		for (int j = 0; j < cnt; ++j) {
			const int nk = 180;
			const double dk = 2 * (double)m_pi() / nk;
			float_t u = (float_t)j / cnt;
			float_t tm = sqr(u) * m_pi() / 2;
			float_t zm = cos(tm), z_m = sin(tm);
			double nint = 0;

			for (int k = 0; k < nk; ++k) {
				float_t u = (float_t)k / (float_t)nk;
				float_t pm = u * 2 * m_pi();

				nint+= (double)sat(z_i * z_m * cos(pm) + zi * zm);
			}
			nint*= dk;

			km(j, i) = kji_tmp * nint * (double)(u * z_m);
		}
	}

	// compute
	const std::vector<double> v = km.eigenvector(4);
	for (int i = 0; i < (int)v.size(); ++i)
		m_ndf.push_back(v[i]);
	m_ndf.push_back(max((float_t)0, 2 * m_ndf[cnt - 1] - m_ndf[cnt - 2]));

#ifndef NVERBOSE
	DJB_LOG("djb_verbose: NDF extraction complete\n");
#endif
}

//------------------------------------------------------------------------------
// extract the microfacet NDF with power iterations
void tab::extract_ndf(const brdf &brdf)
{
	const int w = m_zres - 1;
	const int h = m_pres;
	const double du1 = (double)m_pi() / w;
	const double du2 = 2 * (double)m_pi() / h;
	matrix km(w * h);
	std::vector<brdf::io_pair> io;
	std::vector<brdf::value_type> frp_v;

	// evaluate all directions
	for (int i2 = 0; i2 < h; ++i2)
	for (int i1 = 0; i1 < w; ++i1) {
		float_t u1 = (float_t)i1 / w; // in [0, 1)
		float_t u2 = (float_t)i2 / h; // in [0, 1)
		float_t ti = sqr(u1) * m_pi() / 2; // in [0, pi/2)
		float_t pi = u2 * 2 * m_pi(); // in [0, 2pi)
		float_t zi = cos(ti), z_i = sin(ti);
		vec3 wi = vec3(z_i * cos(pi), z_i * sin(pi), zi);

		io.push_back(brdf::io_pair(wi, wi));
	}
	frp_v = brdf.eval_batch(io);

	// build kernel
	for (int i2 = 0; i2 < h; ++i2)
	for (int i1 = 0; i1 < w; ++i1) {
		float_t u1_i = (float_t)i1 / w; // in [0, 1)
		float_t u2_i = (float_t)i2 / h; // in [0, 1)
		float_t ti = sqr(u1_i) * m_pi() / 2; // in [0, pi/2)
		float_t pi = u2_i * 2 * m_pi(); // in [0, 2pi)
		float_t zi = cos(ti), z_i = sin(ti);
		vec3 wi = vec3(z_i * cos(pi), z_i * sin(pi), zi);
		brdf::value_type frp = frp_v[i1 + w * i2];
		float_t frp_i = frp.sum() / frp.size();//dot(frp, vec3(0.2126, 0.7152, 0.0722));
		double kji_tmp = (double)frp_i * du1 * du2;

		for (int j2 = 0; j2 < h; ++j2)
		for (int j1 = 0; j1 < w; ++j1) {
			float_t u1_m = (float_t)j1 / w; // in [0, 1)
			float_t u2_m = (float_t)j2 / h; // in [0, 1)
			float_t tm = sqr(u1_m) * m_pi() / 2; // in [0, pi/2)
			float_t pm = u2_m * 2 * m_pi();      // in [0, 2pi)
			float_t zm = cos(tm), z_m = sin(tm);
			vec3 wm = vec3(z_m * cos(pm), z_m * sin(pm), zm);
			float_t dp = sat(dot(wm, wi));

			km(j1 + w * j2, i1 + w * i2) = kji_tmp * (double)(u1_m * dp * z_m);
		}
	}

	// compute slope pdf
	const std::vector<double> v = km.eigenvector(4);

	// store NDF
	for (int j = 0; j < h; ++j) {
		for (int i = 0; i < w; ++i) {
			m_ndf.push_back(v[i + w * j]);
		}
		float_t p1 = v[j * w + w - 2];
		float_t p2 = v[j * w + w - 1];
		m_ndf.push_back(max((float_t)0, 2 * p2 - p1));
	}

#ifndef NVERBOSE
	DJB_LOG("djb_verbose: NDF extraction complete\n");
#endif
}

//------------------------------------------------------------------------------
// normalize ndf
void tab_r::normalize_ndf()
{
	DJB_ASSERT(cdf(vec2(1, 1), 1) > 0 && "Invalid NDF normalization");
	float_t nrm = 1 / cdf(vec2(1, 1), 1);

	for (int i = 0; i < (int)m_ndf.size(); ++i)
		m_ndf[i]*= nrm;

	for (int i = 0; i < (int)m_cdf.size(); ++i)
		m_cdf[i]*= nrm;

#ifndef NVERBOSE
	DJB_LOG("djb_verbose: NDF norm. constant = %.9f\n", (double)nrm);
#endif
}

void tab::normalize_ndf()
{
	DJB_ASSERT(cdf(vec2(1, 1), vec3(0, 0, 1)) > 0 && "Invalid NDF normalization");
	float_t nrm = 1 / cdf(vec2(1, 1), vec3(0, 0, 1));

	for (int i = 0; i < (int)m_ndf.size(); ++i)
		m_ndf[i]*= nrm;

	for (int i = 0; i < (int)m_cdf.size(); ++i)
		m_cdf[i]*= nrm;

#ifndef NVERBOSE
	DJB_LOG("djb_verbose: NDF norm. constant = %.9f\n", (double)nrm);
#endif
}

//------------------------------------------------------------------------------
void tab_r::compute_cdf()
{
	int nti = 16;
	int nu2 = 256;
	int nu1 = 64;
	double du2 = (double)m_pi() / nu1;
	double du1 = 2 * (double)m_pi() / nu2;

	for (int i3 = 0; i3 < nti; ++i3) {
		float_t u = (float_t)i3 / (nti - 1); // in [0,1]
		float_t ti = sqrt(u) * m_pi() / 2;     // in [0,pi/2]
		float_t zi = sat(cos(ti)), z_i = sin(ti);
		vec3 wi = vec3(z_i, 0, zi);

		// bottom row (zeroes)
		for (int i1 = 0; i1 < nu1; ++i1)
			m_cdf.push_back(vec2(0));

		// rest of the domain
		for (int i2 = 1; i2 < nu2; ++i2) {
			float_t u2 = (float_t)i2 / nu2;   // in (0,1)
			float_t tm = sqr(u2) * m_pi() / 2;  // in [0,pi/2)
			float_t ctm = cos(tm), stm = sin(tm);
			float_t ndf = ndf_std_radial(ctm);
			double nint = 0;

			for (int i1 = 0; i1 < nu1; ++i1) {
				float_t u1 = (float_t)i1 / nu1;    // in [0,1)
				float_t pm = (2 * u1 - 1) * m_pi();  // in [-pi,pi)
				float_t cpm = cos(pm), spm = sin(pm);
				vec3 wm = vec3(stm * cpm, stm * spm, ctm);
				float_t dp = sat(dot(wm, wi));
				vec2 nint2 = m_cdf[i1 + nu1 * (i2 - 1 + nu2 * i3)];
				float_t tmp = dp * ndf * u2 * stm;

				nint+= (double)tmp;
				m_cdf.push_back(vec2(nint * du1, tmp) * du2 + nint2);
			}
		}
	}

#ifndef NVERBOSE
	DJB_LOG("djb_verbose: CDF ready\n");
#endif
}

void tab::compute_cdf()
{
	int npi = 32;
	int nti = 16;
	int nu2 = 64;
	int nu1 = 512;
	double du2 = (double)m_pi() / nu1;
	double du1 = 2 * (double)m_pi() / nu2;

	for (int i4 = 0; i4 < npi; ++i4) {
		float_t u = (float_t)i4 / npi;   // in [0,1)
		float_t pi = (2 * u - 1) * m_pi(); // in [-pi,pi)
		float_t cpi = cos(pi), spi = sin(pi);

		for (int i3 = 0; i3 < nti; ++i3) {
			float_t u = (float_t)i3 / (nti - 1); // in [0,1]
			float_t ti = sqrt(u) * m_pi() / 2;     // in [0,pi/2]
			float_t zi = sat(cos(ti)), z_i = sin(ti);
			vec3 wi = vec3(z_i * cpi, z_i * spi, zi);

			// bottom row (zeroes since z_m = 0)
			for (int i1 = 0; i1 < nu1; ++i1)
				m_cdf.push_back(vec2(0));

			// rest of the domain
			for (int i2 = 1; i2 < nu2; ++i2) {
				float_t u2 = (float_t)i2 / nu2;   // in (0,1)
				float_t tm = sqr(u2) * m_pi() / 2;  // in [0,pi/2)
				float_t ctm = cos(tm), stm = sin(tm);
				double nint = 0;

				for (int i1 = 0; i1 < nu1; ++i1) {
					float_t u1 = (float_t)i1 / nu1;    // in [0,1)
					float_t pm = (2 * u1 - 1) * m_pi();  // in [phi_i-pi/2,phi_i+pi/2)
					float_t cpm = cos(pm), spm = sin(pm);
					vec3 wm = vec3(stm * cpm, stm * spm, ctm);
					float_t ndf = ndf_std(wm);
					float_t dp = sat(dot(wm, wi));
					int idx = i1 + nu1 * (i2 - 1 + nu2 * (i3 + nti * i4));
					vec2 nint2 = m_cdf[idx];
					float_t tmp = dp * ndf * u2 * stm;

					nint+= (double)tmp;
					m_cdf.push_back(vec2(nint * du1, tmp) * du2 + nint2);
				}
			}
		}
	}

#ifndef NVERBOSE
	DJB_LOG("djb_verbose: CDF ready\n");
#endif
}

//------------------------------------------------------------------------------
// Mapping API
vec2 tab_r::cdfv(const vec2 &u, float_t zi) const
{
	int res1 = 64;
	int res2 = 256;
	int res3 = 16;
	float_t u1 = u.x;
	float_t u2 = u.y;
	float_t u3 = sqr(acos(zi) * (2 / m_pi()));

	return spline::eval3d(m_cdf, res1, res2, res3,
	                      spline::uwrap_edge, u1,
	                      spline::uwrap_edge, u2,
	                      spline::uwrap_edge, u3);
}

float_t tab_r::cdf(const vec2 &u, float_t zi) const
{
	return cdfv(u, zi).x;
}

float_t tab_r::cdf1(float_t u1, float_t zi) const
{
	float_t bmin = cdf(vec2( 0, 1), zi);
	float_t bmax = cdf(vec2( 1, 1), zi);
	float_t eval = cdf(vec2(u1, 1), zi);

	return (eval - bmin) / (bmax - bmin);
}

float_t tab_r::cdf2(float_t u2, float_t u1, float_t zi) const
{
	float_t nrm = cdfv(vec2(u1,  1), zi).y;
	float_t cdf = cdfv(vec2(u1, u2), zi).y;

	return cdf / nrm;
}

float_t tab_r::qf1(float_t u, float_t zi) const
{
	const float_t epsilon = 1e-5;
	float_t u1 = 0, delta = sat(u);
	int32_t level = 0;

	if (delta >= 1) return 1;

	while (fabs(delta) > epsilon && level < 30) {
		u1+= (float_t)sgn(delta) / (2 << level);
		delta = u - cdf1(u1, zi);
		++level;
	}

	return u1;
}

float_t tab_r::qf2(float_t u, float_t qf1, float_t zi) const
{
	const float_t epsilon = 1e-5;
	float_t u2 = 0, delta = sat(u);
	int32_t level = 0;

	if (delta >= 1) return 1;

	while (fabs(delta) > epsilon && level < 30) {
		u2+= (float_t)sgn(delta) / (2 << level);
		delta = u - cdf2(u2, qf1, zi);
		++level;
	}

	return u2;
}

vec2 tab::cdfv(const vec2 &u, const vec3 &wi) const
{
	int res1 = 512;
	int res2 = 64;
	int res3 = 16;
	int res4 = 32;
	float_t pi = wi.z < 1 ? atan2(wi.y, wi.x) : 0;
	float_t u1 = u.x;
	float_t u2 = u.y;
	float_t u3 = sqr(acos(wi.z) * (2 / m_pi()));
	float_t u4 = (pi / m_pi() + 1) / 2;

	return spline::eval4d(m_cdf,
	                      res1, res2, res3, res4,
	                      spline::uwrap_edge  , u1,
	                      spline::uwrap_edge  , u2,
	                      spline::uwrap_edge  , u3,
	                      spline::uwrap_repeat, u4);
}

float_t tab::cdf(const vec2 &u, const vec3 &wi) const
{
	return cdfv(u, wi).x;
}

float_t tab::cdf1(float_t u1, const vec3 &wi) const
{
	float_t bmin = cdf(vec2( 0, 1), wi);
	float_t bmax = cdf(vec2( 1, 1), wi);
	float_t eval = cdf(vec2(u1, 1), wi);

	return (eval - bmin) / (bmax - bmin);
}

float_t tab::cdf2(float_t u2, float_t u1, const vec3 &wi) const
{
	float_t nrm = cdfv(vec2(u1,  1), wi).y;
	float_t cdf = cdfv(vec2(u1, u2), wi).y;

	return cdf / nrm;
}

float_t tab::qf1(float_t u, const vec3 &wi) const
{
	const float_t epsilon = 1e-4;
	float_t u1 = 0, delta = sat(u);
	int32_t level = 0;

	if (delta >= 1) return 1;

	while (fabs(delta) > epsilon && level < 30) {
		u1+= (float_t)sgn(delta) / (2 << level);
		delta = u - cdf1(u1, wi);
		++level;
	}

	return u1;
}

float_t tab::qf2(float_t u, float_t qf1, const vec3 &wi) const
{
	const float_t epsilon = 1e-4;
	float_t u2 = 0, delta = sat(u);
	int32_t level = 0;

	if (delta >= 1) return 1;

	while (fabs(delta) > epsilon && level < 30) {
		u2+= (float_t)sgn(delta) / (2 << level);
		delta = u - cdf2(u2, qf1, wi);
		++level;
	}

	return u2;
}

//------------------------------------------------------------------------------
// eval API
float_t tab_r::ndf_std_radial(float_t zm) const
{
	if (zm >= 0) {
		float_t u = sqrt(acos(zm) * (2 / m_pi()));
		return spline::eval(m_ndf, (int)m_ndf.size(), spline::uwrap_edge, u);
	}

	return 0;
}

float_t tab_r::sigma_std_radial(float_t zi) const
{
	return cdf(vec2(1, 1), sat(zi));
}

float_t tab::ndf_std(const vec3 &wm) const
{
	if (wm.z >= 0) {
		int w = m_zres;
		int h = m_pres;
		float_t pm = wm.z < 1 ? atan2(wm.y, wm.x) : 0;
		float_t u1 = sqrt(acos(wm.z) * (2 / m_pi()));
		float_t u2 = (pm / m_pi() + 1) / 2;

		return spline::eval2d(m_ndf, w, h,
		                      spline::uwrap_edge  , u1,
		                      spline::uwrap_repeat, u2);
	}
	return 0;
}

float_t tab::sigma_std(const vec3 &wi) const
{
	return cdf(vec2(1, 1), wi);
}

//------------------------------------------------------------------------------
// mapping API
vec3 tab_r::u2_to_h2_std_radial(const vec2 &u, float_t zi, float_t) const
{
	float_t u1 = qf1(u.x, zi);
	float_t u2 = qf2(u.y, u1, zi);
	float_t pm = (2 * u1 - 1) * m_pi();
	float_t tm = sqr(u2) * m_pi() / 2;
	float_t z_m = sin(tm);

	return vec3(z_m * cos(pm), z_m * sin(pm), cos(tm));
}

vec2 tab_r::h2_to_u2_std_radial(const vec3 &wm, float_t zi, float_t) const
{
	float_t pm = wm.z < 1 ? atan2(wm.y, wm.x) : 0;
	float_t u1 = (pm / m_pi() + 1) / 2;
	float_t u2 = sqrt(acos(sat(wm.z)) * (2 / m_pi()));

	return vec2(cdf1(u1, zi), cdf2(u2, u1, zi));
}

vec3 tab::u2_to_h2_std(const vec2 &u, const vec3 &wi) const
{
	int s = 1;//wi.y > 0 ? 1 : -1; // exploit the azimuthal symmetry of the BRDF
	vec3 wi_std = vec3(s * wi.x, s * wi.y, wi.z);
	float_t u1 = qf1(u.x, wi_std);
	float_t u2 = qf2(u.y, u1, wi_std);
	float_t pm = (2 * u1 - 1) * m_pi();
	float_t tm = sqr(u2) * m_pi() / 2;
	float_t z_m = sin(tm);

	return vec3(s * z_m * cos(pm), s * z_m * sin(pm), cos(tm));
}

vec2 tab::h2_to_u2_std(const vec3 &wm, const vec3 &wi) const
{
	int s = 1;//wi.y > 0 ? 1 : -1; // exploit the azimuthal symmetry of the BRDF
	vec3 wi_std = vec3(s * wi.x, s * wi.y, wi.z);
	vec3 wm_std = vec3(s * wm.x, s * wm.y, wm.z);
	float_t pm = wm_std.z < 1 ? atan2(wm_std.y, wm_std.x) : 0;
	float_t u1 = (pm / m_pi() + 1) / 2;
	float_t u2 = sqrt(acos(sat(wm_std.z)) * (2 / m_pi()));

	return vec2(cdf1(u1, wi_std), cdf2(u2, u1, wi_std));
}

//------------------------------------------------------------------------------
const std::vector<float_t>& tab::get_ndfv(int *zres, int *pres) const
{
	if (zres) (*zres) = m_zres;
	if (pres) (*pres) = m_pres;

	return m_ndf;
}

//------------------------------------------------------------------------------
microfacet::args tab_r::extract_beckmann_args(const tab_r &tab)
{
	int cnt = 512;
	double du = (double)sqr(m_pi()) / cnt;
	double nint = 0;
	float_t alpha;

	for (int i = 0; i < cnt; ++i) {
		float_t u = (float_t)i / cnt;
		float_t tm = sqr(u) * m_pi() / 2;
		float_t zm = cos(tm), z_m = sin(tm);
		vec3 wm = vec3(z_m, 0, zm);

		nint+= (double)(u * sqr(z_m) * tan(tm) * tab.ndf(wm));
	}
	nint*= du;
	alpha = sqrt(2 * nint);

#ifndef NVERBOSE
	DJB_LOG("djb_verbose: Beckmann_alpha = %.9f\n", (double)alpha);
#endif
	return microfacet::args::isotropic(alpha);
}

microfacet::args tab_r::extract_ggx_args(const tab_r &tab)
{
	int cnt = 512;
	double du = 4 * (double)m_pi() / cnt;
	double nint = 0;

	for (int i = 0; i < cnt; ++i) {
		float_t u = (float_t)i / cnt;
		float_t tm = sqr(u) * m_pi() / 2;
		float_t zm = cos(tm), z_m = sin(tm);
		vec3 wm = vec3(z_m, 0, zm);

		nint+= (double)(u * sqr(z_m) * tab.ndf(wm));
	}
	nint*= du;

	nint = sqrt(1 / (double)(tab.ndf_std_radial(1) * m_pi()));

#ifndef NVERBOSE
	DJB_LOG("djb_verbose: GGX_alpha = %.9f\n", nint);
#endif
	return microfacet::args::isotropic(nint);
}

microfacet::args tab::extract_ggx_args(const tab &tab)
{
	int nu2 = 512;
	int nu1 = 256;
	double du2 = 2 * (double)m_pi() / nu2;
	double du1 = (double)m_pi() / nu1;
	double nint[2] = {0, 0};

	for (int i2 = 0; i2 < nu2; ++i2) {
		float_t u2 = (float_t)i2 / nu2;
		float_t pm = 2 * m_pi() * u2;
		float_t cpm = cos(pm), spm = sin(pm);

		for (int i1 = 0; i1 < nu1; ++i1) {
			float_t u1 = (float_t)i1 / nu1;
			float_t tm = sqr(u1) * m_pi() / 2;
			float_t zm = cos(tm), z_m = sin(tm);
			vec3 wm = vec3(z_m * cpm, z_m * spm, zm);
			float_t tmp = u1 * sqr(z_m) * tab.ndf(wm);

			nint[0]+= (double)(fabs(cpm) * tmp);
			nint[1]+= (double)(fabs(spm) * tmp);
		}
	}
	nint[0]*= du1 * du2;
	nint[1]*= du1 * du2;

#ifndef NVERBOSE
	DJB_LOG("djb_verbose: GGX_alpha = {%.9f, %.9f}\n", nint[0], nint[1]);
#endif
	return microfacet::args::elliptic(nint[0], nint[1]);
}

// *****************************************************************************
// MERL API implementation

// Copyright 2005 Mitsubishi Electric Research Laboratories All Rights Reserved.

// Permission to use, copy and modify this software and its documentation without
// fee for educational, research and non-profit purposes, is hereby granted, provided
// that the above copyright notice and the following three paragraphs appear in all copies.

// To request permission to incorporate this software into commercial products contact:
// Vice President of Marketing and Business Development;
// Mitsubishi Electric Research Laboratories (MERL), 201 Broadway, Cambridge, MA 02139 or
// <license@merl.com>.

// IN NO EVENT SHALL MERL BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL,
// OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND
// ITS DOCUMENTATION, EVEN IF MERL HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.

// MERL SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED
// HEREUNDER IS ON AN "AS IS" BASIS, AND MERL HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT,
// UPDATES, ENHANCEMENTS OR MODIFICATIONS.

#define MERL_SAMPLING_RES_THETA_H   90
#define MERL_SAMPLING_RES_THETA_D   90
#define MERL_SAMPLING_RES_PHI_D    360

#define MERL_RED_SCALE   (1.00 / 1500.0)
#define MERL_GREEN_SCALE (1.15 / 1500.0)
#define MERL_BLUE_SCALE  (1.66 / 1500.0)

//------------------------------------------------------------------------------
// Lookup theta_half index
// This is a non-linear mapping!
// In:  [0 .. pi/2]
// Out: [0 .. 89]
static int
theta_half_index(float_t theta_half)
{
	if (theta_half <= 0)
		return 0;
	float_t theta_half_deg = ((theta_half / (m_pi()/2)) * MERL_SAMPLING_RES_THETA_H);
	float_t temp = sqrt(theta_half_deg * MERL_SAMPLING_RES_THETA_H);
	int ret_val = (int)temp;

	if (ret_val < 0) {
		ret_val = 0;
	} else if (ret_val >= MERL_SAMPLING_RES_THETA_H) {
		ret_val = MERL_SAMPLING_RES_THETA_H - 1;
	}

	return ret_val;
}

//------------------------------------------------------------------------------
// Lookup theta_diff index
// In:  [0 .. pi/2]
// Out: [0 .. 89]
static int
theta_diff_index(float_t theta_diff)
{
	int tmp = theta_diff / (m_pi() / 2) * MERL_SAMPLING_RES_THETA_D;
	if (tmp < 0)
		return 0;
	else if (tmp < MERL_SAMPLING_RES_THETA_D - 1)
		return tmp;
	else
		return MERL_SAMPLING_RES_THETA_D - 1;
}

//------------------------------------------------------------------------------
// Lookup phi_diff index
static int
phi_diff_index(float_t phi_diff)
{
	// Because of reciprocity, the BRDF is unchanged under
	// phi_diff -> phi_diff + m_pi()
	if (phi_diff < 0)
		phi_diff += m_pi();

	// In: phi_diff in [0 .. pi]
	// Out: tmp in [0 .. 179]
	int tmp = phi_diff / m_pi() * MERL_SAMPLING_RES_PHI_D / 2;
	if (tmp < 0)
		return 0;
	else if (tmp < MERL_SAMPLING_RES_PHI_D / 2 - 1)
		return tmp;
	else
		return MERL_SAMPLING_RES_PHI_D / 2 - 1;
}
// XXX End of
// Copyright 2005 Mitsubishi Electric Research Laboratories All Rights Reserved.

//------------------------------------------------------------------------------
// MERL Contructor
merl::merl(const char *path_to_file)
{
	std::fstream f(path_to_file, std::fstream::in | std::fstream::binary);
	int32_t n, dims[3];

	// check file
	if (!f.is_open())
		throw exc("djb_error: Failed to open %s\n", path_to_file);

	// read header
	f.read((char *)dims, /*bytes*/4 * 3);
	n = dims[0] * dims[1] * dims[2];
	if (n <= 0)
		throw exc("djb_error: Failed to read MERL header\n");

	// allocate brdf and read data
	m_samples.resize(3 * n);
	f.read((char *)&m_samples[0], sizeof(double) * 3 * n);
	if (f.fail())
		throw exc("djb_error: Reading %s failed\n", path_to_file);
}

//------------------------------------------------------------------------------
// look up the BRDF.
brdf::value_type merl::eval(const vec3 &wi, const vec3 &wo, const void *) const
{
	if (wi.z > 0 && wo.z > 0) {
		// convert to half / diff angle coordinates
		vec3 wh, wd;
		float_t th, ph, td, pd;
		io_to_hd(wi, wo, &wh, &wd);
		xyz_to_theta_phi(wh, &th, &ph);
		xyz_to_theta_phi(wd, &td, &pd);

		// compute indexes
		int idx_r = phi_diff_index(pd)
		          + theta_diff_index(td)
		          * MERL_SAMPLING_RES_PHI_D / 2
		          + theta_half_index(th)
		          * MERL_SAMPLING_RES_PHI_D / 2
		          * MERL_SAMPLING_RES_THETA_D;
		int idx_g = idx_r + MERL_SAMPLING_RES_THETA_H
		          * MERL_SAMPLING_RES_THETA_D
		          * MERL_SAMPLING_RES_PHI_D / 2;
		int idx_b = idx_r + MERL_SAMPLING_RES_THETA_H
		          * MERL_SAMPLING_RES_THETA_D
		          * MERL_SAMPLING_RES_PHI_D;

		// get color
		vec3 rgb;
		rgb.x = m_samples[idx_r] * MERL_RED_SCALE;
		rgb.y = m_samples[idx_g] * MERL_GREEN_SCALE;
		rgb.z = m_samples[idx_b] * MERL_BLUE_SCALE;

		if (rgb.x < 0 || rgb.y < 0 || rgb.z < 0) {
#ifndef NVERBOSE
			DJB_LOG("djb_verbose: below horizon\n");
#endif
			return zero_value();
		}
		rgb*= wo.z;

		return brdf::value_type(&rgb[0], 3);
	}

	return zero_value();
}

// *****************************************************************************
// UTIA API implementation (based on Jiri Filip's implementation)

#define DJB__UTIA_STEP_T  (float_t)15.0
#define DJB__UTIA_STEP_P  (float_t)7.5
#define DJB__UTIA_NTI      6
#define DJB__UTIA_NPI     48
#define DJB__UTIA_NTV      6
#define DJB__UTIA_NPV     48
#define DJB__UTIA_PLANES   3

//------------------------------------------------------------------------------
// Read UTIA BRDF data
utia::utia(const char *filename)
{
	// open file
	std::fstream f(filename, std::fstream::in | std::fstream::binary);
	if (!f.is_open())
		throw exc("djb_error: Failed to open %s\n", filename);

	// allocate memory
	int cnt = DJB__UTIA_PLANES * DJB__UTIA_NTI * DJB__UTIA_NPI
	        * DJB__UTIA_NTV * DJB__UTIA_NPV;
	m_samples.resize(cnt);

	// read data
	f.read((char *)&m_samples[0], sizeof(double) * cnt);

	// normalize
	m_norm = 1.0;
	normalize();
	if (f.fail())
		throw exc("djb_error: Reading %s failed\n", filename);
}

//------------------------------------------------------------------------------
// Look up the UTIA BRDF
brdf::value_type
utia::eval(const vec3 &wi, const vec3 &wo, const void *) const
{
	float_t r2d = 180 / m_pi();
	float_t theta_i = r2d * acos(wi.z);
	float_t theta_o = r2d * acos(wo.z);
	float_t phi_i = r2d * atan2(wi.y, wi.x);
	float_t phi_o = r2d * atan2(wo.y, wo.x);

	// make sure we're above horizon
	if (theta_i >= 90 || theta_o >= 90)
		return zero_value();

	// make sure phi is in [0, 360)
	while (phi_i < 0) {phi_i+= 360;}
	while (phi_o < 0) {phi_o+= 360;}
	while (phi_i >= 360) {phi_i-= 360;}
	while (phi_o >= 360) {phi_o-= 360;}

	int iti[2], itv[2], ipi[2], ipv[2];
	iti[0] = (int)floor(theta_i / DJB__UTIA_STEP_T);
	iti[1] = iti[0] + 1;
	if(iti[0] > DJB__UTIA_NTI - 2) {
		iti[0] = DJB__UTIA_NTI - 2;
		iti[1] = DJB__UTIA_NTI - 1;
	}
	itv[0] = (int)floor(theta_o / DJB__UTIA_STEP_T);
	itv[1] = itv[0] + 1;
	if(itv[0] > DJB__UTIA_NTV - 2) {
		itv[0] = DJB__UTIA_NTV - 2;
		itv[1] = DJB__UTIA_NTV - 1;
	}

	ipi[0] = (int)floor(phi_i / DJB__UTIA_STEP_P);
	ipi[1] = ipi[0] + 1;
	ipv[0] = (int)floor(phi_o / DJB__UTIA_STEP_P);
	ipv[1] = ipv[0] + 1;

	float_t sum;
	float_t wti[2], wtv[2], wpi[2], wpv[2];
	wti[1] = theta_i - (float_t)(DJB__UTIA_STEP_T * iti[0]);
	wti[0] = (float_t)(DJB__UTIA_STEP_T * iti[1]) - theta_i;
	sum = wti[0] + wti[1];
	wti[0]/= sum;
	wti[1]/= sum;
	wtv[1] = theta_o - (float_t)(DJB__UTIA_STEP_T * itv[0]);
	wtv[0] = (float_t)(DJB__UTIA_STEP_T * itv[1]) - theta_o;
	sum = wtv[0] + wtv[1];
	wtv[0]/= sum;
	wtv[1]/= sum;

	wpi[1] = phi_i - (float_t)(DJB__UTIA_STEP_P * ipi[0]);
	wpi[0] = (float_t)(DJB__UTIA_STEP_P * ipi[1]) - phi_i;
	sum = wpi[0] + wpi[1];
	wpi[0]/= sum;
	wpi[1]/= sum;
	wpv[1] = phi_o - (float_t)(DJB__UTIA_STEP_P * ipv[0]);
	wpv[0] = (float_t)(DJB__UTIA_STEP_P * ipv[1]) - phi_o;
	sum = wpv[0] + wpv[1];
	wpv[0]/= sum;
	wpv[1]/= sum;

	if(ipi[1] == DJB__UTIA_NPI)
		ipi[1] = 0;
	if(ipv[1] == DJB__UTIA_NPV)
		ipv[1] = 0;

	int nc = DJB__UTIA_NPV * DJB__UTIA_NTV;
	int nr = DJB__UTIA_NPI * DJB__UTIA_NTI;
	float_t RGB[DJB__UTIA_PLANES];
	for(int isp = 0; isp < DJB__UTIA_PLANES; ++isp) {
		int i, j, k, l;

		RGB[isp] = 0;
		for(i = 0; i < 2; ++i)
		for(j = 0; j < 2; ++j)
		for(k = 0; k < 2; ++k)
		for(l = 0; l < 2; ++l) {
			float_t w = wti[i] * wtv[j] * wpi[k] * wpv[l];
			int idx = isp * nr * nc + nc * (DJB__UTIA_NPI * iti[i] + ipi[k]) 
			        + DJB__UTIA_NPV * itv[j] + ipv[l];

			RGB[isp]+= w * (float_t)m_samples[idx];
		}
		if (RGB[isp] > (float_t)0.0375)
			RGB[isp] = pow((RGB[isp] + (float_t)0.055)/(float_t)1.055, (float_t)2.4);
		else
			RGB[isp]/= (float_t)12.92;
		RGB[isp]*= 100;
	}

	vec3 rgb = wo.z * vec3(
		max((float_t)0, RGB[0]),
		max((float_t)0, RGB[1]),
		max((float_t)0, RGB[2])
	);

	return brdf::value_type(&rgb[0], 3);
}

//------------------------------------------------------------------------------
// Some UTIA BRDFs contain negative values for some materials. Therefore, we 
// clamp them. The data also needs to be scaled.
void utia::normalize()
{
	// clamp to zero
	for (int i = 0; i < (int)m_samples.size(); ++i) {
#ifndef NVERBOSE
		if (m_samples[i] < 0.0)
			DJB_LOG("djb_verbose: negative UTIA BRDF value found, set to 0\n");
#endif
		m_samples[i] = max(0.0, m_samples[i]);
	}

	// scale
	double k = /* Magic constant provided by Jiri Filip */ 1 / 140.0;
	for (int i = 0; i < (int)m_samples.size(); ++i)
		m_samples[i]*= k;
}

// *************************************************************************************************
// Shifted Gamma Distribution API implementation

const sgd::data sgd::s_data[] = {
	{ "alum-bronze", "alum-bronze", { 0.0478786, 0.0313514, 0.0200638 }, { 0.0364976, 0.664975, 0.268836 }, { 0.014832, 0.0300126, 0.0490339 }, { 0.459076, 0.450056, 0.529272 }, { 6.05524, 0.235756, 0.580647 }, { 5.05524, 0.182842, 0.476088 }, { 46.3841, 24.5961, 14.8261 }, { 2.60672, 2.97371, 2.7827 }, { 1.12717e-07, 1.06401e-07, 5.27952e-08 }, { 47.783, 36.2767, 31.6066 }, { 0.205635, 0.066289, -0.0661091 }, { 0.100735, 0.0878706, 0.0861907 } },
	{ "alumina-oxide", "alumina-oxide", { 0.316358, 0.292248, 0.25416 }, { 0.00863128, 0.00676832, 0.0103309 }, { 0.000159222, 0.000139421, 0.000117714 }, { 0.377727, 0.318496, 0.402598 }, { 0.0300766, 1.70375, 1.96622 }, { -0.713784, 0.70375, 1.16019 }, { 4381.96, 5413.74, 5710.42 }, { 3.31076, 4.93831, 2.84538 }, { 6.72897e-08, 1.15769e-07, 6.32199e-08 }, { 354.275, 367.448, 414.581 }, { 0.52701, 0.531166, 0.53301 }, { 0.213276, 0.147418, 0.27746 } },
	{ "aluminium", "aluminium", { 0.0305166, 0.0358788, 0.0363463 }, { 0.0999739, 0.131797, 0.0830361 }, { 0.0012241, 0.000926487, 0.000991844 }, { 0.537669, 0.474562, 0.435936 }, { 0.977854, 0.503108, 1.77905 }, { -0.0221457, -0.0995445, 0.77905 }, { 449.321, 658.044, 653.86 }, { 8.2832e-07, 9.94692e-08, 6.11887e-08 }, { 3.54592e-07, 16.0175, 15.88 }, { 23.8656, 10.6911, 9.69801 }, { -0.510356, 0.570179, 0.566156 }, { 0.303567, 0.232628, 0.441578 } },
	{ "aventurnine", "aventurnine", { 0.0548217, 0.0621179, 0.0537826 }, { 0.0348169, 0.0872381, 0.111961 }, { 0.000328039, 0.000856166, 0.00145342 }, { 0.387167, 0.504525, 0.652122 }, { 0.252033, 0.133897, 0.087172 }, { 0.130593, 0.0930416, 0.0567429 }, { 2104.51, 676.157, 303.59 }, { 3.12126, 2.50965e-07, 2.45778e-05 }, { 1.03849e-07, 8.53824e-07, 3.20722e-07 }, { 251.265, 24.2886, 29.0236 }, { 0.510125, -0.41764, -0.245097 }, { 0.0359759, 0.0297523, 0.0285881 } },
	{ "beige-fabric", "fabric-beige", { 0.20926, 0.160666, 0.145337 }, { 0.121663, 0.0501577, 0.00177279 }, { 0.39455, 0.15975, 0.110706 }, { 0.474725, 0.0144728, 1.70871e-12 }, { 0.0559459, 0.222268, 8.4764 }, { -0.318718, -0.023826, 7.4764 }, { 3.8249, 7.32453, 10.0904 }, { 2.26283, 2.97144, 3.55311 }, { 0.0375346, 0.073481, 0.0740222 }, { 7.52635, 9.05672, 10.6185 }, { 0.217453, 0.407084, 0.450203 }, { 0.00217528, 0.00195262, 0.00171008 } },
	{ "black-fabric", "fabric-black", { 0.0189017, 0.0112353, 0.0110067 }, { 2.20654e-16, 6.76197e-15, 1.57011e-13 }, { 0.132262, 0.128044, 0.127838 }, { 0.189024, 0.18842, 0.188426 }, { 1, 1, 1 }, { 0, 0, 0 }, { 8.1593, 8.38075, 8.39184 }, { 3.83017, 3.89536, 3.89874 }, { 0.00415117, 0.00368324, 0.00365826 }, { 12.9974, 13.2597, 13.2737 }, { 0.207997, 0.205597, 0.205424 }, { 0.000363154, 0.000272253, 0.000274773 } },
	{ "black-obsidian", "black-obsidian", { 0.00130399, 0.0011376, 0.00107233 }, { 0.133029, 0.125362, 0.126188 }, { 0.000153649, 0.000148939, 0.000179285 }, { 0.186234, 0.227495, 0.25745 }, { 2.42486e-12, 0.0174133, 0.091766 }, { -0.0800755, -0.048671, 0.0406445 }, { 5668.57, 5617.79, 4522.84 }, { 13.7614, 8.59526, 6.44667 }, { 1e+38, 1e+38, 1e+38 }, { 117.224, 120.912, 113.366 }, { 1.19829, 1.19885, 1.19248 }, { 0.0841374, 0.0943085, 0.123514 } },
	{ "black-oxidized-steel", "black-oxidized-steel", { 0.0149963, 0.0120489, 0.0102471 }, { 0.373438, 0.344382, 0.329202 }, { 0.187621, 0.195704, 0.200503 }, { 0.661367, 0.706913, 0.772267 }, { 0.0794166, 0.086518, 0.080815 }, { 0.0470402, 0.0517633, 0.0455037 }, { 5.1496, 4.91636, 4.69009 }, { 4.0681, 3.95489, 3.71052 }, { 1.07364e-07, 1.05341e-07, 1.16556e-07 }, { 20.2383, 20.1786, 20.2553 }, { -0.479617, -0.4885, -0.478388 }, { 0.00129576, 0.0011378, 0.000986163 } },
	{ "black-phenolic", "black-phenolic", { 0.00204717, 0.00196935, 0.00182908 }, { 0.177761, 0.293146, 0.230592 }, { 0.00670804, 0.00652009, 0.00656043 }, { 0.706648, 0.677776, 0.673986 }, { 0.16777, 0.12335, 0.166663 }, { 0.111447, 0.0927321, 0.125663 }, { 65.4189, 70.8936, 70.9951 }, { 1.06318, 1.15283, 1.16529 }, { 1.24286e-07, 3.00039e-08, 9.77334e-08 }, { 74.0711, 75.1165, 73.792 }, { 0.338204, 0.319306, 0.33434 }, { 0.0307129, 0.0531183, 0.0454238 } },
	{ "black-soft-plastic", "black-plastic-soft", { 0.00820133, 0.00777718, 0.00764537 }, { 0.110657, 0.0980322, 0.100579 }, { 0.0926904, 0.0935964, 0.0949975 }, { 0.14163, 0.148703, 0.143694 }, { 0.150251, 0.169418, 0.170457 }, { 0.100065, 0.113089, 0.114468 }, { 11.2419, 11.113, 10.993 }, { 4.3545, 4.3655, 4.31586 }, { 0.00464641, 0.00384785, 0.0046145 }, { 14.6751, 14.8089, 14.5436 }, { 0.275651, 0.262317, 0.271284 }, { 0.000527402, 0.000550255, 0.000527718 } },
	{ "blue-acrylic", "acrylic-blue", { 0.0134885, 0.0373766, 0.10539 }, { 0.0864901, 0.0228191, 0.204042 }, { 0.000174482, 0.000269795, 0.0015211 }, { 0.373948, 0.362425, 0.563636 }, { 0.0185562, 0.399982, 0.0525861 }, { -0.0209713, 0.241543, 0.0169474 }, { 4021.24, 2646.36, 346.898 }, { 3.38722, 3.62885, 1.83684e-06 }, { 9.64334e-08, 9.96105e-08, 3.61787e-07 }, { 338.073, 272.828, 23.5039 }, { 0.526039, 0.515404, -0.526935 }, { 0.0612235, 0.0789826, 0.0461093 } },
	{ "blue-fabric", "fabric-blue", { 0.0267828, 0.0281546, 0.066668 }, { 0.0825614, 0.0853369, 0.0495164 }, { 0.248706, 0.249248, 0.18736 }, { 9.23066e-13, 1.66486e-12, 2.27218e-12 }, { 0.201626, 0.213723, 0.56548 }, { 0.225891, 0.226267, 0.638493 }, { 5.15615, 5.14773, 6.43713 }, { 2.25846, 2.25536, 2.68382 }, { 0.128037, 0.128363, 0.0944915 }, { 6.95531, 6.94633, 8.17665 }, { 0.407528, 0.407534, 0.411378 }, { 0.000323043, 0.00032064, 0.000653631 } },
	{ "blue-metallic-paint2", "ch-ball-blue-metallic", { 0.010143, 0.0157349, 0.0262717 }, { 0.0795798, 0.0234493, 0.0492337 }, { 0.00149045, 0.00110477, 0.00141008 }, { 0.624615, 0.598721, 0.67116 }, { 9.36434e-14, 3.61858e-15, 1.15633e-14 }, { -0.210234, -1, -1 }, { 314.024, 441.812, 299.726 }, { 1.20935e-05, 7.51792e-06, 3.86474e-05 }, { 3.38901e-07, 2.94502e-07, 3.15718e-07 }, { 27.0491, 28.576, 30.6214 }, { -0.326593, -0.274443, -0.187842 }, { 0.0908879, 0.163236, 0.286541 } },
	{ "blue-metallic-paint", "metallic-blue", { 0.00390446, 0.00337319, 0.00848198 }, { 0.0706771, 0.0415082, 0.104423 }, { 0.155564, 0.139, 0.15088 }, { 1.01719, 1.02602, 1.16153 }, { 0.149347, 0.153181, 1.87241e-14 }, { -0.487331, -0.76557, -1 }, { 4.4222, 4.59265, 3.93929 }, { 2.54345, 2.33884, 2.24405 }, { 6.04906e-08, 5.81858e-08, 1.2419e-07 }, { 23.9533, 25.0641, 24.6856 }, { -0.34053, -0.294595, -0.258117 }, { 0.00202544, 0.00246268, 0.0059725 } },
	{ "blue-rubber", "blue-rubber", { 0.0371302, 0.0732915, 0.146637 }, { 0.384232, 0.412357, 0.612608 }, { 0.218197, 0.2668, 0.478375 }, { 0.815054, 1.00146, 1.24995 }, { 0.0631713, 0.0622636, 0.0399196 }, { 0.0478254, 0.0422186, 0.007015 }, { 4.41586, 3.76795, 3.46276 }, { 3.77807, 3.82679, 3.33186 }, { 1.2941e-07, 1.07194e-07, 0.00045665 }, { 19.8046, 19.3115, 11.4364 }, { -0.499472, -0.557706, -0.172177 }, { 0.000630461, 0.000733287, 0.00171092 } },
	{ "brass", "brass", { 0.0301974, 0.0223812, 0.0139381 }, { 0.0557826, 0.0376687, 0.0775998 }, { 0.0002028, 0.000258468, 0.00096108 }, { 0.362322, 0.401593, 0.776606 }, { 0.639886, 0.12354, 0.0197853 }, { -0.360114, -0.87646, -0.0919344 }, { 3517.61, 2612.49, 331.815 }, { 3.64061, 2.87206, 0.529487 }, { 1.01146e-07, 9.83073e-08, 5.48819e-08 }, { 312.802, 283.431, 183.091 }, { 0.522711, 0.516719, 0.474834 }, { 0.440765, 0.200948, 0.15549 } },
	{ "cherry-235", "cherry-235", { 0.0497502, 0.0211902, 0.0120688 }, { 0.166001, 0.202786, 0.165189 }, { 0.0182605, 0.0277997, 0.0255721 }, { 0.0358348, 0.163231, 0.129135 }, { 0.0713408, 0.0571719, 0.0791809 }, { 0.0200814, 0.00887306, 0.0251675 }, { 54.7448, 33.7294, 37.36 }, { 6.11314, 6.23697, 6.16351 }, { 30.3886, 0.00191869, 0.0495069 }, { 18.8114, 25.8454, 23.3753 }, { 0.816378, 0.387479, 0.522125 }, { 0.00235908, 0.00199931, 0.00155902 } },
	{ "chrome", "chrome", { 0.00697189, 0.00655268, 0.0101854 }, { 0.0930656, 0.041946, 0.104558 }, { 0.00013, 0.0002, 6e-5 }, { 0.3, 0.36, 0.26 }, { 0.256314, 0.819565, 3.22085e-13 }, { -0.743686, -0.180435, -1 }, { 2200, 2150, 4100 }, { 3.8545, 4.44817, 5.40959 }, { 5.30781e-08, 1.04045e-07, 1.8e+10 }, { 354.965, 349.356, 457 }, { 0.526796, 0.528469, 1.00293 }, { 0.802138, 1.29121, 1.06148 } },
	{ "chrome-steel", "chrome-steel", { 0.0206718, 0.0240818, 0.024351 }, { 0.129782, 0.109032, 0.0524555 }, { 5.51292e-05, 3.13288e-05, 4.51944e-05 }, { 0.207979, 0.152758, 0.325431 }, { 1.18818e-12, 2.06813e-11, 0.580895 }, { -0.316807, -0.265326, -0.419105 }, { 15466.8, 28628.9, 16531.2 }, { 12.8988, 68.7898, 4.68237 }, { 1e+10, 1e+10, 44.7025 }, { 510, 665, 618.155 }, { 1.20035, 1.2003, 0.579562 }, { 0.552277, 0.448267, 1.12869 } },
	{ "colonial-maple-223", "colonial-maple-223", { 0.100723, 0.0356306, 0.0162408 }, { 0.059097, 0.0661341, 0.11024 }, { 0.0197628, 0.0279336, 0.0621265 }, { 0.0311867, 0.112022, 0.344348 }, { 0.0576683, 0.0617498, 0.0479061 }, { -0.0503364, -0.0382196, -0.0382636 }, { 50.7952, 34.6835, 14.201 }, { 6.03342, 6.01053, 4.56588 }, { 18.9034, 0.087246, 1.03757e-07 }, { 18.3749, 21.6159, 26.9347 }, { 0.801289, 0.544191, -0.139944 }, { 0.00229125, 0.00193164, 0.0013723 } },
	{ "color-changing-paint1", "", { 0.00513496, 0.00500415, 0.00296872 }, { 1.53167, 0.430731, 1.48308 }, { 0.00320129, 0.0023053, 0.0329464 }, { 0.167301, 0.100003, 1.04868 }, { 0.0208525, 0.109366, 0.0553211 }, { 0.0101418, 0.0801275, 0.0357004 }, { 279.058, 407.716, 9.71061 }, { 6.73768, 7.19333, 1.04729 }, { 2.66398e+08, 8.98323e+10, 4.16363e-08 }, { 36.7257, 27.8164, 45.5388 }, { 1.01208, 1.19549, 0.131148 }, { 0.164144, 0.0515527, 0.116602 } },
	{ "color-changing-paint2", "", { 0.00463528, 0.00544054, 0.0070818 }, { 1.35172, 1.47838, 1.29831 }, { 0.0279961, 0.0267135, 0.0257468 }, { 0.720154, 0.717648, 0.694662 }, { 0.019073, 0.00825302, 0.0301024 }, { -0.0181694, -0.0198592, -0.000522292 }, { 18.6722, 19.4709, 20.7514 }, { 1.56832, 1.554, 1.60402 }, { 1.19918e-07, 9.95358e-08, 9.69838e-08 }, { 41.295, 42.1716, 42.3986 }, { 0.124794, 0.129237, 0.132024 }, { 0.0489296, 0.0499662, 0.054555 } },
	{ "color-changing-paint3", "", { 0.00305737, 0.00257341, 0.00263616 }, { 0.880793, 0.691268, 0.707821 }, { 0.014742, 0.0135513, 0.0108894 }, { 0.537248, 0.572188, 0.457665 }, { 0.055479, 0.0666783, 0.0585094 }, { 0.0315282, 0.0457554, 0.0365558 }, { 42.1798, 43.3684, 62.0624 }, { 2.06002, 1.82703, 2.54086 }, { 5.0531e-08, 9.99088e-08, 5.726e-08 }, { 50.0948, 52.0344, 54.643 }, { 0.197971, 0.229219, 0.241049 }, { 0.0484699, 0.0342594, 0.0347198 } },
	{ "dark-blue-paint", "dark-blue-paint", { 0.00665057, 0.0139696, 0.0472605 }, { 0.231099, 0.18931, 0.12528 }, { 0.130681, 0.112103, 0.0629285 }, { 0.238562, 0.17, 0.0157371 }, { 0.112486, 0.106018, 0.139316 }, { 0.0662142, 0.0682314, 0.10607 }, { 8.0828, 9.4312, 16.8231 }, { 4.05526, 4.09607, 4.54368 }, { 0.00104743, 0.0035546, 0.10813 }, { 14.347, 13.928, 13.5626 }, { 0.11894, 0.226752, 0.524826 }, { 0.000406243, 0.000291986, 0.000555778 } },
	{ "dark-red-paint", "dark-red-paint", { 0.237125, 0.0365577, 0.0106149 }, { 0.227405, 0.111055, 0.150433 }, { 0.67372, 0.0706716, 0.20328 }, { 1.38127, 1.4345e-13, 0.27723 }, { 1.56101e-11, 0.0779523, 0.136566 }, { -0.148315, 0.0560349, 0.0961999 }, { 3.95965, 15.1861, 5.71355 }, { 2.27942, 4.30359, 3.20838 }, { 0.0225467, 0.113039, 0.00667899 }, { 7.45998, 12.679, 10.9071 }, { 0.119045, 0.521154, 0.166378 }, { 0.00155397, 0.000508377, 0.000218865 } },
	{ "dark-specular-fabric", "", { 0.0197229, 0.00949167, 0.00798414 }, { 0.556218, 0.401495, 0.378651 }, { 0.140344, 0.106541, 0.166715 }, { 0.249059, 0.177611, 0.434167 }, { 0.0351133, 0.0387177, 0.0370533 }, { 0.0243153, 0.0293178, 0.0264913 }, { 7.60492, 9.81673, 6.19307 }, { 3.93869, 4.23097, 4.3775 }, { 0.00122421, 0.00238545, 8.47126e-06 }, { 13.889, 14.5743, 17.2049 }, { 0.114655, 0.210179, -0.227628 }, { 0.00158681, 0.000974676, 0.000638865 } },
	{ "delrin", "delrin", { 0.272703, 0.249805, 0.220642 }, { 0.536593, 0.727886, 0.64011 }, { 0.176535, 0.344018, 0.208011 }, { 0.762088, 0.823603, 0.85976 }, { 0.0398465, 0.0430719, 0.0290162 }, { -0.0121332, -0.0255915, -0.0305128 }, { 5.02017, 3.78669, 4.39454 }, { 3.44325, 3.49289, 3.48886 }, { 9.20654e-08, 0.000215686, 1.06338e-07 }, { 21.2534, 12.5556, 20.455 }, { -0.438862, -0.184248, -0.478699 }, { 0.00493658, 0.00655617, 0.00422698 } },
	{ "fruitwood-241", "fruitwood-241", { 0.0580445, 0.0428667, 0.0259801 }, { 0.203894, 0.233494, 0.263882 }, { 0.00824986, 0.0534794, 0.0472951 }, { 0.160382, 1.07206, 0.768335 }, { 0.00129482, 0.00689891, 0.01274 }, { -0.0211778, -0.0140791, -0.00665974 }, { 110.054, 6.98485, 11.6203 }, { 6.74678, 1.31986, 1.76021 }, { 162.121, 1.18843e-07, 1.0331e-07 }, { 32.6966, 36.752, 34.371 }, { 0.765181, 0.0514459, 0.0106852 }, { 0.00493613, 0.00460352, 0.00373027 } },
	{ "gold-metallic-paint2", "ch-ball-gold-metallic2", { 0.0796008, 0.0538361, 0.0649523 }, { 0.633627, 1.77116, 0.0564028 }, { 0.00376608, 0.00871206, 0.000572055 }, { 0.415684, 0.368424, 0.623038 }, { 0.0343265, 0.00330259, 4.15759e-12 }, { -0.00929705, -0.0219437, -0.0596944 }, { 181.769, 85.527, 795.042 }, { 2.77279, 3.50114, 0.985212 }, { 4.78294e-08, 9.8399e-08, 4.45358e-08 }, { 83.871, 57.2291, 216.537 }, { 0.365256, 0.276734, 0.491264 }, { 0.117145, 0.17457, 0.0754722 } },
	{ "gold-metallic-paint3", "ch-ball-gold-metallic", { 0.0579212, 0.0416649, 0.0271208 }, { 0.0729896, 0.0597695, 0.037684 }, { 0.00146432, 0.00156513, 0.000977438 }, { 0.529437, 0.551234, 0.504486 }, { 1.84643e-14, 5.7212e-15, 8.68546e-13 }, { -1, -1, -0.648897 }, { 382.887, 345.197, 593.725 }, { 4.97078e-07, 1.12888e-06, 4.15366e-07 }, { 3.98586e-07, 3.69533e-07, 16.0596 }, { 22.0196, 22.6462, 11.7126 }, { -0.634103, -0.588056, 0.578355 }, { 0.116233, 0.0897222, 0.0711514 } },
	{ "gold-metallic-paint", "metallic-gold", { 0.0178625, 0.00995704, 0.00335044 }, { 0.17127, 0.120714, 0.115473 }, { 0.127954, 0.127825, 0.109623 }, { 0.781093, 0.795517, 0.661313 }, { 1.00776e-12, 1.22243e-15, 6.18432e-13 }, { -1, -1, -0.334497 }, { 5.90039, 5.83556, 7.14985 }, { 2.7548, 2.7057, 2.94348 }, { 9.46481e-08, 1.06951e-07, 1.10733e-07 }, { 23.8811, 23.9059, 24.2972 }, { -0.303345, -0.293778, -0.267019 }, { 0.0095612, 0.00483452, 0.00145599 } },
	{ "gold-paint", "gold-paint", { 0.147708, 0.0806975, 0.033172 }, { 0.160592, 0.217282, 0.236425 }, { 0.122506, 0.108069, 0.12187 }, { 0.795078, 0.637578, 0.936117 }, { 9.16095e-12, 1.81225e-12, 0.0024589 }, { -0.596835, -0.331147, -0.140729 }, { 5.98176, 7.35539, 5.29722 }, { 2.64832, 3.04253, 2.3013 }, { 9.3111e-08, 8.80143e-08, 9.65288e-08 }, { 24.3593, 24.4037, 25.3623 }, { -0.284195, -0.277297, -0.245352 }, { 0.00313716, 0.00203922, 0.00165683 } },
	{ "gray-plastic", "gray-plastic", { 0.103233, 0.104428, 0.0983734 }, { 0.494656, 0.517207, 0.52772 }, { 0.00758705, 0.00848095, 0.00887135 }, { 0.557908, 0.556548, 0.545887 }, { 0.0428175, 0.0438899, 0.0569098 }, { 0.0208304, 0.0221893, 0.0375763 }, { 75.6274, 68.3098, 66.5689 }, { 1.72111, 1.76597, 1.8406 }, { 1.06783e-07, 5.31845e-08, 6.53296e-08 }, { 65.6538, 63.2018, 61.5805 }, { 0.308907, 0.283768, 0.280211 }, { 0.0512156, 0.050844, 0.0505763 } },
	{ "grease-covered-steel", "", { 0.0196306, 0.0200926, 0.0187026 }, { 0.0433721, 0.0311621, 0.0326401 }, { 0.00019081, 0.000173919, 0.000217638 }, { 0.164569, 0.141125, 0.219421 }, { 7.12672e-13, 1.06789e-14, 1.61131e-13 }, { -1, -1, -1 }, { 4655.4, 5210.63, 3877.45 }, { 17.2827, 29.2523, 8.54352 }, { 1e+38, 1e+38, 1e+38 }, { 103.46, 105.572, 99.1097 }, { 1.20022, 1.20678, 1.19961 }, { 0.261184, 0.20789, 0.210192 } },
	{ "green-acrylic", "acrylic-green", { 0.0176527, 0.0761863, 0.0432331 }, { 0.0517555, 0.15899, 0.0754193 }, { 0.000185443, 5.19959e-05, 7.95188e-05 }, { 0.288191, 0.170979, 0.145492 }, { 0.418137, 0.0486445, 0.170328 }, { 0.330999, 0.0294954, 0.110218 }, { 4223.98, 16977.4, 11351.3 }, { 5.6545, 26.1353, 1.54193e+07 }, { 1e+38, 1e+38, 4.23355e+06 }, { 172.888, 204.112, 403.267 }, { 0.995381, 1.19417, 0.646645 }, { 0.138255, 0.168884, 0.116563 } },
	{ "green-fabric", "fabric-green", { 0.0511324, 0.0490447, 0.0577457 }, { 0.043898, 0.108081, 0.118528 }, { 0.0906425, 0.14646, 0.125546 }, { 0.199121, 0.21946, 0.130311 }, { 0.117671, 0.0797822, 0.0840896 }, { 0.107501, 0.0628391, 0.0668466 }, { 11.1681, 7.43507, 8.69777 }, { 4.65909, 3.72793, 3.72472 }, { 0.00055264, 0.00331292, 0.0119365 }, { 16.8276, 12.7802, 12.1538 }, { 0.153958, 0.17378, 0.291679 }, { 0.000577582, 0.000635885, 0.000729507 } },
	{ "green-latex", "fabric-green-latex", { 0.0885476, 0.13061, 0.0637004 }, { 0.177041, 0.16009, 0.101365 }, { 0.241826, 0.21913, 0.2567 }, { 0.175925, 0.162514, 0.326958 }, { 0.0213854, 0.0498004, 0.0677643 }, { -0.0864353, -0.0518848, -0.00668045 }, { 5.17117, 5.55867, 4.84094 }, { 2.61304, 2.75985, 2.84236 }, { 0.0417628, 0.0352726, 0.0128547 }, { 8.4496, 8.91691, 9.57998 }, { 0.296393, 0.295144, 0.18077 }, { 0.000875063, 0.00103454, 0.000568252 } },
	{ "green-metallic-paint2", "ch-ball-green-metallic", { 0.00536389, 0.0147585, 0.0072232 }, { 0.0553207, 0.0656441, 0.0608999 }, { 0.00131834, 0.00140737, 0.00171711 }, { 0.436009, 0.57969, 0.54703 }, { 0.291808, 0.194127, 0.199549 }, { 0.126008, -0.0681937, 0.0148264 }, { 493.87, 362.85, 317.92 }, { 4.96631e-08, 3.41674e-06, 8.72658e-07 }, { 15.5833, 3.60732e-07, 3.78431e-07 }, { 8.31751, 25.0069, 21.7501 }, { 0.561587, -0.431953, -0.657077 }, { 0.019819, 0.0506927, 0.0279691 } },
	{ "green-metallic-paint", "green-metallic-paint", { 0.00368935, 0.0155555, 0.022272 }, { 0.185621, 0.436002, 0.322925 }, { 0.131402, 0.146271, 0.154061 }, { 1.28366, 0.865104, 0.944013 }, { 0.12483, 0.0443223, 0.0955612 }, { 0.028874, -0.118581, -0.139373 }, { 3.62502, 5.13725, 4.71161 }, { 1.97389, 2.72437, 2.63239 }, { 5.5607e-08, 1.1239e-07, 9.76666e-08 }, { 27.5467, 23.1688, 23.2911 }, { -0.204638, -0.326555, -0.334024 }, { 0.00120994, 0.00217841, 0.00218444 } },
	{ "green-plastic", "green-plastic", { 0.015387, 0.0851675, 0.0947402 }, { 0.0607427, 0.156977, 0.125155 }, { 0.000302146, 0.00197038, 0.000690284 }, { 0.373134, 0.751741, 0.505735 }, { 0.116395, 0.0388464, 0.0476683 }, { 0.0123694, -0.0038785, 0.00864048 }, { 2329.55, 181.586, 833.886 }, { 3.39581, 0.000128411, 3.06753e-07 }, { 6.38801e-08, 2.49751e-07, 5.82332e-07 }, { 260.135, 31.8416, 26.4101 }, { 0.510576, -0.156086, -0.337325 }, { 0.0411204, 0.0490786, 0.0385724 } },
	{ "hematite", "hematite", { 0.00948374, 0.0117628, 0.00985037 }, { 0.0705694, 0.118965, 0.115059 }, { 0.000908552, 0.000601576, 0.00184248 }, { 0.515183, 0.498157, 0.73351 }, { 0.235045, 0.0609324, 0.00720315 }, { -0.120178, -0.096611, -0.235146 }, { 626.075, 967.655, 201.871 }, { 3.94735e-07, 3.54792e-07, 0.000101676 }, { 5.04376e-07, 16.4874, 2.78427e-07 }, { 24.7601, 14.7537, 31.4869 }, { -0.431506, 0.577853, -0.162174 }, { 0.0598105, 0.0605776, 0.0805566 } },
	{ "ipswich-pine-221", "ipswich-pine-221", { 0.0560746, 0.0222518, 0.0105117 }, { 0.0991995, 0.106719, 0.110343 }, { 0.014258, 0.0178759, 0.0188163 }, { 0.0625943, 0.113994, 0.118296 }, { 1.55288e-13, 6.595e-12, 3.97788e-13 }, { -0.0675784, -0.0696373, -0.0703103 }, { 68.7248, 53.3521, 50.6148 }, { 6.36482, 6.37738, 6.35839 }, { 111.962, 1.68378, 0.850892 }, { 20.956, 23.9475, 24.0837 }, { 0.842456, 0.66795, 0.641354 }, { 0.00364018, 0.00320748, 0.00300736 } },
	{ "light-brown-fabric", "", { 0.0612259, 0.0263619, 0.0187761 }, { 3.65487e-12, 9.7449e-12, 4.13685e-12 }, { 0.147778, 0.137639, 0.13071 }, { 0.188292, 0.188374, 0.189026 }, { 1, 1, 1 }, { 0, 0, 0 }, { 7.46192, 7.90085, 8.23842 }, { 3.59765, 3.74493, 3.85476 }, { 0.00660661, 0.0049581, 0.00395337 }, { 12.0657, 12.6487, 13.0977 }, { 0.221546, 0.213332, 0.206743 }, { 0.00126964, 0.000811029, 0.000670968 } },
	{ "light-red-paint", "paint-light-red", { 0.391162, 0.0458387, 0.0059411 }, { 0.522785, 0.144057, 0.214076 }, { 0.854048, 0.0627781, 0.205965 }, { 1.40232, 0.101554, 0.73691 }, { 0.0346142, 0.0655657, 0.0725632 }, { -0.0688995, 0.0400933, 0.0252314 }, { 4.68691, 16.2538, 4.71913 }, { 1.72476, 4.88106, 3.96894 }, { 0.0969212, 0.0124952, 1.02202e-07 }, { 5.57044, 16.1561, 19.9884 }, { 0.249637, 0.388724, -0.505514 }, { 0.00271456, 0.000889762, 0.000505975 } },
	{ "maroon-plastic", "maroon-plastic", { 0.189951, 0.0353828, 0.0321504 }, { 0.127693, 0.100703, 0.115731 }, { 0.00160715, 0.00110827, 0.00100127 }, { 0.684406, 0.645917, 0.569111 }, { 0.0479368, 0.0624437, 0.0921161 }, { -0.0134224, 0.00888653, 0.0423901 }, { 257.032, 398.91, 515.363 }, { 4.57088e-05, 2.62643e-05, 3.15876e-06 }, { 3.05979e-07, 2.76112e-07, 3.67984e-07 }, { 29.9497, 31.8569, 27.4899 }, { -0.211122, -0.159036, -0.309239 }, { 0.0285283, 0.0277773, 0.0286259 } },
	{ "natural-209", "natural-209", { 0.0961753, 0.0349012, 0.0121752 }, { 0.0781649, 0.0898869, 0.111321 }, { 0.0137282, 0.0154247, 0.0233645 }, { 0.0491415, 0.0673163, 0.149331 }, { 0.0522205, 0.0456428, 0.0279324 }, { -0.0277529, -0.0308045, -0.0492147 }, { 71.8965, 63.4418, 40.2168 }, { 6.3511, 6.32932, 6.32009 }, { 224.515, 48.774, 0.0171617 }, { 20.2337, 21.0421, 25.7335 }, { 0.874926, 0.811948, 0.483785 }, { 0.00465507, 0.00387214, 0.00295941 } },
	{ "neoprene-rubber", "neoprene-rubber", { 0.259523, 0.220477, 0.184871 }, { 0.275058, 0.391429, 0.0753145 }, { 0.143818, 0.207586, 0.0764912 }, { 0.770284, 0.774203, 0.700644 }, { 0.113041, 0.110436, 0.16895 }, { 0.060346, 0.0565499, 0.0788468 }, { 5.56845, 4.61088, 8.84784 }, { 3.02411, 3.82334, 2.38942 }, { 5.3042e-08, 1.01087e-07, 6.70643e-08 }, { 23.2109, 20.1103, 28.3099 }, { -0.379072, -0.500903, -0.156114 }, { 0.00218754, 0.00314666, 0.00180987 } },
	{ "nickel", "nickel", { 0.0144009, 0.0115339, 0.00989042 }, { 0.157696, 0.293022, 0.450103 }, { 0.00556292, 0.00627392, 0.00660563 }, { 0.171288, 0.168324, 0.161023 }, { 2.21884, 1.61986, 0.931645 }, { 1.21884, 1.22103, 0.698939 }, { 160.907, 143.234, 136.933 }, { 6.76252, 6.76447, 6.76278 }, { 16179.3, 3451.04, 3453.27 }, { 36.1378, 35.1378, 33.614 }, { 0.846825, 0.821209, 0.830948 }, { 0.132612, 0.177506, 0.155358 } },
	{ "nylon", "nylon", { 0.204199, 0.211192, 0.19234 }, { 0.156797, 0.303324, 0.236394 }, { 0.0250344, 0.0436802, 0.0421753 }, { 0.528875, 0.617086, 0.620808 }, { 0.240279, 0.117115, 0.127421 }, { 0.219191, 0.0940848, 0.101339 }, { 26.4669, 14.8484, 15.213 }, { 2.32486, 2.22618, 2.18663 }, { 1.17309e-07, 5.34378e-08, 6.68165e-08 }, { 39.9707, 33.9774, 34.3152 }, { 0.11793, -0.0190508, -0.00222626 }, { 0.00830775, 0.00885046, 0.00645556 } },
	{ "orange-paint", "orange-paint", { 0.368088, 0.147113, 0.00692426 }, { 0.524979, 0.116386, 0.199437 }, { 0.818115, 0.064743, 0.229391 }, { 1.44385, 0.0709512, 0.483597 }, { 6.92565e-13, 0.106161, 0.102279 }, { -0.174318, 0.0934385, 0.0625648 }, { 4.57466, 16.0185, 4.96427 }, { 1.84547, 4.70387, 3.6232 }, { 0.072629, 0.0299825, 0.000333551 }, { 5.96872, 14.9466, 13.2194 }, { 0.222125, 0.438216, -0.0759733 }, { 0.00178442, 0.000789226, 0.000301814 } },
	{ "pearl-paint", "pearl-paint", { 0.181967, 0.159068, 0.143348 }, { 0.105133, 0.0928717, 0.0802367 }, { 0.0724063, 0.0808503, 0.0596139 }, { 0.194454, 0.203296, 0.091958 }, { 0.168966, 0.297431, 0.401185 }, { -0.831034, -0.702569, -0.226546 }, { 13.6335, 12.3113, 17.1256 }, { 5.07525, 4.9097, 4.92468 }, { 0.000252814, 0.000258641, 0.0180128 }, { 18.9547, 18.2047, 16.1366 }, { 0.156731, 0.135808, 0.415562 }, { 0.00244402, 0.00167924, 0.0012928 } },
	{ "pickled-oak-260", "pickled-oak-260", { 0.181735, 0.14142, 0.125486 }, { 0.0283411, 0.0296418, 0.025815 }, { 0.0105853, 0.0102771, 0.0101188 }, { 2.31337e-14, 2.35272e-14, 1.99762e-14 }, { 5.38184e-13, 2.15933e-13, 3.55496e-12 }, { -0.309259, -0.291046, -0.329625 }, { 95.4759, 98.3089, 99.831 }, { 6.37433, 6.39032, 6.39857 }, { 4641.15, 5970.15, 6818.76 }, { 18.1707, 18.2121, 18.2334 }, { 1.00563, 1.01274, 1.01645 }, { 0.00336548, 0.0032891, 0.00330829 } },
	{ "pink-fabric2", "", { 0.24261, 0.0829238, 0.0751196 }, { 0.161823, 0.0591236, 0.00907967 }, { 0.220011, 0.148623, 0.111966 }, { 6.44517e-12, 3.24286e-13, 3.83556e-11 }, { 0.242032, 0.456181, 3.11925 }, { -0.0582931, 0.295844, 2.84268 }, { 5.66376, 7.80657, 9.98941 }, { 2.43742, 3.05804, 3.53392 }, { 0.111404, 0.079067, 0.0738938 }, { 7.47166, 9.23588, 10.5653 }, { 0.408014, 0.422779, 0.448866 }, { 0.00145188, 0.000903357, 0.00103814 } },
	{ "pink-fabric", "fabric-pink", { 0.270553, 0.223977, 0.240993 }, { 0.299998, 0.418074, 4.07112e-13 }, { 0.787023, 0.234345, 0.17346 }, { 1.77629, 4.64947e-15, 0.203846 }, { 0.121124, 0.0271041, 1 }, { 0.0151107, 0.00273969, 0 }, { 4.71724, 5.3941, 6.55218 }, { 2.23471, 2.34423, 3.31887 }, { 0.0266324, 0.11955, 0.00966123 }, { 7.20596, 7.20326, 11.0696 }, { 0.130136, 0.407562, 0.22282 }, { 0.00129262, 0.00124216, 0.00262093 } },
	{ "pink-felt", "fabric-pink-felt", { 0.259533, 0.192978, 0.185581 }, { 0.359813, 0.533498, 0.0390541 }, { 0.46679, 0.203504, 0.314933 }, { 0.663613, 1.70267e-13, 0.778919 }, { 0.0530603, 0.0108612, 1.12976 }, { -0.124124, -0.0455941, 1.05206 }, { 3.6266, 6.02293, 3.92458 }, { 2.22042, 2.55548, 3.68735 }, { 0.0333497, 0.102524, 8.10122e-05 }, { 7.40195, 7.81026, 13.5108 }, { 0.185696, 0.409228, -0.236166 }, { 0.000823299, 0.00122612, 0.00141316 } },
	{ "pink-jasper", "pink-jasper", { 0.226234, 0.138929, 0.110785 }, { 0.0846118, 0.0984038, 0.078693 }, { 0.00223592, 0.00203213, 0.0013737 }, { 0.53138, 0.4995, 0.436111 }, { 0.172825, 0.129714, 0.154397 }, { 0.0911362, 0.0599408, 0.0851167 }, { 253.003, 292.741, 474.2 }, { 1.64528, 1.86649, 5.29965e-08 }, { 5.71153e-08, 6.24765e-08, 15.5452 }, { 110.886, 113.953, 8.17872 }, { 0.416268, 0.422653, 0.561605 }, { 0.0166667, 0.0194625, 0.0142451 } },
	{ "pink-plastic", "pink-plastic", { 0.354572, 0.0905002, 0.0696372 }, { 0.0316585, 0.0444153, 6.36158e-15 }, { 0.0566727, 0.0369011, 0.142628 }, { 0.215149, 0.0621896, 0.189286 }, { 0.317781, 0.0925394, 1 }, { 0.266863, 0.0652821, 0 }, { 16.788, 27.3043, 7.67451 }, { 5.62766, 5.4919, 3.67439 }, { 1.71214e-05, 0.187553, 0.00561921 }, { 23.0822, 17.5369, 12.3737 }, { 0.0833775, 0.577298, 0.215985 }, { 0.00219041, 0.000905537, 0.00219415 } },
	{ "polyethylene", "polyethylene", { 0.228049, 0.239339, 0.240326 }, { 0.0420869, 0.134269, 0.0867928 }, { 0.0472725, 0.260465, 0.0719615 }, { 2.09548e-13, 0.743064, 2.27838e-13 }, { 0.489907, 0.316434, 0.181688 }, { 0.424297, 0.244434, 0.0772152 }, { 22.178, 4.24558, 14.9332 }, { 4.92064, 4.37149, 4.27422 }, { 0.330549, 9.78063e-07, 0.109488 }, { 14.3439, 17.178, 12.5991 }, { 0.61045, -0.470479, 0.517652 }, { 0.00285999, 0.00147486, 0.00191841 } },
	{ "polyurethane-foam", "polyurethane-foam", { 0.0898318, 0.0428583, 0.0340984 }, { 4.0852e-12, 8.0217e-14, 4.05682e-14 }, { 0.154984, 0.142104, 0.139418 }, { 0.188586, 0.188095, 0.188124 }, { 1, 1, 1 }, { 0, 0, 0 }, { 7.18433, 7.70043, 7.81983 }, { 3.50087, 3.67779, 3.7174 }, { 0.0079259, 0.00567317, 0.00525063 }, { 11.6922, 12.3795, 12.537 }, { 0.226848, 0.217315, 0.215132 }, { 0.00119032, 0.000823112, 0.000753708 } },
	{ "pure-rubber", "pure-rubber", { 0.284259, 0.251873, 0.223824 }, { 0.542899, 0.598765, 0.142162 }, { 0.62899, 0.41413, 0.0693873 }, { 1.13687, 0.452063, 0.262952 }, { 0.0185379, 0.00420901, 0.0353132 }, { -0.0265343, -0.035723, 0.0236736 }, { 3.75577, 3.75969, 13.6137 }, { 2.17187, 2.13584, 5.56595 }, { 0.0311422, 0.0526357, 7.8942e-07 }, { 7.14885, 7.0586, 23.9264 }, { 0.152489, 0.245615, -0.0979145 }, { 0.00177602, 0.00171837, 0.0007762 } },
	{ "purple-paint", "purple-paint", { 0.290743, 0.0347118, 0.0339802 }, { 0.301308, 0.205258, 0.280717 }, { 0.0339173, 0.00839425, 0.0378011 }, { 0.703279, 0.20083, 0.797376 }, { 0.0654958, 0.0786512, 0.0696424 }, { 0.0299494, 0.0467651, 0.0373649 }, { 16.3705, 104.698, 13.2101 }, { 1.72814, 6.77684, 1.54679 }, { 1.10548e-07, 0.177314, 6.66798e-08 }, { 38.0847, 41.1712, 38.2781 }, { 0.0789738, 0.576929, 0.0587144 }, { 0.00296394, 0.00277256, 0.00225685 } },
	{ "pvc", "pvc", { 0.0322978, 0.0357449, 0.0403426 }, { 0.28767, 0.317369, 0.310067 }, { 0.0171547, 0.0176681, 0.0213663 }, { 0.769726, 0.730637, 0.797555 }, { 0.0289552, 0.026258, 0.0281305 }, { 0.00815373, 0.00651989, 0.00714485 }, { 25.8231, 26.9026, 20.5927 }, { 1.20095, 1.31883, 1.23655 }, { 1.02768e-07, 1.09767e-07, 5.97648e-08 }, { 51.5975, 50.0184, 48.1953 }, { 0.218049, 0.208814, 0.173894 }, { 0.00471012, 0.00620207, 0.00575443 } },
	{ "red-fabric2", "", { 0.155216, 0.0226757, 0.0116884 }, { 1.80657e-15, 5.51946e-13, 1.35221e-15 }, { 0.16689, 0.135884, 0.128307 }, { 0.184631, 0.18856, 0.1883 }, { 1, 1, 1 }, { 0, 0, 0 }, { 6.78759, 7.98303, 8.36701 }, { 3.33819, 3.77225, 3.89063 }, { 0.0112096, 0.00468671, 0.00372538 }, { 11.0557, 12.7596, 13.2393 }, { 0.240935, 0.21164, 0.206003 }, { 0.00263226, 0.00060129, 0.000538747 } },
	{ "red-fabric", "fabric-red", { 0.201899, 0.0279008, 0.0103965 }, { 0.168669, 0.0486346, 0.040485 }, { 0.324447, 0.228455, 0.109436 }, { 0.787411, 0.821197, 0.279212 }, { 0.0718348, 0.0644687, 0.0206123 }, { -0.0585917, -0.0062547, -0.050402 }, { 3.88129, 4.32067, 9.16355 }, { 3.59825, 3.93046, 4.66379 }, { 0.000130047, 1.04152e-07, 4.59182e-05 }, { 13.0776, 19.649, 17.8852 }, { -0.209387, -0.530789, -0.0242035 }, { 0.00103676, 0.000248481, 0.000220698 } },
	{ "red-metallic-paint", "ch-ball-red-metallic", { 0.0380897, 0.00540095, 0.00281156 }, { 0.0416724, 0.07642, 0.108438 }, { 0.00133258, 0.00106883, 0.00128863 }, { 0.693854, 0.52857, 0.539477 }, { 2.45718e-16, 0.0598671, 0.0633332 }, { -1, -0.08904, -0.0114056 }, { 300.371, 521.418, 425.982 }, { 6.45857e-05, 6.3446e-07, 8.51754e-07 }, { 2.75773e-07, 4.05125e-07, 3.41703e-07 }, { 33.0213, 24.3646, 23.5793 }, { -0.121415, -0.469753, -0.532083 }, { 0.287672, 0.0467824, 0.0286558 } },
	{ "red-phenolic", "red-phenolic", { 0.165227, 0.0256259, 0.00935644 }, { 0.240561, 0.360634, 0.475777 }, { 0.0052844, 0.00467439, 0.00613717 }, { 0.568938, 0.509763, 0.575762 }, { 0.156419, 0.0972193, 0.069671 }, { 0.0752589, 0.0444558, 0.0266428 }, { 104.336, 128.839, 89.6357 }, { 1.57629, 1.92067, 1.57462 }, { 6.38793e-08, 6.71457e-08, 5.25172e-08 }, { 77.4088, 79.3795, 73.0158 }, { 0.343482, 0.352913, 0.324912 }, { 0.0680783, 0.0859239, 0.0936197 } },
	{ "red-plastic", "red-plastic", { 0.247569, 0.049382, 0.0175621 }, { 0.406976, 0.151478, 0.176348 }, { 0.28723, 0.0572489, 0.0624682 }, { 0.939617, 0.0851973, 0.0701483 }, { 0.10036, 0.178468, 0.149441 }, { 0.0512697, 0.13191, 0.102958 }, { 3.80564, 17.8374, 16.5631 }, { 4.37031, 4.96092, 4.75924 }, { 1.03822e-07, 0.0236845, 0.0321546 }, { 18.6088, 16.1371, 15.1243 }, { -0.609047, 0.43547, 0.445882 }, { 0.00134715, 0.000536577, 0.00047104 } },
	{ "red-specular-plastic", "red-plastic-specular", { 0.252589, 0.0397665, 0.0185317 }, { 0.0139957, 0.0343278, 0.0527973 }, { 6.01746e-05, 8.07327e-05, 0.000205705 }, { 0.174569, 0.202455, 0.390522 }, { 0.441328, 0.179378, 0.150221 }, { 0.191312, 0.0691835, 0.0760833 }, { 14623.1, 10620.2, 3332.98 }, { 22.6631, 12.9299, 3.06408 }, { 1e+38, 1e+38, 6.36761e-08 }, { 187.799, 162.245, 315.067 }, { 1.19812, 1.20136, 0.52102 }, { 0.0620224, 0.0541448, 0.0836154 } },
	{ "silicon-nitrade", "silicon-nitrade", { 0.0141611, 0.0115865, 0.00842477 }, { 0.0710113, 0.0670906, 0.015769 }, { 6.40406e-05, 0.000138867, 0.00224354 }, { 0.159422, 0.283527, 0.734323 }, { 0.0516164, 0.10318, 2.36643 }, { -0.0277792, -0.0531505, 1.36643 }, { 13926.7, 5668.82, 167.824 }, { 31.0579, 5.63563, 8.52534e-05 }, { 1e+38, 1e+38, 2.96855e-07 }, { 176.231, 148.745, 29.0282 }, { 1.20757, 1.14062, -0.241968 }, { 0.0712429, 0.0720965, 0.11404 } },
	{ "silver-metallic-paint2", "", { 0.0554792, 0.0573803, 0.0563376 }, { 0.121338, 0.115673, 0.10966 }, { 0.029859, 0.0303706, 0.0358666 }, { 0.144097, 0.104489, 0.158163 }, { 1.03749e-14, 3.52034e-15, 4.41778e-12 }, { -1, -1, -1 }, { 31.9005, 32.1514, 26.5685 }, { 6.08248, 5.89319, 5.95156 }, { 0.00761403, 0.0839948, 0.00138703 }, { 23.5891, 20.6786, 23.3297 }, { 0.435405, 0.54017, 0.34665 }, { 0.021384, 0.0187248, 0.0175762 } },
	{ "silver-metallic-paint", "metallic-silver", { 0.0189497, 0.0205686, 0.0228822 }, { 0.173533, 0.168901, 0.165266 }, { 0.037822, 0.038145, 0.0381908 }, { 0.165579, 0.162955, 0.160835 }, { 5.66903e-12, 1.65276e-14, 4.28399e-14 }, { -1, -1, -1 }, { 25.1551, 24.9957, 25.0003 }, { 5.92591, 5.90225, 5.89007 }, { 0.000684235, 0.000840795, 0.000995926 }, { 23.4725, 23.1898, 23.0144 }, { 0.310575, 0.317996, 0.324938 }, { 0.0122324, 0.0117672, 0.0113004 } },
	{ "silver-paint", "paint-silver", { 0.152796, 0.124616, 0.113375 }, { 0.30418, 0.30146, 0.283174 }, { 0.110819, 0.105318, 0.0785677 }, { 0.640378, 0.641115, 0.445228 }, { 2.37347e-13, 7.68194e-13, 2.9434e-12 }, { -0.350607, -0.355433, -0.359297 }, { 7.21531, 7.46519, 10.8289 }, { 3.06016, 2.97652, 3.78287 }, { 9.71666e-08, 1.09342e-07, 9.82336e-08 }, { 24.1475, 24.5083, 25.6757 }, { -0.281384, -0.257633, -0.200968 }, { 0.00361589, 0.00384995, 0.00444223 } },
	{ "special-walnut-224", "special-walnut-224", { 0.0121712, 0.00732998, 0.00463072 }, { 0.209603, 0.216118, 0.211885 }, { 0.117091, 0.119932, 0.131119 }, { 0.548899, 0.524858, 0.569425 }, { 0.0808859, 0.0802614, 0.0789982 }, { 0.0327605, 0.0324012, 0.0274637 }, { 7.42314, 7.41578, 6.77215 }, { 3.61532, 3.82778, 3.71096 }, { 1.19182e-07, 1.03098e-07, 1.08004e-07 }, { 22.9756, 22.7318, 22.2976 }, { -0.31158, -0.332257, -0.35462 }, { 0.000934753, 0.000878135, 0.000758769 } },
	{ "specular-black-phenolic", "black-bball", { 0.00212164, 0.00308282, 0.00410253 }, { 0.0881574, 0.0923246, 0.0398117 }, { 0.00119167, 0.000641898, 0.000186605 }, { 0.616914, 0.578026, 0.40121 }, { 0.122486, 0.0907984, 0.13855 }, { 0.0482851, 0.0348218, 0.0481813 }, { 395.656, 781.361, 3615.49 }, { 1.18051e-05, 1.22149, 2.87626 }, { 2.88932e-07, 6.09017e-08, 9.79363e-08 }, { 28.9748, 200.529, 331.336 }, { -0.257695, 0.487197, 0.524707 }, { 0.0311874, 0.0437836, 0.0853211 } },
	{ "specular-blue-phenolic", "blue-bball", { 0.00497564, 0.0138836, 0.032815 }, { 0.1077, 0.0898232, 0.175296 }, { 0.000918571, 0.0010348, 0.00176322 }, { 0.570978, 0.639916, 0.709385 }, { 0.0354139, 0.0488958, 0.023159 }, { -0.0434314, -0.0294427, -0.039938 }, { 558.482, 431.708, 222.547 }, { 3.57872e-06, 2.38985e-05, 6.87316e-05 }, { 3.33356e-07, 3.3336e-07, 3.2851e-07 }, { 28.5289, 32.1362, 30.3802 }, { -0.272244, -0.141224, -0.19005 }, { 0.0324614, 0.0338654, 0.057732 } },
	{ "specular-green-phenolic", "green-bball", { 0.00781782, 0.0259654, 0.0233739 }, { 0.0688449, 0.144658, 0.143654 }, { 0.000307494, 0.0010353, 0.00155331 }, { 0.365481, 0.585805, 0.787512 }, { 0.129429, 0.0443676, 5.423e-12 }, { 0.047932, -0.0136055, -0.0592743 }, { 2313.47, 482.847, 206.646 }, { 3.55591, 5.3306e-06, 0.000279403 }, { 1.03804e-07, 3.47154e-07, 3.16888e-07 }, { 256.625, 28.2901, 38.0691 }, { 0.511901, -0.277015, 0.00513058 }, { 0.0373526, 0.0450242, 0.0470867 } },
	{ "specular-maroon-phenolic", "maroon-bball", { 0.152486, 0.0263216, 0.00802748 }, { 0.0761775, 0.098375, 0.165913 }, { 0.000342958, 0.000605578, 0.00144136 }, { 0.4052, 0.553617, 0.65133 }, { 0.0646024, 0.0116325, 0.037551 }, { -0.0555983, -0.0527264, -0.0283844 }, { 1961.35, 868.119, 306.537 }, { 2.81751, 1.361, 2.43015e-05 }, { 5.70302e-08, 5.89096e-08, 3.27578e-07 }, { 248.591, 203.707, 29.053 }, { 0.506511, 0.488814, -0.242746 }, { 0.0465516, 0.0434438, 0.0545231 } },
	{ "specular-orange-phenolic", "orange-bball", { 0.32771, 0.0540131, 0.00883213 }, { 0.051915, 0.0686764, 0.0489478 }, { 7.91913e-05, 0.000139576, 1.62017e-05 }, { 0.253564, 0.354675, 3.55583e-12 }, { 0.0768695, 0.0496641, 0.0223538 }, { -0.015514, -0.023864, -0.0433847 }, { 10274.5, 5159.16, 61722.9 }, { 7.4207, 3.83488, 8.72207e+06 }, { 1e+38, 1.02522e-07, 2.30124e+07 }, { 170.364, 373.255, 855.811 }, { 1.19475, 0.53082, 0.608547 }, { 0.0405677, 0.0498947, 0.0953035 } },
	{ "specular-red-phenolic", "red-bball", { 0.303563, 0.0354891, 0.00899721 }, { 0.151819, 0.0938022, 0.196935 }, { 0.00117843, 0.00056476, 0.00185124 }, { 0.570146, 0.524406, 0.732785 }, { 0.0287445, 0.0672098, 0.0178433 }, { -0.0386172, 0.0144813, -0.0322308 }, { 438.968, 982.441, 201.328 }, { 2.88657e-06, 8.12418e-07, 9.95633e-05 }, { 3.8194e-07, 3.69661e-07, 3.03179e-07 }, { 26.0046, 30.0091, 31.3125 }, { -0.376008, -0.21799, -0.162894 }, { 0.0307055, 0.0199408, 0.0652829 } },
	{ "specular-violet-phenolic", "violet-bball", { 0.0686035, 0.0181856, 0.0210368 }, { 0.108459, 0.0471612, 0.171691 }, { 0.00123271, 0.000443974, 0.00149517 }, { 0.657484, 0.546753, 0.653065 }, { 0.0403569, 0.121081, 0.035323 }, { -0.0295013, 0.0563904, -0.0275623 }, { 351.208, 1193.45, 294.897 }, { 3.17585e-05, 1.3817, 2.44051e-05 }, { 3.02028e-07, 6.19706e-08, 3.40809e-07 }, { 31.3319, 234.879, 28.7237 }, { -0.168991, 0.500354, -0.252626 }, { 0.033584, 0.0495535, 0.0510203 } },
	{ "specular-white-phenolic", "white-bball", { 0.282896, 0.231703, 0.127818 }, { 0.0678467, 0.0683808, 0.032756 }, { 2.00918e-05, 3.22307e-05, 9.59333e-05 }, { 4.35028e-11, 0.131903, 0.390338 }, { 0.0132613, 0.0416359, 0.285115 }, { -0.1025, -0.0828341, 0.0759745 }, { 49772.5, 28318.4, 7131.79 }, { 4.58788e+06, 1.12234e+07, 3.05696 }, { 4.5813e+06, 1.00372e+07, 6.93295e-08 }, { 767.24, 646.93, 455.184 }, { 0.60999, 0.61958, 0.536789 }, { 0.134616, 0.114414, 0.179676 } },
	{ "specular-yellow-phenolic", "yellow-bball", { 0.309395, 0.135278, 0.0159106 }, { 0.0607659, 0.141526, 0.110839 }, { 0.000200174, 0.00179326, 0.00048094 }, { 0.355381, 0.767285, 0.505029 }, { 0.077039, 0.0175574, 0.038586 }, { -0.0182146, -0.0417745, -0.0114057 }, { 3597.35, 190.656, 1192.15 }, { 3.80702, 0.000181166, 1.69208 }, { 1.01181e-07, 3.0765e-07, 1.27637e-07 }, { 313.8, 34.1523, 221.08 }, { 0.523001, -0.0813947, 0.500075 }, { 0.0451868, 0.0491835, 0.031808 } },
	{ "ss440", "ss440", { 0.0229923, 0.0187037, 0.0153204 }, { 0.127809, 0.14899, 0.0473376 }, { 4.06782e-05, 4.86278e-05, 8.15367e-05 }, { 0.0888931, 0.259135, 0.332727 }, { 2.26479e-12, 5.7223e-13, 0.820577 }, { -0.474442, -0.223179, -0.179423 }, { 23197.8, 16625.7, 9083.17 }, { 1.43395e+07, 7.33446, 4.46921 }, { 1.06287e+07, 1e+38, 9.97795e-08 }, { 556.069, 215.021, 479.875 }, { 0.627834, 1.20059, 0.540075 }, { 0.366952, 0.463741, 0.874071 } },
	{ "steel", "steel", { 0.019973, 0.0127074, 0.0246402 }, { 0.0615275, 0.0469644, 0.0402151 }, { 8.63865e-05, 0.000249576, 5.77865e-05 }, { 0.3729, 0.583679, 0.471406 }, { 0.665193, 0.798139, 0.0115189 }, { -0.334807, -0.201861, -0.988481 }, { 8119.74, 1951.31, 10374.2 }, { 3.40501, 1.12086, 1.88549 }, { 1.25096e-07, 9.51373e-08, 1.20412e-07 }, { 474.668, 313.81, 603.334 }, { 0.539696, 0.519433, 0.545491 }, { 1.11996, 1.28262, 1.42004 } },
	{ "teflon", "teflon", { 0.276442, 0.263098, 0.260294 }, { 1.56924, 1.52804, 1.43859 }, { 0.678586, 0.662167, 0.577852 }, { 1.2402, 1.14126, 1.44077 }, { 9.40662e-13, 3.57656e-11, 1.50208e-11 }, { -0.0492032, -0.0587316, -0.0548028 }, { 3.91532, 3.82834, 3.64944 }, { 2.09871, 2.05044, 2.90315 }, { 0.0378525, 0.0435869, 0.0028469 }, { 6.8689, 6.72229, 9.65585 }, { 0.166634, 0.181605, -0.0468321 }, { 0.00462131, 0.00463132, 0.00408386 } },
	{ "tungsten-carbide", "tungsten-carbide", { 0.0151872, 0.0103016, 0.0123192 }, { 0.0504358, 0.075701, 0.0556673 }, { 6.6122e-05, 7.65809e-05, 4.80196e-05 }, { 0.255291, 0.270824, 0.26732 }, { 5.30357e-14, 5.44537e-12, 1.09586e-11 }, { -1, -1, -1 }, { 12280.7, 10423.3, 16683.5 }, { 7.42976, 6.36068, 6.81602 }, { 1e+38, 1e+38, 1e+38 }, { 185.623, 173.962, 218.975 }, { 1.197, 1.19586, 1.19745 }, { 0.472021, 0.583364, 0.850304 } },
	{ "two-layer-gold", "", { 0.0415046, 0.0312801, 0.0253658 }, { 1.58161, 1.18736, 1.63847 }, { 0.0263104, 0.0293804, 0.0241265 }, { 0.355682, 0.354281, 0.36415 }, { 0.117355, 0.0614942, 0.0447004 }, { 0.0411678, -0.0237579, -0.0100488 }, { 30.4478, 27.5367, 32.7278 }, { 3.91606, 3.99968, 3.79114 }, { 9.38513e-08, 6.62806e-08, 6.79906e-08 }, { 36.9091, 35.6578, 38.4915 }, { 0.0815181, 0.0465613, 0.0922721 }, { 0.165383, 0.160769, 0.168742 } },
	{ "two-layer-silver", "", { 0.0657916, 0.0595705, 0.0581288 }, { 1.55275, 2.00145, 1.93045 }, { 0.0149977, 0.0201665, 0.0225062 }, { 0.382631, 0.35975, 0.361657 }, { 4.93242e-13, 1.00098e-14, 0.0103259 }, { -0.0401315, -0.0395054, -0.0312454 }, { 50.1263, 38.8508, 34.9978 }, { 3.41873, 3.77545, 3.78138 }, { 6.09709e-08, 1.02036e-07, 1.01016e-07 }, { 46.6236, 40.8229, 39.1812 }, { 0.183797, 0.139103, 0.117092 }, { 0.170639, 0.189329, 0.21468 } },
	{ "violet-acrylic", "acrylic-violet", { 0.0599875, 0.023817, 0.0379025 }, { 0.134984, 0.13337, 0.295509 }, { 0.0011295, 0.00126481, 0.00186818 }, { 0.523244, 0.551606, 0.478149 }, { 0.100325, 0.0939057, 0.0663939 }, { 0.0603016, 0.0579001, 0.0353439 }, { 498.715, 424.307, 328.834 }, { 4.79917e-07, 1.39397e-06, 2.03376 }, { 4.27008e-07, 3.4655e-07, 1.05578e-07 }, { 23.5924, 24.3831, 116.7 }, { -0.515008, -0.476979, 0.432199 }, { 0.041711, 0.0411698, 0.0691096 } },
	{ "violet-rubber", "violet-rubber", { 0.223179, 0.0553634, 0.113238 }, { 0.547456, 0.0966027, 0.185463 }, { 0.445092, 0.089421, 0.1543 }, { 0.923481, 0.782897, 0.800907 }, { 0.0518927, 0.113508, 0.094549 }, { -0.0208955, 0.0585063, 0.036751 }, { 3.58673, 7.33539, 5.24404 }, { 2.83007, 2.27788, 3.00009 }, { 0.00374266, 9.65628e-08, 1.13746e-07 }, { 9.67534, 27.1898, 22.3519 }, { -0.0037936, -0.174481, -0.363226 }, { 0.00274248, 0.000994259, 0.00101746 } },
	{ "white-acrylic", "acrylic-white", { 0.314106, 0.300008, 0.263648 }, { 0.015339, 0.0736169, 0.0976209 }, { 0.00233914, 0.00168956, 0.00117717 }, { 0.570501, 0.51337, 0.440543 }, { 2.651, 0.38473, 0.249988 }, { 2.27139, 0.341493, 0.218093 }, { 226.134, 342.423, 548.396 }, { 1.41218, 2.17179e-07, 4.52325e-08 }, { 9.83895e-08, 5.84338e-06, 15.7173 }, { 110.209, 18.8595, 8.78916 }, { 0.419684, -0.61884, 0.56243 }, { 0.0537471, 0.0401864, 0.0400114 } },
	{ "white-diffuse-bball", "white-diffuse-bball", { 0.284726, 0.239199, 0.177227 }, { 0.680658, 0.828508, 0.502128 }, { 0.689731, 0.601478, 0.209084 }, { 1.23085, 0.956833, 0.800488 }, { 1.06465e-11, 7.45307e-19, 0.089058 }, { -0.109723, -0.090129, 0.0545089 }, { 3.94153, 3.65714, 4.53186 }, { 2.04845, 2.06125, 3.72838 }, { 0.0434345, 0.0439287, 1.06879e-07 }, { 6.69695, 6.79535, 20.1466 }, { 0.178983, 0.18898, -0.494774 }, { 0.007671, 0.0072205, 0.00441788 } },
	{ "white-fabric2", "", { 0.10784, 0.102669, 0.113943 }, { 0.0375359, 0.0296317, 0.0364218 }, { 0.0583526, 0.0520763, 0.063905 }, { 9.1836e-12, 5.41897e-13, 3.47708e-12 }, { 0.0528459, 0.0854122, 0.0860966 }, { 0.0578589, 0.0971296, 0.0985717 }, { 18.1669, 20.2291, 16.6809 }, { 4.60692, 4.77932, 4.46485 }, { 0.172556, 0.23927, 0.138435 }, { 13.5006, 13.9648, 13.1165 }, { 0.561147, 0.587176, 0.54153 }, { 0.00113864, 0.00114867, 0.00119185 } },
	{ "white-fabric", "", { 0.290107, 0.219835, 0.160654 }, { 0.230066, 0.156787, 2.19005e-12 }, { 0.479844, 0.196767, 0.162438 }, { 0.669662, 2.22894e-13, 0.194582 }, { 7.17773e-14, 0.0364632, 1 }, { -0.209237, -0.0528699, 0 }, { 3.60967, 6.18732, 6.91147 }, { 2.17095, 2.60737, 3.42418 }, { 0.0379118, 0.0990846, 0.00860197 }, { 7.221, 7.95861, 11.4289 }, { 0.195346, 0.409992, 0.225019 }, { 0.000728813, 0.000806025, 0.00177083 } },
	{ "white-marble", "", { 0.236183, 0.221746, 0.192889 }, { 0.24075, 0.221456, 0.23389 }, { 0.00430204, 0.00429388, 0.00405907 }, { 0.681306, 0.676061, 0.620701 }, { 0.118569, 0.167963, 0.140415 }, { 0.0620704, 0.102032, 0.071968 }, { 103.053, 104.312, 122.169 }, { 1.0396, 1.05866, 1.26372 }, { 1.08288e-07, 6.82995e-08, 5.76023e-08 }, { 88.7766, 89.0261, 88.95 }, { 0.378101, 0.372681, 0.371975 }, { 0.0493991, 0.0689784, 0.0584648 } },
	{ "white-paint", "white-paint", { 0.356024, 0.3536, 0.324889 }, { 4.14785, 0.255488, 0.108438 }, { 0.0654126, 0.0841905, 0.538225 }, { 0.796927, 0.7778, 0.888627 }, { 0.0770738, 0.25087, 1.75144e-13 }, { 0.0782297, 0.226285, -0.367441 }, { 8.89985, 7.66809, 3.59697 }, { 1.96428, 2.23069, 2.23051 }, { 5.9674e-08, 1.07673e-07, 0.0280623 }, { 31.0512, 27.6872, 7.41492 }, { -0.0896936, -0.151411, 0.154604 }, { 0.160631, 0.0657026, 0.0710504 } },
	{ "yellow-matte-plastic", "yellow-matte-plastic", { 0.276745, 0.108557, 0.0203686 }, { 0.806628, 1.99624, 0.977002 }, { 0.0237573, 0.0265756, 0.0305873 }, { 0.558101, 0.537647, 0.710679 }, { 0.0624565, 0.0605404, 0.0541077 }, { 0.049915, 0.0567466, 0.0473569 }, { 26.7398, 24.8437, 17.6137 }, { 2.1481, 2.32064, 1.65758 }, { 5.99553e-08, 5.96103e-08, 6.38097e-08 }, { 41.7723, 39.7008, 40.1979 }, { 0.11569, 0.0891281, 0.0886802 }, { 0.0469972, 0.10918, 0.0466045 } },
	{ "yellow-paint", "paint-yellow", { 0.288876, 0.195348, 0.0314583 }, { 0.449392, 0.412812, 0.168707 }, { 0.650734, 0.190849, 0.16131 }, { 1.21986, 0.0333524, 0.137577 }, { 0.00201415, 7.31639e-13, 0.13481 }, { -0.0897316, -0.0639292, 0.103378 }, { 3.83308, 6.31195, 7.06429 }, { 2.18358, 2.72018, 3.26561 }, { 0.0298708, 0.0764778, 0.0198928 }, { 7.1694, 8.39507, 10.5682 }, { 0.146537, 0.387991, 0.292185 }, { 0.00152911, 0.00123385, 0.000424783 } },
	{ "yellow-phenolic", "phenolic-yellow", { 0.26924, 0.190177, 0.0858303 }, { 0.0861694, 0.0960246, 0.122709 }, { 0.00126171, 0.00197611, 0.00187166 }, { 0.444222, 0.512534, 0.504223 }, { 0.0988294, 0.120249, 0.0929719 }, { 0.0295258, 0.0539044, 0.0471952 }, { 509.34, 294.418, 314.704 }, { 4.59604e-08, 1.76251, 1.81658 }, { 15.6472, 6.5671e-08, 9.96466e-08 }, { 8.52565, 116.04, 118.064 }, { 0.561753, 0.425536, 0.432459 }, { 0.0238647, 0.0190162, 0.0173945 } },
	{ "yellow-plastic", "yellow-plastic", { 0.221083, 0.193042, 0.0403393 }, { 0.265199, 0.340361, 0.0670333 }, { 0.280789, 0.146396, 0.0248514 }, { 0.920018, 0.0356237, 7.80502e-12 }, { 0.144471, 0.10845, 0.125678 }, { 0.113592, 0.0663735, 0.103644 }, { 3.85808, 7.84753, 41.2517 }, { 4.34432, 3.16869, 5.71439 }, { 1.04213e-07, 0.055929, 7.84273 }, { 18.6813, 9.73614, 16.4495 }, { -0.60265, 0.393386, 0.781122 }, { 0.00125483, 0.00140731, 0.00101943 } }
};

static double 
sgd__g1(const vec3 &wi, double theta0, double c, double k_, double lambda)
{
	double tmp1 = max(0.0, acos((double)wi.z) - theta0);
	double tmp2 = 1.0 - exp(c * pow(tmp1, k_));
	double tmp3 = 1.0 + lambda * tmp2;
	return sat(tmp3);
}

static double sgd__ndf(double zh, double alpha, double p, double kap)
{
	const double inv_pi = 1 / (double)m_pi();
	double c2 = sqr(zh);
	double t2 = (1.0 - c2) / c2;
	double ax = alpha + t2 / alpha;

	return ((kap * exp(-ax) * inv_pi) / (pow(ax, p) * c2 * c2));
}

// -------------------------------------------------------------------------------------------------
// SGD Ctor
sgd::sgd(const char *name): m_fresnel(), m_data(NULL)
{
	bool found = false;

	for (int i = 0; i < (int)(sizeof(sgd::s_data) / sizeof(sgd::data)); ++i) {
		if (!strcmp(s_data[i].name, name)
		    || !strcmp(s_data[i].otherName, name)) {
			m_data = &s_data[i];
			vec3 f0 = vec3(m_data->f0[0], m_data->f0[1], m_data->f0[2]);
			vec3 f1 = vec3(m_data->f1[0], m_data->f1[1], m_data->f1[2]);
			m_fresnel = fresnel::ptr(fresnel::sgd(f0, f1));

			found = true;
			break;
		}
	}
	if (!found) throw exc("djb_error: No SGD parameters for %s\n", name);
}

// -------------------------------------------------------------------------------------------------
// SGD eval
brdf::value_type
sgd::eval(const vec3 &wi, const vec3 &wo, const void *) const
{
	if (wi.z > 0 && wo.z > 0) {
		vec3 wh = normalize(wi + wo);
		const brdf::value_type Ks = {
		    (float_t)m_data->rhoS[0],
		    (float_t)m_data->rhoS[1],
		    (float_t)m_data->rhoS[2]
		};
		const brdf::value_type Kd = {
		    (float_t)m_data->rhoD[0],
		    (float_t)m_data->rhoD[1],
		    (float_t)m_data->rhoD[2]
		};
		brdf::value_type F = m_fresnel->eval(sat(dot(wi, wh)));
		brdf::value_type G = gaf(wh, wi, wo);
		brdf::value_type D = ndf(wh);

		return ((Kd + Ks * (F * D * G) / (wi.z * wo.z)) / m_pi());
	}

	return zero_value();
}

// -------------------------------------------------------------------------------------------------
// SGD Shadowing
brdf::value_type sgd::gaf(const vec3 &, const vec3 &wi, const vec3 &wo) const
{
	return g1(wi) * g1(wo);
}

brdf::value_type sgd::g1(const vec3 &wi) const
{
	brdf::value_type g1 = zero_value();

	for (int i = 0; i < (int)g1.size(); ++i)
		g1[i] = sgd__g1(wi, m_data->theta0[i], m_data->c[i],
	                    m_data->k[i], m_data->lambda[i]);

	return g1;
}

// -------------------------------------------------------------------------------------------------
// SGD NDF
brdf::value_type sgd::ndf(const vec3 &wh) const
{
	brdf::value_type ndf = zero_value();

	for (int i = 0; i < (int)ndf.size(); ++i)
		ndf[i] = (float_t)sgd__ndf((double)wh.z, m_data->alpha[i],
								   m_data->p[i], m_data->kap[i]);

	return ndf;
}


// *************************************************************************************************
// ABC Distribution API implementation

const abc::data abc::s_data[] = {
	{ "alum-bronze", {0.024514, 0.022346, 0.016220}, {51.714554, 37.932693, 27.373095}, 10482.133789, 0.816737, 2.236525 },
	{ "alumina-oxide", {0.310854, 0.288744, 0.253423}, {2191.683838, 1851.525513, 2064.303467}, 97381.554688, 1.393850, 1.437706 },
	{ "aluminium", {0.000000, 0.002017, 0.005639}, {1137.956787, 1090.751831, 1113.879150}, 218716.593750, 1.092006, 6.282688 },
	{ "aventurnine", {0.052933, 0.060724, 0.052465}, {1835.591309, 1594.715820, 1585.083984}, 324913.375000, 1.110341, 1.403899 },
	{ "beige-fabric", {0.202460, 0.132107, 0.110584}, {0.146439, 0.133698, 0.120796}, 3442009.500000, 0.083210, 10.406549 },
	{ "black-fabric", {0.014945, 0.008790, 0.008178}, {3.109159, 2.772291, 2.723374}, 871444.875000, 0.292300, 1.057058 },
	{ "black-obsidian", {0.000000, 0.000000, 0.000000}, {1447.612183, 1281.373169, 1313.115601}, 223549.906250, 1.196914, 1.485313 },
	{ "black-oxidized-steel", {0.012978, 0.011743, 0.011219}, {1.100971, 1.057305, 1.015528}, 30.661055, 1.869546, 1.660413 },
	{ "black-phenolic", {0.001445, 0.001674, 0.001885}, {104.792976, 99.654602, 100.537247}, 8696.112305, 1.524949, 1.839669 },
	{ "black-soft-plastic", {0.000666, 0.000594, 0.000723}, {0.449065, 0.442911, 0.443035}, 124.908409, 0.618811, 1.902569 },
	{ "blue-acrylic", {0.011106, 0.035057, 0.104254}, {1191.397339, 1099.257202, 1082.125366}, 255868.953125, 1.084736, 1.475061 },
	{ "blue-fabric", {0.008485, 0.009937, 0.034319}, {0.262721, 0.290408, 0.465468}, 3115416.500000, 0.138407, 2.581633 },
	{ "blue-metallic-paint", {0.003770, 0.001030, 0.009177}, {0.495104, 0.469843, 1.097868}, 48.572060, 1.702520, 9.024582 },
	{ "blue-metallic-paint2", {0.000000, 0.000000, 0.000000}, {399.504974, 589.572571, 1115.380737}, 68107.390625, 1.124658, 2.756175 },
	{ "blue-rubber", {0.035403, 0.074082, 0.154563}, {1.295714, 1.279542, 1.248921}, 37.265717, 1.565906, 1.448611 },
	{ "brass", {0.007722, 0.012504, 0.010908}, {2975.156982, 1931.137207, 987.877380}, 244596.531250, 1.136163, 2.698691 },
	{ "cherry-235", {0.037077, 0.014564, 0.007235}, {2.240743, 2.211937, 2.200735}, 123.542030, 1.498179, 1.741225 },
	{ "chrome-steel", {0.006340, 0.008416, 0.009893}, {795.266418, 629.395203, 620.750549}, 122360.437500, 1.486198, 99.999619 },
	{ "chrome", {0.002692, 0.002520, 0.003088}, {986.846741, 771.035522, 696.134583}, 64661.257812, 1.905420, 99.999413 },
	{ "colonial-maple-223", {0.081471, 0.023697, 0.008944}, {1.003711, 0.932037, 0.910946}, 24.846296, 2.121997, 1.805962 },
	{ "color-changing-paint1", {0.002224, 0.002598, 0.005303}, {32.486813, 34.621838, 36.775242}, 2456.161865, 1.388325, 2.066753 },
	{ "color-changing-paint2", {0.000368, 0.000022, 0.001103}, {16.119473, 13.414398, 13.766625}, 1099.414307, 1.331730, 3.186646 },
	{ "color-changing-paint3", {0.007087, 0.004775, 0.003475}, {35.495728, 34.895355, 33.480820}, 2193.942139, 1.442522, 1.918318 },
	{ "dark-blue-paint", {0.002488, 0.007395, 0.034947}, {0.541177, 0.465818, 0.482633}, 24.620041, 1.411947, 1.882264 },
	{ "dark-red-paint", {0.251814, 0.030605, 0.009098}, {0.457430, 0.406366, 0.394383}, 34.332043, 1.023539, 1.670872 },
	{ "dark-specular-fabric", {0.020414, 0.009048, 0.007769}, {1.659937, 1.561381, 1.496425}, 130.493088, 0.975626, 1.356871 },
	{ "delrin", {0.296617, 0.284276, 0.247152}, {4.629758, 4.546722, 4.481231}, 526.069458, 0.931238, 1.545662 },
	{ "fruitwood-241", {0.048981, 0.034689, 0.019745}, {5.976665, 5.760932, 5.568172}, 192.199890, 1.573300, 1.483582 },
	{ "gold-metallic-paint", {0.009164, 0.005649, 0.000424}, {1.685630, 1.147257, 0.418757}, 53.738525, 1.511730, 9.031934 },
	{ "gold-metallic-paint2", {0.000000, 0.000000, 0.000000}, {204.653580, 176.726898, 145.123520}, 1705396.875000, 0.621691, 8.599911 },
	{ "gold-metallic-paint3", {0.000000, 0.000000, 0.000000}, {712.306885, 534.449829, 299.714020}, 94549.195312, 0.970436, 3.965659 },
	{ "gold-paint", {0.150463, 0.080224, 0.022239}, {1.010258, 0.811582, 0.504538}, 46.942558, 1.756670, 7.936110 },
	{ "gray-plastic", {0.099830, 0.100258, 0.092187}, {208.924332, 204.230804, 203.086151}, 33710.691406, 1.023289, 1.400441 },
	{ "grease-covered-steel", {0.000000, 0.000000, 0.000000}, {292.647400, 277.053131, 268.833618}, 15585.240234, 1.170786, 2.556668 },
	{ "green-acrylic", {0.016581, 0.074299, 0.040329}, {1279.877197, 1196.967407, 1221.283569}, 59359.058594, 1.407153, 1.354611 },
	{ "green-fabric", {0.040464, 0.035315, 0.041862}, {0.655209, 0.935078, 1.071461}, 1602934.125000, 0.158285, 1.639683 },
	{ "green-latex", {0.071622, 0.106434, 0.047407}, {0.618660, 0.699535, 0.427560}, 701732.250000, 0.230061, 3.772501 },
	{ "green-metallic-paint", {0.000000, 0.027206, 0.035274}, {0.983370, 1.731232, 1.827718}, 43.999603, 2.172307, 3.235001 },
	{ "green-metallic-paint2", {0.000000, 0.000000, 0.000000}, {289.186859, 578.364441, 361.170837}, 117416.273438, 0.980195, 2.199395 },
	{ "green-plastic", {0.012262, 0.079555, 0.087255}, {2172.868896, 1950.600586, 1812.148193}, 365361.062500, 1.158465, 1.428372 },
	{ "hematite", {0.000000, 0.000273, 0.000528}, {1261.585449, 1214.305420, 1255.108643}, 453901.000000, 1.011091, 2.480358 },
	{ "ipswich-pine-221", {0.049757, 0.016449, 0.004662}, {1.680928, 1.649491, 1.647754}, 49.356201, 1.853272, 1.729069 },
	{ "light-brown-fabric", {0.048293, 0.019506, 0.013650}, {1.725058, 1.219095, 1.080752}, 3929432.500000, 0.155265, 1.153189 },
	{ "light-red-paint", {0.410883, 0.038975, 0.006158}, {0.591418, 0.516404, 0.539583}, 12.604274, 2.137619, 1.743125 },
	{ "maroon-plastic", {0.192636, 0.033163, 0.030345}, {2021.205688, 1751.832031, 1723.350952}, 255986.625000, 1.178735, 1.438042 },
	{ "natural-209", {0.080513, 0.023515, 0.003459}, {2.211772, 2.104441, 1.964800}, 91.766762, 1.154241, 1.641148 },
	{ "neoprene-rubber", {0.248773, 0.211228, 0.170494}, {1.327917, 1.299042, 1.233595}, 63.131874, 1.701130, 1.942141 },
	{ "nickel", {0.006227, 0.006663, 0.007587}, {36.614742, 32.745403, 28.906111}, 705.733887, 1.945258, 5.441883 },
	{ "nylon", {0.232688, 0.232487, 0.207358}, {19.215639, 18.194090, 16.938326}, 3334.451904, 0.886752, 1.382529 },
	{ "orange-paint", {0.407889, 0.131526, 0.003770}, {0.451945, 0.373510, 0.347662}, 11.196602, 1.972428, 1.887030 },
	{ "pearl-paint", {0.181452, 0.162150, 0.136895}, {0.843725, 0.714731, 0.493903}, 30.882542, 1.941556, 9.303603 },
	{ "pickled-oak-260", {0.127652, 0.093767, 0.077318}, {0.813234, 0.803229, 0.781871}, 67.082710, 1.625856, 2.933847 },
	{ "pink-fabric", {0.252317, 0.191945, 0.194732}, {0.434443, 0.450356, 0.464137}, 239167.375000, 0.146117, 2.710645 },
	{ "pink-fabric2", {0.210380, 0.046071, 0.034554}, {0.970204, 0.643877, 0.617551}, 1448228.625000, 0.193577, 4.907917 },
	{ "pink-felt", {0.267287, 0.178590, 0.156370}, {0.225846, 0.226874, 0.221785}, 54.853024, 0.344321, 2.938402 },
	{ "pink-jasper", {0.196444, 0.119217, 0.093004}, {372.695374, 357.903107, 363.584320}, 149508.703125, 0.934382, 1.652987 },
	{ "pink-plastic", {0.360193, 0.079077, 0.053058}, {0.826036, 0.561110, 0.579990}, 16122.769531, 0.217444, 1.810468 },
	{ "polyethylene", {0.183218, 0.202033, 0.207279}, {0.839841, 0.740593, 0.637590}, 3082111.000000, 0.118759, 3.010911 },
	{ "polyurethane-foam", {0.057868, 0.017982, 0.010879}, {0.146946, 0.101932, 0.092976}, 5723581.500000, 0.073071, 3.375618 },
	{ "pure-rubber", {0.287230, 0.252144, 0.211784}, {0.741122, 0.716288, 0.661539}, 85.876984, 0.982928, 1.670395 },
	{ "purple-paint", {0.280217, 0.031190, 0.032274}, {14.006363, 13.769886, 13.753972}, 408.684143, 1.776584, 1.483770 },
	{ "pvc", {0.030285, 0.033992, 0.038152}, {40.608692, 39.046635, 37.916531}, 2901.928223, 1.203405, 1.407985 },
	{ "red-fabric", {0.185341, 0.020015, 0.003522}, {0.934393, 0.314632, 0.273278}, 130451.187500, 0.287254, 3.335066 },
	{ "red-fabric2", {0.116616, 0.012705, 0.002205}, {0.586502, 0.141217, 0.134042}, 8092575.000000, 0.084146, 1.976978 },
	{ "red-metallic-paint", {0.000000, 0.000000, 0.000000}, {2077.145508, 423.511139, 278.621643}, 161943.781250, 1.015078, 2.346517 },
	{ "red-phenolic", {0.162631, 0.021944, 0.006764}, {198.859695, 194.122971, 198.083405}, 57814.757812, 1.049066, 1.752476 },
	{ "red-plastic", {0.268000, 0.044432, 0.010675}, {1.081538, 1.045352, 1.037089}, 34.117554, 1.529797, 1.666450 },
	{ "red-specular-plastic", {0.246119, 0.035688, 0.015152}, {2890.193359, 2449.908936, 2543.227783}, 215481.906250, 1.268530, 1.322196 },
	{ "silicon-nitrade", {0.002805, 0.003669, 0.003936}, {1185.676392, 1031.834106, 1048.810913}, 185127.203125, 1.146923, 1.828749 },
	{ "silver-metallic-paint", {0.000342, 0.000025, 0.002221}, {2.419921, 2.373467, 2.319122}, 58.200935, 1.718832, 9.073233 },
	{ "silver-metallic-paint2", {0.036282, 0.048095, 0.055908}, {1.964917, 1.771562, 1.583858}, 84.018394, 1.141540, 8.961876 },
	{ "silver-paint", {0.159599, 0.127910, 0.108671}, {1.151248, 1.207139, 1.248789}, 38.884895, 1.971779, 8.336617 },
	{ "special-walnut-224", {0.008121, 0.004822, 0.003478}, {0.704096, 0.693490, 0.676845}, 15.779606, 2.668091, 1.883156 },
	{ "specular-black-phenolic", {0.001379, 0.001737, 0.001891}, {2866.697021, 2568.570068, 3042.953613}, 291094.218750, 1.252478, 1.415415 },
	{ "specular-blue-phenolic", {0.002601, 0.011165, 0.029456}, {3080.328613, 2655.796143, 2536.662354}, 298749.312500, 1.201234, 1.437517 },
	{ "specular-green-phenolic", {0.004986, 0.023480, 0.020383}, {3187.704102, 2609.277832, 2708.220947}, 267365.562500, 1.224307, 1.411174 },
	{ "specular-maroon-phenolic", {0.150765, 0.024140, 0.006117}, {2987.999268, 2544.122314, 2451.749023}, 261203.859375, 1.225481, 1.424407 },
	{ "specular-orange-phenolic", {0.325956, 0.051359, 0.005049}, {3129.503174, 2678.228516, 2803.262207}, 462669.000000, 1.185505, 1.439050 },
	{ "specular-red-phenolic", {0.301706, 0.032497, 0.006908}, {2943.967773, 2402.380127, 2540.010010}, 261627.484375, 1.209261, 1.410007 },
	{ "specular-violet-phenolic", {0.066982, 0.015686, 0.018350}, {3117.315918, 2600.714111, 2717.788330}, 257952.890625, 1.251725, 1.407573 },
	{ "specular-white-phenolic", {0.279240, 0.225120, 0.122981}, {4513.249023, 3816.075195, 3782.943848}, 265093.843750, 1.318991, 1.426619 },
	{ "specular-yellow-phenolic", {0.305828, 0.133078, 0.012394}, {3565.679932, 2820.262207, 2784.792236}, 275647.718750, 1.221645, 1.393108 },
	{ "ss440", {0.003789, 0.004251, 0.004992}, {758.115112, 613.785278, 553.360596}, 96436.515625, 1.550240, 99.999336 },
	{ "steel", {0.006151, 0.008683, 0.011281}, {5709.654297, 4641.991211, 4419.819824}, 234651.921875, 1.401774, 2.658189 },
	{ "teflon", {0.321152, 0.313597, 0.305526}, {1.787717, 1.816174, 1.846979}, 41.644257, 2.066234, 1.611215 },
	{ "tungsten-carbide", {0.003085, 0.003349, 0.003638}, {808.095520, 620.232666, 606.583435}, 90806.242188, 1.700902, 99.999184 },
	{ "two-layer-gold", {0.000000, 0.000000, 0.000000}, {103.922806, 91.724258, 76.325027}, 481560.750000, 0.631262, 8.633638 },
	{ "two-layer-silver", {0.000000, 0.000000, 0.000000}, {125.852753, 121.561691, 119.139694}, 1048834.875000, 0.598443, 8.718435 },
	{ "violet-acrylic", {0.044077, 0.006396, 0.023810}, {428.049377, 362.420013, 431.304535}, 1148630.875000, 0.752451, 2.462801 },
	{ "violet-rubber", {0.232500, 0.045230, 0.108831}, {0.840866, 0.796264, 0.822774}, 371.841339, 0.646749, 2.143676 },
	{ "white-acrylic", {0.316720, 0.300911, 0.263206}, {1379.722168, 1291.507080, 1357.878052}, 153332.265625, 1.153521, 1.291205 },
	{ "white-diffuse-bball", {0.319956, 0.272448, 0.178031}, {2.104850, 2.082304, 2.011517}, 75.010391, 1.940200, 1.693726 },
	{ "white-fabric", {0.331064, 0.233945, 0.160661}, {3.545784, 3.587269, 3.573341}, 18457.023438, 0.103981, 1.039433 },
	{ "white-fabric2", {0.074490, 0.068798, 0.080224}, {0.078094, 0.081885, 0.083064}, 6166446.000000, 0.023514, 2.997413 },
	{ "white-marble", {0.222139, 0.208435, 0.183381}, {349.574860, 331.314941, 323.574066}, 49167.527344, 1.153413, 1.650249 },
	{ "white-paint", {0.320307, 0.312274, 0.305093}, {159.141998, 146.118881, 139.397903}, 2777.948730, 1.813009, 1.197418 },
	{ "yellow-matte-plastic", {0.290588, 0.113993, 0.019811}, {85.230881, 80.641701, 78.267593}, 6072.900879, 1.145334, 1.315196 },
	{ "yellow-paint", {0.301059, 0.196365, 0.025507}, {0.446085, 0.446681, 0.399212}, 12.881507, 2.137449, 1.822657 },
	{ "yellow-phenolic", {0.291521, 0.210540, 0.097144}, {498.945862, 477.551605, 480.963745}, 114769.390625, 1.006011, 1.543437 },
	{ "yellow-plastic", {0.237134, 0.202702, 0.030379}, {0.997742, 0.967993, 0.828363}, 289.454254, 0.522704, 1.596116 }
};

static double abc__ndf(double zh, double A, double B, double C)
{
	double tmp = 1.0 - zh;

	return (A / pow(1.0 + B * tmp, C));
}

// -------------------------------------------------------------------------------------------------
// ABC Ctor
abc::abc(const char *name): m_fresnel(), m_data(NULL)
{
	bool found = false;

	for (int i = 0; i < (int)(sizeof(abc::s_data) / sizeof(abc::data)); ++i) {
		if (!strcmp(s_data[i].name, name)) {
			m_data = &s_data[i];
			brdf::value_type ior(m_data->ior, zero_value().size());
			m_fresnel = fresnel::ptr(fresnel::unpolarized(ior));
			found = true;
			break;
		}
	}
	if (!found) throw exc("djb_error: No ABC parameters for %s\n", name);
}

// -------------------------------------------------------------------------------------------------
// ABC eval
brdf::value_type
abc::eval(const vec3 &wi, const vec3 &wo, const void *) const
{
	if (wi.z > 0 && wo.z > 0) {
		vec3 wh = normalize(wi + wo);
		brdf::value_type Kd = {
		    (float_t)m_data->kD[0],
		    (float_t)m_data->kD[1],
		    (float_t)m_data->kD[2]
		};
		brdf::value_type F = m_fresnel->eval(sat(dot(wi, wh)));
		brdf::value_type D = ndf(wh);
		float_t G = gaf(wh, wi, wo);

		return ((Kd + (F * D * G) / (wi.z * wo.z)) / m_pi());
	}

	return zero_value();
}

// -------------------------------------------------------------------------------------------------
// ABC Shadowing
float_t abc::gaf(const vec3 &wh, const vec3 &wi, const vec3 &wo) const
{
	float_t G1i = min((float_t)1, (float_t)2 * (wh.z * wi.z / dot(wh, wi)));
	float_t G1o = min((float_t)1, (float_t)2 * (wh.z * wo.z / dot(wh, wo)));

	return min(G1i, G1o);
}

// -------------------------------------------------------------------------------------------------
// ABC NDF
brdf::value_type abc::ndf(const vec3 &wh) const
{
	brdf::value_type ndf = zero_value();

	for (int i = 0; i < (int)ndf.size(); ++i)
		ndf[i] = (float_t)abc__ndf((double)wh.z,
								   m_data->A[i],
								   m_data->B,
								   m_data->C);

	return ndf;
}

// *************************************************************************************************
// Non-Parametric Microfacet Model API implementation
const char * npf::s_list[100] = {
    "alum-bronze", "alumina-oxide", "aluminium", "aventurnine", "beige-fabric",
    "black-fabric", "black-obsidian", "black-oxidized-steel", "black-phenolic",
    "black-soft-plastic", "blue-acrylic", "blue-fabric", "blue-metallic-paint2",
    "blue-metallic-paint", "blue-rubber", "brass", "cherry-235", "chrome",
    "chrome-steel", "colonial-maple-223", "color-changing-paint1",
    "color-changing-paint2", "color-changing-paint3", "dark-blue-paint",
    "dark-red-paint", "dark-specular-fabric", "delrin", "fruitwood-241",
    "gold-metallic-paint2", "gold-metallic-paint3", "gold-metallic-paint",
    "gold-paint", "gray-plastic", "grease-covered-steel", "green-acrylic",
    "green-fabric", "green-latex", "green-metallic-paint2",
    "green-metallic-paint", "green-plastic", "hematite", "ipswich-pine-221",
    "light-brown-fabric", "light-red-paint", "maroon-plastic", "natural-209",
    "neoprene-rubber", "nickel", "nylon", "orange-paint", "pearl-paint",
    "pickled-oak-260", "pink-fabric2", "pink-fabric", "pink-felt",
    "pink-jasper", "pink-plastic", "polyethylene", "polyurethane-foam",
    "pure-rubber", "purple-paint", "pvc", "red-fabric2", "red-fabric",
    "red-metallic-paint", "red-phenolic", "red-plastic", "red-specular-plastic",
    "silicon-nitrade", "silver-metallic-paint2", "silver-metallic-paint",
    "silver-paint", "special-walnut-224", "specular-black-phenolic",
    "specular-blue-phenolic", "specular-green-phenolic",
    "specular-maroon-phenolic", "specular-orange-phenolic",
    "specular-red-phenolic", "specular-violet-phenolic",
    "specular-white-phenolic", "specular-yellow-phenolic", "ss440", "steel",
    "teflon", "tungsten-carbide", "two-layer-gold", "two-layer-silver",
    "violet-acrylic", "violet-rubber", "white-acrylic", "white-diffuse-bball",
    "white-fabric2", "white-fabric", "white-marble", "white-paint",
    "yellow-matte-plastic", "yellow-paint", "yellow-phenolic","yellow-plastic"
};

vec3 npf::uberTextureLookup(const vec2 &uv) const
{
#if 0
	vec3 color = spline::eval2d(m_uber_texture, 512, 256,
	                            spline::uwrap_edge, uv.x,
	                            spline::uwrap_edge, uv.y);
	return color;
#else
	int i1 = uv.x * 512; if (i1 > 511) i1 = 511; if (i1 < 0) i1 = 0;
	int i2 = m_id;
	int idx = i1 + 512 * i2;
	return m_uber_texture[idx];
#endif
}

vec3 npf::uberTextureLookupInt(int x) const
{
	return m_uber_texture[x + 512 * m_id];
}

vec3 npf::lookupInterpolatedG1(int n, float_t theta) const
{
	float fb = theta / M_PI / 2 * 90.f;
	return uberTextureLookupInt(clamp(fb, 0.f, 89.f)+2+90);
	// find bin centers
	float f0 = floor(fb-0.5f) + 0.5f;
	float f1 = f0 + 1.f;
	// find bin indexes t0 and t1
	float t0 = floor(f0);
	float t1 = floor(f1);
	// ignores the first bin
	t0 = max(1.f, t0);
	t1 = max(2.f, t1);
	// find theta at bin centers
	//float theta0 = (t0+0.5f) * M_PI / 2 / 90.f;
	//float theta1 = (t1+0.5f) * M_PI / 2 / 90.f;
	//find the weights
	float w0 = 1.f - (fb - f0) ;
	float w1 = 1.f - w0 ; // 1.0 - (f1 - fb)
	// sample
	vec3 G1 = vec3(0);
	vec2 uv;
	uv.y = float(n)/256.f;
	if (t1 > 90.f-1.f) {
		uv.x = (t0+2.+90.f)/512.f;
		//G1 = uberTextureLookup(uv) * w0;
		G1 = uberTextureLookupInt(t0+2+90) * w0;
	} else {
		uv.x = (t0+2.+90.)/512.f;
		vec3 g0 = uberTextureLookup(uv);
		g0 = uberTextureLookupInt(t0+2+90);
		uv.x = (t1+2.+90.)/512.f;
		vec3 g1 = uberTextureLookup(uv);
		g1 = uberTextureLookupInt(t1+2+90);
		G1 = g0 * w0 + g1 * w1;
	}
	return G1;
}


brdf::value_type npf::eval(const vec3 &dirIn, const vec3 &dirOut, const void *) const
{
	if (dirIn.z < 0 || dirOut.z < 0)
		return zero_value();

	vec3 tmp = dirIn + dirOut;
	float_t nrm = dot(tmp, tmp);

	if (nrm == 0)
		return zero_value();
	vec3 dirNormal = vec3(0, 0, 1);
	vec3 dirH = tmp * inversesqrt(nrm);
	float thetaH = acos(sat(dot(dirH, dirNormal)));
	float thetaD = acos(sat(dot(dirIn, dirH)));
	float thetaI = acos(sat(dot(dirIn, dirNormal)));
	float thetaO = acos(sat(dot(dirOut, dirNormal)));

	// BRDF texture has a fixed size of 512x256
	vec2 uv;
	uv.y = float(m_id)/256.;

	uv.x = 0.0/512.;
	vec3 rhoD = uberTextureLookup(uv);
	//if (m_first) printf("%f %f %f\n", rhoD.x, rhoD.y, rhoD.z); //OK

	uv.x = 1.0/512.;
	vec3 rhoS = uberTextureLookup(uv);
	//if (m_first) printf("%f %f %f\n", rhoS.x, rhoS.y, rhoS.z); //OK

	int iD = int(clamp(sqrt(thetaH / M_PI / 2
	             * 90.0 * 90.0), 0.0, 89.0));
	uv.x = float(iD+2)/512.;
	if (m_first)
		printf("iD: %i\n", iD);
	vec3 D = uberTextureLookup(uv);
	D = uberTextureLookupInt(iD+2); //OK

	vec3 G1I = lookupInterpolatedG1(m_id, thetaI);

	vec3 G1O = lookupInterpolatedG1(m_id, thetaO);

	auto tmp2 = thetaD / M_PI / 2 * 90.0;
	int iF = int(clamp(tmp2, 0.0, 89.0));
	uv.x = float(iF+2+90+90)/512.;
	vec3 F = uberTextureLookup(uv);
	F = uberTextureLookupInt(iF+2+90+90); //OK

	vec3 BRDF = (rhoD) + (rhoS) * D * F *
	            (G1I / cos(thetaI)) * (G1O / cos(thetaO));
	BRDF*= dirOut.z;
	BRDF*= 1.0 / 16;

	m_first = false;

	if (fabs(dot(BRDF, BRDF)) > 9999.9999f || std::isnan(dot(BRDF, BRDF))) {
		return zero_value();
	}

	return brdf::value_type({BRDF.x, BRDF.y, BRDF.z});
}

npf::npf(const char *uber_texture, const char *name):
    m_uber_texture(), m_id(-1), m_first(true)
{
	// load the uber_texture
	FILE *pf = fopen(uber_texture, "rb");
	if (!pf)
		throw exc("djb_error: Failed to load %s\n", uber_texture);

	m_uber_texture.resize(512*256);
	fread(&m_uber_texture[0], sizeof(m_uber_texture[0]), m_uber_texture.size(), pf);
	fclose(pf);

	// extract id from name
	for (int i = 0; i < 100; ++i) {
		if (!strcmp(s_list[i], name)) {
			m_id = i;
#ifndef NVERBOSE
			DJB_LOG("djb_verbose: MERL ID: %i\n", m_id);
#endif
			break;
		}
	}
	if (m_id == -1) throw exc("djb_error: No NPF parameters for %s\n", name);
}

} // namespace djb

#endif // DJ_BRDF_IMPLEMENTATION

