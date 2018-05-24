////////////////////////////////////////////////////////////////////////////////
//
// Complete program (this compiles):
// Sphere Light Shading Demo
//
// g++ `sdl2-config --cflags` -I imgui planets.cpp gl_core_4_3.cpp  imgui/imgui*.cpp `sdl2-config --libs` -ldl -lGL -o planets
//

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_impl.h"

#include <cstdio>
#include <cstdlib>
#include <exception>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define DJ_OPENGL_IMPLEMENTATION 1
#include "dj_opengl.h"

#define DJ_ALGEBRA_IMPLEMENTATION 1
#include "dj_algebra.h"

#define DJ_BRDF_IMPLEMENTATION 1
#include "dj_brdf.h"

#define LOG(fmt, ...)  fprintf(stdout, fmt, ##__VA_ARGS__); fflush(stdout);


////////////////////////////////////////////////////////////////////////////////
// Tweakable Constants
//
////////////////////////////////////////////////////////////////////////////////
#define VIEWER_DEFAULT_WIDTH  1280
#define VIEWER_DEFAULT_HEIGHT 720

////////////////////////////////////////////////////////////////////////////////
// Global Variables
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// Framebuffer Manager
enum { AA_NONE, AA_MSAA2, AA_MSAA4, AA_MSAA8, AA_MSAA16 };
struct FramebufferManager {
	int w, h, aa, pass, samplesPerPass, samplesPerPixel;
	struct {bool progressive, reset;} flags;
	struct {int fixed;} msaa;
	struct {float r, g, b;} clearColor;
} g_framebuffer = {
	VIEWER_DEFAULT_WIDTH, VIEWER_DEFAULT_HEIGHT, AA_MSAA2, 0, 8, 1024 * 1024,
	{true, true},
	{false},
	{61./255., 119./255., 192./225}
};

// -----------------------------------------------------------------------------
// Camera Manager
struct CameraManager {
	float fovy, zNear, zFar; // perspective settings
	dja::vec3 pos;           // 3D position
	dja::mat3 axis;          // 3D frame
} g_camera = {
	55.f, 0.01f, 1024.f,
	dja::vec3(3, 0, 1.2),
	dja::mat3(
		0.971769, -0.129628, -0.197135,
		0.127271, 0.991562, -0.024635,
		0.198665, -0.001150, 0.980067
	)
};

// -----------------------------------------------------------------------------
// Planet Manager
enum {
	SHADING_MC_COS,
	SHADING_MC_GGX,
	SHADING_MC_MIS,
	SHADING_DEBUG
};
enum { BRDF_DIFFUSE, BRDF_MERL, BRDF_NPF };
struct SphereManager {
	struct {bool showLines;} flags;
	struct {
		int xTess, yTess;
		int vertexCnt, indexCnt;
	} sphere;
	struct {
		struct {
			const char **files;
			int id, cnt;
		} merl;
		struct {
			const char **files;
			int id, cnt;
		} envmap;
		const char *pathToUberData;
		int mode, brdf;
		float ggxAlpha;
	} shading;
} g_sphere = {
	{false},
	{24, 48, -1, -1}, // sphere
	{
		{NULL, 0, 0},
		{NULL, 0, 0},
		NULL,
		SHADING_MC_GGX,
		BRDF_MERL,
		1.f
	}

};

// -----------------------------------------------------------------------------
// Application Manager
struct AppManager {
	struct {
		const char *shader;
		const char *output;
	} dir;
	struct {
		int w, h;
		bool hud;
		float gamma, exposure;
	} viewer;
	struct {
		int on, frame, capture;
	} recorder;
	int frame, frameLimit;
} g_app = {
	/*dir*/    {"./shaders/", "./"},
	/*viewer*/ {
	               VIEWER_DEFAULT_WIDTH, VIEWER_DEFAULT_HEIGHT,
	               true,
	               2.2f, 2.0f
	           },
	/*record*/ {false, 0, 0},
	/*frame*/  0, -1
};

// -----------------------------------------------------------------------------
// OpenGL Manager
enum { CLOCK_SPF, CLOCK_COUNT };
enum { FRAMEBUFFER_BACK, FRAMEBUFFER_SCENE, FRAMEBUFFER_COUNT };
enum { VERTEXARRAY_EMPTY, VERTEXARRAY_SPHERE, VERTEXARRAY_COUNT };
enum { STREAM_SPHERES, STREAM_TRANSFORM, STREAM_RANDOM, STREAM_COUNT };
enum {
	TEXTURE_BACK,
	TEXTURE_SCENE,
	TEXTURE_Z,
	TEXTURE_ENVMAP,
	TEXTURE_NPF,
	TEXTURE_MERL,
	TEXTURE_COUNT
};
enum {
	BUFFER_SPHERE_VERTICES,
	BUFFER_SPHERE_INDEXES,
	BUFFER_MERL,
	BUFFER_COUNT
};
enum {
	PROGRAM_VIEWER,
	PROGRAM_BACKGROUND,
	PROGRAM_SPHERE,
	PROGRAM_COUNT
};
enum {
	UNIFORM_VIEWER_FRAMEBUFFER_SAMPLER,
	UNIFORM_VIEWER_EXPOSURE,
	UNIFORM_VIEWER_GAMMA,
	UNIFORM_VIEWER_VIEWPORT,

	UNIFORM_BACKGROUND_CLEAR_COLOR,
	UNIFORM_BACKGROUND_ENVMAP_SAMPLER,

	UNIFORM_SPHERE_SAMPLES_PER_PASS,
	UNIFORM_SPHERE_NPF_SAMPLER,
	UNIFORM_SPHERE_ENVMAP_SAMPLER,
	UNIFORM_SPHERE_MERL_SAMPLER,
	UNIFORM_SPHERE_ALPHA,
	UNIFORM_SPHERE_MERL_ID,

	UNIFORM_COUNT
};
struct OpenGLManager {
	GLuint programs[PROGRAM_COUNT];
	GLuint framebuffers[FRAMEBUFFER_COUNT];
	GLuint textures[TEXTURE_COUNT];
	GLuint vertexArrays[VERTEXARRAY_COUNT];
	GLuint buffers[BUFFER_COUNT];
	GLint uniforms[UNIFORM_COUNT];
	djg_buffer *streams[STREAM_COUNT];
	djg_clock *clocks[CLOCK_COUNT];
} g_gl = {{0}};


////////////////////////////////////////////////////////////////////////////////
// Utility functions
//
////////////////////////////////////////////////////////////////////////////////

#ifndef M_PI
#define M_PI 3.141592654
#endif
#define BUFFER_SIZE(x)    ((int)(sizeof(x)/sizeof(x[0])))
#define BUFFER_OFFSET(i) ((char *)NULL + (i))

float radians(float degrees)
{
	return degrees * M_PI / 180.f;
}

char *strcat2(char *dst, const char *src1, const char *src2)
{
	strcpy(dst, src1);

	return strcat(dst, src2);
}

static void
debug_output_logger(
        GLenum source,
        GLenum type,
        GLuint id,
        GLenum severity,
        GLsizei length,
        const GLchar* message,
        const GLvoid* userParam
        ) {
    char srcstr[32], typestr[32];

    switch(source) {
    case GL_DEBUG_SOURCE_API: strcpy(srcstr, "OpenGL"); break;
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM: strcpy(srcstr, "Windows"); break;
    case GL_DEBUG_SOURCE_SHADER_COMPILER: strcpy(srcstr, "Shader Compiler"); break;
    case GL_DEBUG_SOURCE_THIRD_PARTY: strcpy(srcstr, "Third Party"); break;
    case GL_DEBUG_SOURCE_APPLICATION: strcpy(srcstr, "Application"); break;
    case GL_DEBUG_SOURCE_OTHER: strcpy(srcstr, "Other"); break;
    default: strcpy(srcstr, "???"); break;
    };

    switch(type) {
    case GL_DEBUG_TYPE_ERROR: strcpy(typestr, "Error"); break;
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: strcpy(typestr, "Deprecated Behavior"); break;
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: strcpy(typestr, "Undefined Behavior"); break;
    case GL_DEBUG_TYPE_PORTABILITY: strcpy(typestr, "Portability"); break;
    case GL_DEBUG_TYPE_PERFORMANCE: strcpy(typestr, "Performance"); break;
    case GL_DEBUG_TYPE_OTHER: strcpy(typestr, "Message"); break;
    default: strcpy(typestr, "???"); break;
    }

    if(severity == GL_DEBUG_SEVERITY_HIGH || severity == GL_DEBUG_SEVERITY_MEDIUM) {
        LOG("djg_error: %s %s\n"                \
            "-- Begin -- GL_debug_output\n" \
            "%s\n"                              \
            "-- End -- GL_debug_output\n",
            srcstr, typestr, message);
    } else if(severity == GL_DEBUG_SEVERITY_MEDIUM) {
        LOG("djg_warn: %s %s\n"                 \
            "-- Begin -- GL_debug_output\n" \
            "%s\n"                              \
            "-- End -- GL_debug_output\n",
            srcstr, typestr, message);
    }
}

void log_debug_output(void)
{
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(&debug_output_logger, NULL);
}

////////////////////////////////////////////////////////////////////////////////
// Program Configuration
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// set viewer program uniforms
void configureViewerProgram()
{
	glProgramUniform1i(g_gl.programs[PROGRAM_VIEWER],
	                   g_gl.uniforms[UNIFORM_VIEWER_FRAMEBUFFER_SAMPLER],
	                   TEXTURE_SCENE);
	glProgramUniform1f(g_gl.programs[PROGRAM_VIEWER],
	                   g_gl.uniforms[UNIFORM_VIEWER_EXPOSURE],
	                   g_app.viewer.exposure);
	glProgramUniform1f(g_gl.programs[PROGRAM_VIEWER],
	                   g_gl.uniforms[UNIFORM_VIEWER_GAMMA],
	                   g_app.viewer.gamma);
}

// -----------------------------------------------------------------------------
// set background program uniforms
void configureBackgroundProgram()
{
	glProgramUniform3f(g_gl.programs[PROGRAM_BACKGROUND],
	                   g_gl.uniforms[UNIFORM_BACKGROUND_CLEAR_COLOR],
	                   g_framebuffer.clearColor.r,
	                   g_framebuffer.clearColor.g,
	                   g_framebuffer.clearColor.b);
	glProgramUniform1i(g_gl.programs[PROGRAM_BACKGROUND],
	                   g_gl.uniforms[UNIFORM_BACKGROUND_ENVMAP_SAMPLER],
	                   TEXTURE_NPF);
}

// -----------------------------------------------------------------------------
// set Sphere program uniforms
void configureSphereProgram()
{
	glProgramUniform1i(g_gl.programs[PROGRAM_SPHERE],
	                   g_gl.uniforms[UNIFORM_SPHERE_SAMPLES_PER_PASS],
	                   g_framebuffer.samplesPerPass);
	glProgramUniform1i(g_gl.programs[PROGRAM_SPHERE],
	                   g_gl.uniforms[UNIFORM_SPHERE_NPF_SAMPLER],
	                   TEXTURE_NPF);
	glProgramUniform1i(g_gl.programs[PROGRAM_SPHERE],
	                   g_gl.uniforms[UNIFORM_SPHERE_ENVMAP_SAMPLER],
	                   TEXTURE_ENVMAP);
	glProgramUniform1i(g_gl.programs[PROGRAM_SPHERE],
	                   g_gl.uniforms[UNIFORM_SPHERE_MERL_SAMPLER],
	                   TEXTURE_MERL);
	glProgramUniform1i(g_gl.programs[PROGRAM_SPHERE],
	                   g_gl.uniforms[UNIFORM_SPHERE_MERL_ID],
	                   g_sphere.shading.merl.id);
	glProgramUniform1f(g_gl.programs[PROGRAM_SPHERE],
	                   g_gl.uniforms[UNIFORM_SPHERE_ALPHA],
	                   g_sphere.shading.ggxAlpha);
}

////////////////////////////////////////////////////////////////////////////////
// Program Loading
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
/**
 * Load the Viewer Program
 *
 * This program is responsible for blitting the scene framebuffer to
 * the back framebuffer, while applying gamma correction and tone mapping to
 * the rendering.
 */
bool loadViewerProgram()
{
	djg_program *djp = djgp_create();
	GLuint *program = &g_gl.programs[PROGRAM_VIEWER];
	char buf[1024];

	LOG("Loading {Framebuffer-Blit-Program}\n");
	if (g_framebuffer.aa >= AA_MSAA2 && g_framebuffer.aa <= AA_MSAA16)
		djgp_push_string(djp, "#define MSAA_FACTOR %i\n", 1 << g_framebuffer.aa);
	djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "viewer.glsl"));
	if (!djgp_to_gl(djp, 430, false, true, program)) {
		LOG("=> Failure <=\n");
		djgp_release(djp);

		return false;
	}
	djgp_release(djp);

	g_gl.uniforms[UNIFORM_VIEWER_FRAMEBUFFER_SAMPLER] =
		glGetUniformLocation(g_gl.programs[PROGRAM_VIEWER], "u_FramebufferSampler");
	g_gl.uniforms[UNIFORM_VIEWER_VIEWPORT] =
		glGetUniformLocation(g_gl.programs[PROGRAM_VIEWER], "u_Viewport");
	g_gl.uniforms[UNIFORM_VIEWER_EXPOSURE] =
		glGetUniformLocation(g_gl.programs[PROGRAM_VIEWER], "u_Exposure");
	g_gl.uniforms[UNIFORM_VIEWER_GAMMA] =
		glGetUniformLocation(g_gl.programs[PROGRAM_VIEWER], "u_Gamma");

	configureViewerProgram();

	return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Background Program
 *
 * This program renders a Background.
 */
bool loadBackgroundProgram()
{
	djg_program *djp = djgp_create();
	GLuint *program = &g_gl.programs[PROGRAM_BACKGROUND];
	char buf[1024];

	LOG("Loading {Background-Program}\n");
	djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "background.glsl"));
	if (!djgp_to_gl(djp, 430, false, true, program)) {
		LOG("=> Failure <=\n");
		djgp_release(djp);

		return false;
	}
	djgp_release(djp);

	g_gl.uniforms[UNIFORM_BACKGROUND_CLEAR_COLOR] =
		glGetUniformLocation(g_gl.programs[PROGRAM_BACKGROUND], "u_ClearColor");
	g_gl.uniforms[UNIFORM_BACKGROUND_ENVMAP_SAMPLER] =
		glGetUniformLocation(g_gl.programs[PROGRAM_BACKGROUND], "u_EnvmapSampler");

	configureBackgroundProgram();

	return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Sphere Program
 *
 * This program is responsible for rendering the spheres to the
 * framebuffer
 */
bool loadSphereProgram()
{
	djg_program *djp = djgp_create();
	GLuint *program = &g_gl.programs[PROGRAM_SPHERE];
	char buf[1024];

	LOG("Loading {Sphere-Program}\n");
	switch (g_sphere.shading.brdf) {
		case BRDF_MERL:
			djgp_push_string(djp, "#define BRDF_MERL 1\n");
			break;
		case BRDF_NPF:
			djgp_push_string(djp, "#define BRDF_NPF 1\n");
			break;
		case BRDF_DIFFUSE:
			djgp_push_string(djp, "#define BRDF_DIFFUSE 1\n");
			break;
	};
	switch (g_sphere.shading.mode) {
		case SHADING_DEBUG:
			djgp_push_string(djp, "#define SHADE_DEBUG 1\n");
			break;
		case SHADING_MC_GGX:
			djgp_push_string(djp, "#define SHADE_MC_GGX 1\n");
			break;
		case SHADING_MC_COS:
			djgp_push_string(djp, "#define SHADE_MC_COS 1\n");
			break;
		case SHADING_MC_MIS:
			djgp_push_string(djp, "#define SHADE_MC_MIS 1\n");
			break;
	};
	djgp_push_string(djp, "#define BUFFER_BINDING_RANDOM %i\n", STREAM_RANDOM);
	djgp_push_string(djp, "#define BUFFER_BINDING_TRANSFORMS %i\n", STREAM_TRANSFORM);
	djgp_push_string(djp, "#define BUFFER_BINDING_SPHERES %i\n", STREAM_SPHERES);
	djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "ggx.glsl"));
	djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "npf.glsl"));
	djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "brdf_merl.glsl"));
	djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "pivot.glsl"));
	djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "sphere.glsl"));

	if (!djgp_to_gl(djp, 430, false, true, program)) {
		LOG("=> Failure <=\n");
		djgp_release(djp);

		return false;
	}
	djgp_release(djp);

	g_gl.uniforms[UNIFORM_SPHERE_SAMPLES_PER_PASS] =
		glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_SamplesPerPass");
	g_gl.uniforms[UNIFORM_SPHERE_NPF_SAMPLER] =
		glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_NpfSampler");
	g_gl.uniforms[UNIFORM_SPHERE_ENVMAP_SAMPLER] =
		glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_EnvmapSampler");
	g_gl.uniforms[UNIFORM_SPHERE_MERL_SAMPLER] =
		glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_MerlSampler");
	g_gl.uniforms[UNIFORM_SPHERE_ALPHA] =
		glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_Alpha");
	g_gl.uniforms[UNIFORM_SPHERE_MERL_ID] =
		glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_MerlId");

	configureSphereProgram();

	return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load All Programs
 *
 */
bool loadPrograms()
{
	bool v = true;

	v&= loadViewerProgram();
	v&= loadBackgroundProgram();
	v&= loadSphereProgram();

	return v;
}

////////////////////////////////////////////////////////////////////////////////
// Texture Loading
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
/**
 * Load the Scene Framebuffer Textures
 *
 * Depending on the scene framebuffer AA mode, this function load 2 or
 * 3 textures. In FSAA mode, two RGBA16F and one DEPTH24_STENCIL8 textures
 * are created. In other modes, one RGBA16F and one DEPTH24_STENCIL8 textures
 * are created.
 */
bool loadSceneFramebufferTexture()
{
	if (glIsTexture(g_gl.textures[TEXTURE_SCENE]))
		glDeleteTextures(1, &g_gl.textures[TEXTURE_SCENE]);
	if (glIsTexture(g_gl.textures[TEXTURE_Z]))
		glDeleteTextures(1, &g_gl.textures[TEXTURE_Z]);
	glGenTextures(1, &g_gl.textures[TEXTURE_Z]);
	glGenTextures(1, &g_gl.textures[TEXTURE_SCENE]);

	switch (g_framebuffer.aa) {
		case AA_NONE:
			LOG("Loading {Scene-Z-Framebuffer-Texture}\n");
			glActiveTexture(GL_TEXTURE0 + TEXTURE_Z);
			glBindTexture(GL_TEXTURE_2D, g_gl.textures[TEXTURE_Z]);
			glTexStorage2D(GL_TEXTURE_2D,
			               1,
			               GL_DEPTH24_STENCIL8,
			               g_framebuffer.w,
			               g_framebuffer.h);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

			LOG("Loading {Scene-RGBA-Framebuffer-Texture}\n");
			glActiveTexture(GL_TEXTURE0 + TEXTURE_SCENE);
			glBindTexture(GL_TEXTURE_2D, g_gl.textures[TEXTURE_SCENE]);
			glTexStorage2D(GL_TEXTURE_2D,
			               1,
			               GL_RGBA32F,
			               g_framebuffer.w,
			               g_framebuffer.h);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			break;
		case AA_MSAA2:
		case AA_MSAA4:
		case AA_MSAA8:
		case AA_MSAA16: {
			int samples = 1 << g_framebuffer.aa;
			int maxSamples;

			glGetIntegerv(GL_MAX_INTEGER_SAMPLES, &maxSamples);
			if (samples > maxSamples) {
				LOG("note: MSAA is %ix\n", maxSamples);
				samples = maxSamples;
			}
			LOG("Loading {Scene-MSAA-Z-Framebuffer-Texture}\n");
			glActiveTexture(GL_TEXTURE0 + TEXTURE_Z);
			glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, g_gl.textures[TEXTURE_Z]);
			glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE,
			                          samples,
			                          GL_DEPTH24_STENCIL8,
			                          g_framebuffer.w,
			                          g_framebuffer.h,
			                          g_framebuffer.msaa.fixed);

			LOG("Loading {Scene-MSAA-RGBA-Framebuffer-Texture}\n");
			glActiveTexture(GL_TEXTURE0 + TEXTURE_SCENE);
			glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,
			              g_gl.textures[TEXTURE_SCENE]);
			glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE,
			                          samples,
			                          GL_RGBA32F,
			                          g_framebuffer.w,
			                          g_framebuffer.h,
			                          g_framebuffer.msaa.fixed);
		} break;
	}
	glActiveTexture(GL_TEXTURE0);

	return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Back Framebuffer Texture
 *
 * This loads an RGBA8 texture used as a color buffer for the back
 * framebuffer.
 */
bool loadBackFramebufferTexture()
{
	LOG("Loading {Back-Framebuffer-Texture}\n");
	if (glIsTexture(g_gl.textures[TEXTURE_BACK]))
		glDeleteTextures(1, &g_gl.textures[TEXTURE_BACK]);
	glGenTextures(1, &g_gl.textures[TEXTURE_BACK]);

	glActiveTexture(GL_TEXTURE0 + TEXTURE_BACK);
	glBindTexture(GL_TEXTURE_2D, g_gl.textures[TEXTURE_BACK]);
	glTexStorage2D(GL_TEXTURE_2D,
	               1,
	               GL_RGBA8,
	               g_app.viewer.w,
	               g_app.viewer.h);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glActiveTexture(GL_TEXTURE0);

	return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Pivot Texture
 *
 * This loads a precomputed table that is used to map a GGX BRDF to a
 * Uniform PTSD parameter.
 */
bool loadNpfTexture()
{
	LOG("Loading {NPF-Texture}\n");
	FILE *pf = fopen(g_sphere.shading.pathToUberData, "rb");
	std::vector<float> data;

	if (!pf)
		return false;
	data.resize(512 * 256 * 3);
	fread(&data[0], sizeof(float), data.size(), pf);
	fclose(pf);
	if (glIsTexture(g_gl.textures[TEXTURE_NPF]))
		glDeleteTextures(1, &g_gl.textures[TEXTURE_NPF]);
	glGenTextures(1, &g_gl.textures[TEXTURE_NPF]);

	glActiveTexture(GL_TEXTURE0 + TEXTURE_NPF);
	glBindTexture(GL_TEXTURE_2D, g_gl.textures[TEXTURE_NPF]);
	glTexStorage2D(GL_TEXTURE_2D,
	               1,
	               GL_RGB32F,
	               512,
	               256);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 256, GL_RGB, GL_FLOAT, &data[0]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glActiveTexture(GL_TEXTURE0);

	return (glGetError() == GL_NO_ERROR);
}

static bool loadMerlTexture(void)
{
	if (g_sphere.shading.merl.cnt) {
		try {
		LOG("Loading {MERL-BRDF}\n");

		djb::merl merl(g_sphere.shading.merl.files[g_sphere.shading.merl.id]);
		djb::tab_r tab(merl, 90);
		djb::microfacet::args args = djb::tab_r::extract_ggx_args(djb::tab_r(merl, 90));
		g_sphere.shading.ggxAlpha = args.minv[0][0];

		if (glIsTexture(g_gl.textures[TEXTURE_MERL])) {
			glDeleteBuffers(1, &g_gl.buffers[BUFFER_MERL]);
			glDeleteTextures(1, &g_gl.textures[TEXTURE_MERL]);
		}
		glGenBuffers(1, &g_gl.buffers[BUFFER_MERL]);
		glGenTextures(1, &g_gl.textures[TEXTURE_MERL]);

		LOG("Loading {MERL-Texture}\n");
		glActiveTexture(GL_TEXTURE0 + TEXTURE_MERL);
		glBindTexture(GL_TEXTURE_BUFFER, g_gl.textures[TEXTURE_MERL]);
		glBindBuffer(GL_TEXTURE_BUFFER, g_gl.buffers[BUFFER_MERL]);
		std::vector<float> texels;
		const std::vector<double>& samples = merl.get_samples();
		texels.reserve(samples.size());
		texels.resize(0);
		for (int i = 0; i < (int) samples.size(); ++i)
			texels.push_back(samples[i]);
		glBufferData(GL_TEXTURE_BUFFER,
		             sizeof(texels[0]) * texels.size(),
		             &texels[0],
		             GL_STATIC_DRAW);
		glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, g_gl.buffers[BUFFER_MERL]);
		glBindBuffer(GL_TEXTURE_BUFFER, 0);

		// clean up
		glActiveTexture(GL_TEXTURE0);

		} catch (std::exception& e) {
			LOG("%s\n", e.what());
			return false;
		} catch (...) {
			return false;
		}
	}

	return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Roughness Texture
 *
 * This loads an R8 texture used as a roughness texture map.
 */
bool loadEnvmapTexture()
{
	LOG("Loading {Envmap-Texture}\n");
	if (g_sphere.shading.merl.cnt) {
		int id = g_sphere.shading.envmap.id;
		const char *path = g_sphere.shading.envmap.files[id];

		if (glIsTexture(g_gl.textures[TEXTURE_ENVMAP]))
			glDeleteTextures(1, &g_gl.textures[TEXTURE_ENVMAP]);
		glGenTextures(1, &g_gl.textures[TEXTURE_ENVMAP]);

		djg_texture *djgt = djgt_create(0);
		GLuint *glt = &g_gl.textures[TEXTURE_ENVMAP];

		glActiveTexture(GL_TEXTURE0 + TEXTURE_ENVMAP);
		djgt_push_hdrimage(djgt, path, 1);

		if (!djgt_to_gl(djgt, GL_TEXTURE_2D, GL_RGB9_E5, 1, 1, glt)) {
			LOG("=> Failure <=\n");
			djgt_release(djgt);

			return false;
		}
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glActiveTexture(GL_TEXTURE0);

		djgt_release(djgt);
	}
	return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load All Textures
 */
bool loadTextures()
{
	bool v = true;

	if (v) v&= loadSceneFramebufferTexture();
	if (v) v&= loadBackFramebufferTexture();
	if (v) v&= loadEnvmapTexture();
	if (v) v&= loadNpfTexture();
	if (v) v&= loadMerlTexture();

	return v;
}

////////////////////////////////////////////////////////////////////////////////
// Buffer Loading
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
/**
 * Load Sphere Data Buffers
 *
 * This procedure updates the transformations and the data of the spheres that
 * are used in the demo; it is updated each frame.
 */
bool loadSphereDataBuffers(float dt = 0)
{
	static bool first = true;
	struct Transform {
		dja::mat4 model, modelView, modelViewProjection, viewInv;
	} transform;

	if (first) {
		g_gl.streams[STREAM_TRANSFORM] = djgb_create(sizeof(transform));
		first = false;
	}

	// extract view and projection matrices
	dja::mat4 projection = dja::mat4::homogeneous::perspective(
		radians(g_camera.fovy),
		(float)g_framebuffer.w / (float)g_framebuffer.h,
		g_camera.zNear,
		g_camera.zFar
	);
	dja::mat4 viewInv = dja::mat4::homogeneous::translation(g_camera.pos)
	                  * dja::mat4::homogeneous::from_mat3(g_camera.axis);
	dja::mat4 view = dja::inverse(viewInv);


	// upload transformations
	transform.model     = dja::mat4(1.f);
	transform.modelView = view * transform.model;
	transform.modelViewProjection = projection * transform.modelView;
	transform.viewInv = viewInv;

	// upload planet data
	djgb_to_gl(g_gl.streams[STREAM_TRANSFORM], (const void *)&transform, NULL);
	djgb_glbindrange(g_gl.streams[STREAM_TRANSFORM],
	                 GL_UNIFORM_BUFFER,
	                 STREAM_TRANSFORM);

	return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load Random Buffer
 *
 * This buffer holds the random samples used by the GLSL Monte Carlo integrator.
 * It should be updated at each frame. The random samples are generated using
 * the Marsaglia pseudo-random generator.
 */
uint32_t mrand() // Marsaglia random generator
{
	static uint32_t m_z = 1, m_w = 2;

	m_z = 36969u * (m_z & 65535u) + (m_z >> 16u);
	m_w = 18000u * (m_w & 65535u) + (m_w >> 16u);

	return ((m_z << 16u) + m_w);
}

bool loadRandomBuffer()
{
	static bool first = true;
	float buffer[256];
	int offset = 0;

	if (first) {
		g_gl.streams[STREAM_RANDOM] = djgb_create(sizeof(buffer));
		first = false;
	}

	for (int i = 0; i < BUFFER_SIZE(buffer); ++i) {
		buffer[i] = (float)((double)mrand() / (double)0xFFFFFFFFu);
		assert(buffer[i] <= 1.f && buffer[i] >= 0.f);
	}

	djgb_to_gl(g_gl.streams[STREAM_RANDOM], (const void *)buffer, &offset);
	djgb_glbindrange(g_gl.streams[STREAM_RANDOM],
	                 GL_UNIFORM_BUFFER,
	                 STREAM_RANDOM);

	return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load Sphere Mesh Buffer
 *
 * This loads a vertex and an index buffer for a mesh.
 */
bool loadSphereMeshBuffers()
{
	int vertexCnt, indexCnt;
	djg_mesh *mesh = djgm_load_sphere(
		g_sphere.sphere.xTess, g_sphere.sphere.yTess
	);
	const djgm_vertex *vertices = djgm_get_vertices(mesh, &vertexCnt);
	const uint16_t *indexes = djgm_get_triangles(mesh, &indexCnt);

	if (glIsBuffer(g_gl.buffers[BUFFER_SPHERE_VERTICES]))
		glDeleteBuffers(1, &g_gl.buffers[BUFFER_SPHERE_VERTICES]);
	if (glIsBuffer(g_gl.buffers[BUFFER_SPHERE_INDEXES]))
		glDeleteBuffers(1, &g_gl.buffers[BUFFER_SPHERE_INDEXES]);

	LOG("Loading {Mesh-Vertex-Buffer}\n");
	glGenBuffers(1, &g_gl.buffers[BUFFER_SPHERE_VERTICES]);
	glBindBuffer(GL_ARRAY_BUFFER, g_gl.buffers[BUFFER_SPHERE_VERTICES]);
	glBufferData(GL_ARRAY_BUFFER,
	             sizeof(djgm_vertex) * vertexCnt,
	             (const void*)vertices,
	             GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	LOG("Loading {Mesh-Grid-Index-Buffer}\n");
	glGenBuffers(1, &g_gl.buffers[BUFFER_SPHERE_INDEXES]);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gl.buffers[BUFFER_SPHERE_INDEXES]);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,
	             sizeof(uint16_t) * indexCnt,
	             (const void *)indexes,
	             GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	g_sphere.sphere.indexCnt = indexCnt;
	g_sphere.sphere.vertexCnt = vertexCnt;
	djgm_release(mesh);

	return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load All Buffers
 *
 */
bool loadBuffers()
{
	bool v = true;

	v&= loadSphereDataBuffers();
	v&= loadRandomBuffer();
	v&= loadSphereMeshBuffers();

	return v;
}

////////////////////////////////////////////////////////////////////////////////
// Vertex Array Loading
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
/**
 * Load an Empty Vertex Array
 *
 * This will be used to draw procedural geometry, e.g., a fullscreen quad.
 */
bool loadEmptyVertexArray()
{
	LOG("Loading {Empty-VertexArray}\n");
	if (glIsVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]))
		glDeleteVertexArrays(1, &g_gl.vertexArrays[VERTEXARRAY_EMPTY]);

	glGenVertexArrays(1, &g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
	glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
	glBindVertexArray(0);

	return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load Mesh Vertex Array
 *
 * This will be used to draw the sphere mesh loaded with the dj_opengl library.
 */
bool loadSphereVertexArray()
{
	LOG("Loading {Mesh-VertexArray}\n");
	if (glIsVertexArray(g_gl.vertexArrays[VERTEXARRAY_SPHERE]))
		glDeleteVertexArrays(1, &g_gl.vertexArrays[VERTEXARRAY_SPHERE]);

	glGenVertexArrays(1, &g_gl.vertexArrays[VERTEXARRAY_SPHERE]);
	glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_SPHERE]);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glEnableVertexAttribArray(3);
	glBindBuffer(GL_ARRAY_BUFFER, g_gl.buffers[BUFFER_SPHERE_VERTICES]);
	glVertexAttribPointer(0, 4, GL_FLOAT, 0, sizeof(djgm_vertex),
	                      BUFFER_OFFSET(0));
	glVertexAttribPointer(1, 4, GL_FLOAT, 0, sizeof(djgm_vertex),
	                      BUFFER_OFFSET(4 * sizeof(float)));
	glVertexAttribPointer(2, 4, GL_FLOAT, 0, sizeof(djgm_vertex),
	                      BUFFER_OFFSET(8 * sizeof(float)));
	glVertexAttribPointer(3, 4, GL_FLOAT, 0, sizeof(djgm_vertex),
	                      BUFFER_OFFSET(12 * sizeof(float)));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gl.buffers[BUFFER_SPHERE_INDEXES]);
	glBindVertexArray(0);

	return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load All Vertex Arrays
 *
 */
bool loadVertexArrays()
{
	bool v = true;

	v&= loadEmptyVertexArray();
	v&= loadSphereVertexArray();

	return v;
}

////////////////////////////////////////////////////////////////////////////////
// Framebuffer Loading
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
/**
 * Load the Back Framebuffer
 *
 * This framebuffer contains the final image. It will be blitted to the
 * OpenGL window's backbuffer.
 */
bool loadBackFramebuffer()
{
	LOG("Loading {Back-Framebuffer}\n");
	if (glIsFramebuffer(g_gl.framebuffers[FRAMEBUFFER_BACK]))
		glDeleteFramebuffers(1, &g_gl.framebuffers[FRAMEBUFFER_BACK]);

	glGenFramebuffers(1, &g_gl.framebuffers[FRAMEBUFFER_BACK]);
	glBindFramebuffer(GL_FRAMEBUFFER, g_gl.framebuffers[FRAMEBUFFER_BACK]);
	glFramebufferTexture2D(GL_FRAMEBUFFER,
	                       GL_COLOR_ATTACHMENT0,
	                       GL_TEXTURE_2D,
	                       g_gl.textures[TEXTURE_BACK],
	                       0);

	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	if (GL_FRAMEBUFFER_COMPLETE != glCheckFramebufferStatus(GL_FRAMEBUFFER)) {
		LOG("=> Failure <=\n");

		return false;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Scene Framebuffer
 *
 * This framebuffer is used to draw the 3D scene.
 * A single framebuffer is created, holding a color and Z buffer.
 * The scene writes directly to it.
 */
bool loadSceneFramebuffer()
{
	LOG("Loading {Scene-Framebuffer}\n");
	if (glIsFramebuffer(g_gl.framebuffers[FRAMEBUFFER_SCENE]))
		glDeleteFramebuffers(1, &g_gl.framebuffers[FRAMEBUFFER_SCENE]);

	glGenFramebuffers(1, &g_gl.framebuffers[FRAMEBUFFER_SCENE]);
	glBindFramebuffer(GL_FRAMEBUFFER, g_gl.framebuffers[FRAMEBUFFER_SCENE]);

	if (g_framebuffer.aa >= AA_MSAA2 && g_framebuffer.aa <= AA_MSAA16) {
		glFramebufferTexture2D(GL_FRAMEBUFFER,
		                       GL_COLOR_ATTACHMENT0,
		                       GL_TEXTURE_2D_MULTISAMPLE,
		                       g_gl.textures[TEXTURE_SCENE],
		                       0);
		glFramebufferTexture2D(GL_FRAMEBUFFER,
		                       GL_DEPTH_STENCIL_ATTACHMENT,
		                       GL_TEXTURE_2D_MULTISAMPLE,
		                       g_gl.textures[TEXTURE_Z],
		                       0);
	} else {
		glFramebufferTexture2D(GL_FRAMEBUFFER,
		                       GL_COLOR_ATTACHMENT0,
		                       GL_TEXTURE_2D,
		                       g_gl.textures[TEXTURE_SCENE],
		                       0);
		glFramebufferTexture2D(GL_FRAMEBUFFER,
		                       GL_DEPTH_STENCIL_ATTACHMENT,
		                       GL_TEXTURE_2D,
		                       g_gl.textures[TEXTURE_Z],
		                       0);
	}

	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	if (GL_FRAMEBUFFER_COMPLETE != glCheckFramebufferStatus(GL_FRAMEBUFFER)) {
		LOG("=> Failure <=\n");

		return false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load All Framebuffers
 *
 */
bool loadFramebuffers()
{
	bool v = true;

	v&= loadBackFramebuffer();
	v&= loadSceneFramebuffer();

	return v;
}

////////////////////////////////////////////////////////////////////////////////
// OpenGL Resource Loading
//
////////////////////////////////////////////////////////////////////////////////

void init()
{
	bool v = true;
	int i;

	for (i = 0; i < CLOCK_COUNT; ++i) {
		if (g_gl.clocks[i])
			djgc_release(g_gl.clocks[i]);
		g_gl.clocks[i] = djgc_create();
	}

	if (v) v&= loadTextures();
	if (v) v&= loadBuffers();
	if (v) v&= loadFramebuffers();
	if (v) v&= loadVertexArrays();
	if (v) v&= loadPrograms();

	if (!v) throw std::exception();
}

void release()
{
	int i;

	for (i = 0; i < CLOCK_COUNT; ++i)
		if (g_gl.clocks[i])
			djgc_release(g_gl.clocks[i]);
	for (i = 0; i < STREAM_COUNT; ++i)
		if (g_gl.streams[i])
			djgb_release(g_gl.streams[i]);
	for (i = 0; i < PROGRAM_COUNT; ++i)
		if (glIsProgram(g_gl.programs[i]))
			glDeleteProgram(g_gl.programs[i]);
	for (i = 0; i < TEXTURE_COUNT; ++i)
		if (glIsTexture(g_gl.textures[i]))
			glDeleteTextures(1, &g_gl.textures[i]);
	for (i = 0; i < BUFFER_COUNT; ++i)
		if (glIsBuffer(g_gl.buffers[i]))
			glDeleteBuffers(1, &g_gl.buffers[i]);
	for (i = 0; i < FRAMEBUFFER_COUNT; ++i)
		if (glIsFramebuffer(g_gl.framebuffers[i]))
			glDeleteFramebuffers(1, &g_gl.framebuffers[i]);
	for (i = 0; i < VERTEXARRAY_COUNT; ++i)
		if (glIsVertexArray(g_gl.vertexArrays[i]))
			glDeleteVertexArrays(1, &g_gl.vertexArrays[i]);
}

////////////////////////////////////////////////////////////////////////////////
// OpenGL Rendering
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
/**
 * Render the Scene
 *
 * This drawing pass renders the 3D scene to the framebuffer.
 */
void renderSceneProgressive()
{
	// configure GL state
	glBindFramebuffer(GL_FRAMEBUFFER, g_gl.framebuffers[FRAMEBUFFER_SCENE]);
	glViewport(0, 0, g_framebuffer.w, g_framebuffer.h);
	glDepthFunc(GL_LESS);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	if (g_framebuffer.flags.reset) {
		glClearColor(0, 0, 0, g_framebuffer.samplesPerPass);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		g_framebuffer.pass = 0;
		g_framebuffer.flags.reset = false;
	}

	// enable blending only after the first is complete
	// (otherwise backfaces might be included in the rendering)
	if (g_framebuffer.pass > 0) {
		glDepthFunc(GL_LEQUAL);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);
		loadRandomBuffer();
	} else {
		glDepthFunc(GL_LESS);
		glDisable(GL_BLEND);
	}

	// stop progressive drawing once the desired sampling rate has been reached
	if (g_framebuffer.pass * g_framebuffer.samplesPerPass
		< g_framebuffer.samplesPerPixel) {

		// draw planets
		if (g_sphere.flags.showLines)
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

		glUseProgram(g_gl.programs[PROGRAM_SPHERE]);
		glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_SPHERE]);
		glDrawElements(GL_TRIANGLES,
		               g_sphere.sphere.indexCnt,
		               GL_UNSIGNED_SHORT,
		               NULL);

		if (g_sphere.flags.showLines)
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

		// draw background
		glUseProgram(g_gl.programs[PROGRAM_BACKGROUND]);
		glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		++g_framebuffer.pass;
	}

	// restore GL state
	if (g_framebuffer.pass > 0) {
		glDepthFunc(GL_LESS);
		glDisable(GL_BLEND);
	}
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
}

void renderScene()
{
	loadSphereDataBuffers(1.f);
	if (g_framebuffer.flags.progressive) {
		renderSceneProgressive();
	} else {
		int passCnt = g_framebuffer.samplesPerPixel
		            / g_framebuffer.samplesPerPass;

		if (!passCnt) passCnt = 1;
		for (int i = 0; i < passCnt; ++i) {
			loadRandomBuffer();
			renderSceneProgressive();
		}
	}
}

// -----------------------------------------------------------------------------
/**
 * Blit the Scene Framebuffer and draw GUI
 *
 * This drawing pass blits the scene framebuffer with possible magnification
 * and renders the HUD and TweakBar.
 */
void imguiSetAa()
{
	if (!loadSceneFramebufferTexture() || !loadSceneFramebuffer()
	|| !loadViewerProgram()) {
		LOG("=> Framebuffer config failed <=\n");
		throw std::exception();
	}
	g_framebuffer.flags.reset = true;
}

void renderViewer(double cpuDt, double gpuDt)
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_gl.framebuffers[FRAMEBUFFER_BACK]);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, g_gl.framebuffers[FRAMEBUFFER_SCENE]);
	glViewport(0, 0, g_app.viewer.w, g_app.viewer.h);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);

	// post process the scene framebuffer
	glUseProgram(g_gl.programs[PROGRAM_VIEWER]);
	glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	// draw HUD
	if (g_app.viewer.hud) {
		// ImGui
		ImGui_ImplGlfwGL3_NewFrame();
		// Viewer Widgets
		ImGui::SetNextWindowPos(ImVec2(270, 10)/*, ImGuiSetCond_FirstUseEver*/);
		ImGui::SetNextWindowSize(ImVec2(250, 120)/*, ImGuiSetCond_FirstUseEver*/);
		ImGui::Begin("Framebuffer");
		{
			const char* aaItems[] = {
				"None",
				"MSAA x2",
				"MSAA x4",
				"MSAA x8",
				"MSAA x16"
			};
			if (ImGui::Combo("AA", &g_framebuffer.aa, aaItems, BUFFER_SIZE(aaItems)))
				imguiSetAa();
			if (ImGui::Combo("MSAA", &g_framebuffer.msaa.fixed, "Fixed\0Random\0\0"))
				imguiSetAa();
			ImGui::Checkbox("Progressive", &g_framebuffer.flags.progressive);
			if (g_framebuffer.flags.progressive) {
				ImGui::SameLine();
				if (ImGui::Button("Reset"))
					g_framebuffer.flags.reset = true;
			}
		}
		ImGui::End();
		// Framebuffer Widgets
		ImGui::SetNextWindowPos(ImVec2(530, 10)/*, ImGuiSetCond_FirstUseEver*/);
		ImGui::SetNextWindowSize(ImVec2(250, 120)/*, ImGuiSetCond_FirstUseEver*/);
		ImGui::Begin("Viewer");
		{
			if (ImGui::SliderFloat("Exposure", &g_app.viewer.exposure, -3.0f, 3.0f))
				configureViewerProgram();
			if (ImGui::SliderFloat("Gamma", &g_app.viewer.gamma, 1.0f, 4.0f))
				configureViewerProgram();
			if (ImGui::Button("Take Screenshot")) {
				static int cnt = 0;
				char buf[1024];

				snprintf(buf, 1024, "screenshot%03i", cnt);
				glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
				djgt_save_glcolorbuffer_bmp(GL_FRONT, GL_RGBA, buf);
				++cnt;
			}
			if (ImGui::Button("Record"))
				g_app.recorder.on = !g_app.recorder.on;
			if (g_app.recorder.on) {
				ImGui::SameLine();
				ImGui::Text("Recording...");
			}
		}
		ImGui::End();
		// Camera Widgets
		ImGui::SetNextWindowPos(ImVec2(10, 10)/*, ImGuiSetCond_FirstUseEver*/);
		ImGui::SetNextWindowSize(ImVec2(250, 120)/*, ImGuiSetCond_FirstUseEver*/);
		ImGui::Begin("Camera");
		{
			if (ImGui::SliderFloat("FOVY", &g_camera.fovy, 1.0f, 179.0f))
				g_framebuffer.flags.reset = true;
			if (ImGui::SliderFloat("zNear", &g_camera.zNear, 0.01f, 100.f)) {
				if (g_camera.zNear >= g_camera.zFar)
					g_camera.zNear = g_camera.zFar - 0.01f;
			}
			if (ImGui::SliderFloat("zFar", &g_camera.zFar, 1.f, 1500.f)) {
				if (g_camera.zFar <= g_camera.zNear)
					g_camera.zFar = g_camera.zNear + 0.01f;
			}
		}
		ImGui::End();
		// Lighting/Planets Widgets
		ImGui::SetNextWindowPos(ImVec2(10, 140)/*, ImGuiSetCond_FirstUseEver*/);
		ImGui::SetNextWindowSize(ImVec2(250, 450)/*, ImGuiSetCond_FirstUseEver*/);
		ImGui::Begin("Sphere");
		{
			const char* shadingModes[] = {
				"MC Cos",
				"MC GGX",
				"MC MIS",
				"Debug"
			};
			const char* brdfModes[] = {
				"diffuse",
				"merl",
				"npf"
			};
			if (ImGui::Combo("Shading", &g_sphere.shading.mode, shadingModes, BUFFER_SIZE(shadingModes))) {
				loadSphereProgram();
				loadMerlTexture();
				g_framebuffer.flags.reset = true;
			}
			if (ImGui::Combo("Brdf", &g_sphere.shading.brdf, brdfModes, BUFFER_SIZE(brdfModes))) {
				loadSphereProgram();
				g_framebuffer.flags.reset = true;
			}
			if (g_sphere.shading.merl.cnt > 0) {
				if (ImGui::Combo("Merl", &g_sphere.shading.merl.id, g_sphere.shading.merl.files, g_sphere.shading.merl.cnt)) {
					loadMerlTexture();
					loadSphereProgram();
					g_framebuffer.flags.reset = true;
				}
			}
			if (g_sphere.shading.envmap.cnt > 0) {
				if (ImGui::Combo("Envmap", &g_sphere.shading.envmap.id, g_sphere.shading.envmap.files, g_sphere.shading.envmap.cnt)) {
					loadEnvmapTexture();
					loadSphereProgram();
					g_framebuffer.flags.reset = true;
				}
			}
			if (ImGui::CollapsingHeader("Flags", ImGuiTreeNodeFlags_DefaultOpen)) {
				if (ImGui::Checkbox("Wireframe", &g_sphere.flags.showLines))
					g_framebuffer.flags.reset = true;
			}
			if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
				if (ImGui::SliderInt("xTess", &g_sphere.sphere.xTess, 0, 128)) {
					loadSphereMeshBuffers();
					loadSphereVertexArray();
					g_framebuffer.flags.reset = true;
				}
				if (ImGui::SliderInt("yTess", &g_sphere.sphere.yTess, 0, 128)) {
					loadSphereMeshBuffers();
					loadSphereVertexArray();
					g_framebuffer.flags.reset = true;
				}
			}
		}
		ImGui::End();

		ImGui::Render();
		ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());
	}

	// screen recording
	if (g_app.recorder.on) {
		char name[64], path[1024];

		glBindFramebuffer(GL_READ_FRAMEBUFFER, g_gl.framebuffers[FRAMEBUFFER_BACK]);
		sprintf(name, "capture_%02i_%09i",
		        g_app.recorder.capture,
		        g_app.recorder.frame);
		strcat2(path, g_app.dir.output, name);
		djgt_save_glcolorbuffer_bmp(GL_COLOR_ATTACHMENT0, GL_RGB, path);
		++g_app.recorder.frame;
	}

	// restore state
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

// -----------------------------------------------------------------------------
/**
 * Blit the Composited Framebuffer to the Window Backbuffer
 *
 * Final drawing step: the composited framebuffer is blitted to the
 * OpenGL window backbuffer
 */
void renderBack()
{
	glBindFramebuffer(GL_READ_FRAMEBUFFER, g_gl.framebuffers[FRAMEBUFFER_BACK]);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

	// blit scene framebuffer
	glBlitFramebuffer(0, 0, g_app.viewer.w, g_app.viewer.h,
	                  0, 0, g_app.viewer.w, g_app.viewer.h,
	                  GL_COLOR_BUFFER_BIT,
	                  GL_NEAREST);
}

// -----------------------------------------------------------------------------
/**
 * Render Everything
 *
 */
void render()
{
	double cpuDt, gpuDt;

	djgc_start(g_gl.clocks[CLOCK_SPF]);
	renderScene();
	djgc_stop(g_gl.clocks[CLOCK_SPF]);
	djgc_ticks(g_gl.clocks[CLOCK_SPF], &cpuDt, &gpuDt);
	renderViewer(cpuDt, gpuDt);
	renderBack();
	++g_app.frame;
}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
void
keyboardCallback(
	GLFWwindow* window,
	int key, int scancode, int action, int modsls
) {
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureKeyboard)
		return;

	if (action == GLFW_PRESS) {
		switch (key) {
			case GLFW_KEY_ESCAPE:
				//glfwSetWindowShouldClose(window, GL_TRUE);
				g_app.viewer.hud = !g_app.viewer.hud;
			break;
			case GLFW_KEY_R:
				loadPrograms();
				g_framebuffer.flags.reset = true;
			break;
			default: break;
		}
	}
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureMouse)
		return;
}

void mouseMotionCallback(GLFWwindow* window, double x, double y)
{
	static double x0 = 0, y0 = 0;
	double dx = x - x0,
	       dy = y - y0;

	ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureMouse)
		return;

	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
		dja::mat3 axis = dja::transpose(g_camera.axis);
		g_camera.axis = dja::mat3::rotation(dja::vec3(0, 0, 1), dx * 5e-3)
					  * g_camera.axis;
		g_camera.axis = dja::mat3::rotation(axis[1], dy * 5e-3)
					  * g_camera.axis;
		g_camera.axis[0] = dja::normalize(g_camera.axis[0]);
		g_camera.axis[1] = dja::normalize(g_camera.axis[1]);
		g_camera.axis[2] = dja::normalize(g_camera.axis[2]);
		g_framebuffer.flags.reset = true;
	} else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
		dja::mat3 axis = dja::transpose(g_camera.axis);
		g_camera.pos-= axis[1] * dx * 5e-3 * norm(g_camera.pos);
		g_camera.pos+= axis[2] * dy * 5e-3 * norm(g_camera.pos);
		g_framebuffer.flags.reset = true;
	}

	x0 = x;
	y0 = y;
}

void mouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
	ImGuiIO& io = ImGui::GetIO();
	ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
	if (io.WantCaptureMouse)
		return;

	dja::mat3 axis = dja::transpose(g_camera.axis);
	g_camera.pos-= axis[0] * yoffset * 5e-2 * norm(g_camera.pos);
	g_framebuffer.flags.reset = true;
}

void usage(const char *app)
{
	printf("%s -- OpenGL Merl Renderer\n", app);
	printf("usage: %s --merl merl1 merl2 ... --envmap env1 env2 ... "
	       "--npf-data path_to_uber_texture_data --shader-dir path_to_shaders\n", app);
}

// -----------------------------------------------------------------------------
int main(int argc, const char **argv)
{
	for (int i = 1; i < argc; ++i) {
		if (!strcmp("--merl", argv[i])) {
			int cnt = 0;

			++i;
			do {++cnt;} while ((cnt < argc-i) && strncmp("-", argv[i+cnt], 1));
			g_sphere.shading.merl.files = argv + i;
			g_sphere.shading.merl.cnt = cnt;
			i+= cnt - 1;
			LOG("Note: number of MERL BRDFs set to %i\n", cnt);
		} else if (!strcmp("--envmap", argv[i])) {
			int cnt = 0;

			++i;
			do {++cnt;} while ((cnt < argc-i) && strncmp("-", argv[i+cnt], 1));
			g_sphere.shading.envmap.files = argv + i;
			g_sphere.shading.envmap.cnt = cnt;
			i+= cnt - 1;
			LOG("Note: number of Envmaps set to %i\n", cnt);
		} else if (!strcmp("--shader-dir", argv[i])) {
			g_app.dir.shader = argv[++i];
			LOG("Note: shader dir set to %s\n", g_app.dir.shader);
		} else if (!strcmp("--npf-data", argv[i])) {
			g_sphere.shading.pathToUberData = argv[++i];
			LOG("Note: NPF data set to %s\n", g_sphere.shading.pathToUberData);
		}
	}
	if (g_sphere.shading.merl.cnt == 0 ||
	    g_sphere.shading.envmap.cnt == 0 ||
	    g_sphere.shading.pathToUberData == NULL) {
		usage(argv[0]);
		return 0;
	}

	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);

	// Create the Window
	LOG("Loading {Window-Main}\n");
	GLFWwindow* window = glfwCreateWindow(
	                         VIEWER_DEFAULT_WIDTH,
	                         VIEWER_DEFAULT_HEIGHT,
	                         "Hello MERL", NULL, NULL
	                         );
	if (window == NULL) {
		LOG("=> Failure <=\n");
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);
	glfwSetKeyCallback(window, &keyboardCallback);
	glfwSetCursorPosCallback(window, &mouseMotionCallback);
	glfwSetMouseButtonCallback(window, &mouseButtonCallback);
	glfwSetScrollCallback(window, &mouseScrollCallback);

	// Load OpenGL functions
	LOG("Loading {OpenGL}\n");
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		LOG("gladLoadGLLoader failed\n");
		return -1;
	}

	LOG("-- Begin -- Demo\n");
	try {
		//log_debug_output();
		ImGui::CreateContext();
		ImGui_ImplGlfwGL3_Init(window, false);
		ImGui::StyleColorsDark();
		init();

		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();

			glClearColor(0.8, 0.8, 0.8, 1.0);
			glClear(GL_COLOR_BUFFER_BIT);

			render();

			glfwSwapBuffers(window);
		}

		release();
		ImGui_ImplGlfwGL3_Shutdown();
		ImGui::DestroyContext();
		glfwTerminate();
	} catch (std::exception& e) {
		LOG("%s", e.what());
		ImGui_ImplGlfwGL3_Shutdown();
		ImGui::DestroyContext();
		glfwTerminate();
		LOG("(!) Demo Killed (!)\n");

		return EXIT_FAILURE;
	} catch (...) {
		ImGui_ImplGlfwGL3_Shutdown();
		ImGui::DestroyContext();
		glfwTerminate();
		LOG("(!) Demo Killed (!)\n");

		return EXIT_FAILURE;
	}
	LOG("-- End -- Demo\n");


	return 0;
}

