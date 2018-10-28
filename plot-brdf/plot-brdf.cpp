////////////////////////////////////////////////////////////////////////////////
//
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
#define VIEWER_DEFAULT_WIDTH  1024
#define VIEWER_DEFAULT_HEIGHT 1024

// default path to the directory holding the source files
#ifndef PATH_TO_SRC_DIRECTORY
#   define PATH_TO_SRC_DIRECTORY "./"
#endif
#ifndef PATH_TO_ASSET_DIRECTORY
#   define PATH_TO_ASSSET_DIRECTORY "../assets/"
#endif

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
    VIEWER_DEFAULT_WIDTH, VIEWER_DEFAULT_HEIGHT, AA_MSAA8, 0, 1, 1,
    {true, true},
    {false},
    {61./255., 119./255., 192./225}
};

// -----------------------------------------------------------------------------
// Camera Manager
#define INIT_POS dja::vec3(2.5f)
struct CameraManager {
    float fovy, zNear, zFar; // perspective settings
    dja::vec3 pos;           // 3D position
    dja::mat3 axis;          // 3D frame
} g_camera = {
    45.f, 0.01f, 1024.f,
    INIT_POS,
    dja::mat3::lookat(dja::vec3(0), INIT_POS, dja::vec3(0, 0, 1))
};
#undef INIT_POS

// -----------------------------------------------------------------------------
// Sphere Manager
enum {
    SHADING_COLOR,
    SHADING_COLORMAP,
    SHADING_BRDF,
    SHADING_DEBUG
};
enum { BRDF_GGX, BRDF_MERL };
enum { SCHEME_MERL, SCHEME_GGX };
struct SphereManager {
    struct {
        bool showSurface, showLines, showWiHelper,
        showSamples, showParametric;
    } flags;
    struct {
        int xTess, yTess;
        int vertexCnt, indexCnt;
        struct {float r, g, b, a;} color;
    } sphere;
    struct {
        int vertexCnt, instanceCnt;
    } circles;
    struct {
        int scheme;
    } samples;
    struct {
        struct {
            std::vector<const char *> files;
            int id;
        } merl;
        const char *pathToCmap;
        int mode;
    } shading;
    struct {
        float thetaI, phiI;
        float ggxAlpha;
        int id;
    } brdf;
} g_sphere = {
    {true, true, true, false, false},
    {32, 64, -1, -1, {0.1f, 0.5f, 0.1f, 0.65f}}, // sphere
    {256, 7},          // circles
    { SCHEME_MERL },   // GGX
    {
        {{PATH_TO_ASSET_DIRECTORY "./gold-metallic-paint2.binary"}, 0},
        PATH_TO_ASSET_DIRECTORY "cmap_hot.png",
        SHADING_BRDF
    },
    {45.f, 255.f, 1.f, BRDF_MERL}
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
    /*dir*/    {PATH_TO_SRC_DIRECTORY "./shaders/", "./"},
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
enum { STREAM_SPHERES, STREAM_TRANSFORM, STREAM_RANDOM, STREAM_COUNT };
enum {
    VERTEXARRAY_EMPTY,
    VERTEXARRAY_SPHERE,
    VERTEXARRAY_CIRCLE,
    VERTEXARRAY_COUNT
};
enum {
    TEXTURE_BACK,
    TEXTURE_SCENE,
    TEXTURE_Z,
    TEXTURE_MERL,
    TEXTURE_CMAP,
    TEXTURE_COUNT
};
enum {
    BUFFER_SPHERE_VERTICES,
    BUFFER_SPHERE_INDEXES,
    BUFFER_CIRCLE_VERTICES,
    BUFFER_MERL,
    BUFFER_COUNT
};
enum {
    PROGRAM_VIEWER,
    PROGRAM_BACKGROUND,
    PROGRAM_SPHERE,
    PROGRAM_WIRE,
    PROGRAM_SAMPLES,
    PROGRAM_HELPER_WI_DIR,
    PROGRAM_HELPER_WI_ANGLE,
    PROGRAM_PARAMETRIC,
    PROGRAM_COUNT
};
enum {
    UNIFORM_VIEWER_FRAMEBUFFER_SAMPLER,
    UNIFORM_VIEWER_VIEWPORT,

    UNIFORM_BACKGROUND_CLEAR_COLOR,
    UNIFORM_BACKGROUND_ENVMAP_SAMPLER,

    UNIFORM_SPHERE_EXPOSURE,
    UNIFORM_SPHERE_GAMMA,
    UNIFORM_SPHERE_SAMPLES_PER_PASS,
    UNIFORM_SPHERE_MERL_SAMPLER,
    UNIFORM_SPHERE_ALPHA,
    UNIFORM_SPHERE_COLOR,
    UNIFORM_SPHERE_CMAP_SAMPLER,
    UNIFORM_SPHERE_DIR,

    UNIFORM_HELPER_WI_DIR_DIR,

    UNIFORM_HELPER_WI_ANGLE_DIR,

    UNIFORM_WIRE_INSTANCE_COUNT,

    UNIFORM_SAMPLES_DIR,
    UNIFORM_SAMPLES_ALPHA,
    UNIFORM_SAMPLES_POINT_SCALE,

    UNIFORM_PARAMETRIC_DIR,
    UNIFORM_PARAMETRIC_ALPHA,
    UNIFORM_PARAMETRIC_EXPOSURE,
    UNIFORM_PARAMETRIC_COLOR,
    UNIFORM_PARAMETRIC_MERL_SAMPLER,
    UNIFORM_PARAMETRIC_CMAP_SAMPLER,

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

dja::vec3 s2_to_r3(float theta, float phi)
{
    theta = radians(theta);
    phi = radians(phi);
    float tmp = sin(theta);
    return dja::vec3(tmp * cos(phi), tmp * sin(phi), cos(theta));
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
    } else if(severity == GL_DEBUG_SEVERITY_LOW) {
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
}

// -----------------------------------------------------------------------------
//
void configureHelperWiDirProgram()
{
    dja::vec3 wi = s2_to_r3(g_sphere.brdf.thetaI, g_sphere.brdf.phiI);

    glProgramUniform3f(g_gl.programs[PROGRAM_HELPER_WI_DIR],
                       g_gl.uniforms[UNIFORM_HELPER_WI_DIR_DIR],
                       wi.x, wi.y, wi.z);
}
void configureHelperWiAngleProgram()
{
    dja::vec3 wi = s2_to_r3(g_sphere.brdf.thetaI, g_sphere.brdf.phiI);

    glProgramUniform3f(g_gl.programs[PROGRAM_HELPER_WI_ANGLE],
                       g_gl.uniforms[UNIFORM_HELPER_WI_ANGLE_DIR],
                       wi.x, wi.y, wi.z);
}
void configureSamplesProgram()
{
    dja::vec3 wi = s2_to_r3(g_sphere.brdf.thetaI, g_sphere.brdf.phiI);

    glProgramUniform3f(g_gl.programs[PROGRAM_SAMPLES],
                       g_gl.uniforms[UNIFORM_SAMPLES_DIR],
                       wi.x, wi.y, wi.z);
    glProgramUniform1f(g_gl.programs[PROGRAM_SAMPLES],
                       g_gl.uniforms[UNIFORM_SAMPLES_ALPHA],
                       g_sphere.brdf.ggxAlpha);

}

// -----------------------------------------------------------------------------
// set Sphere program uniforms
void configureSphereProgram()
{
    dja::vec3 wi = s2_to_r3(g_sphere.brdf.thetaI, g_sphere.brdf.phiI);

    glProgramUniform3f(g_gl.programs[PROGRAM_SPHERE],
                       g_gl.uniforms[UNIFORM_SPHERE_DIR],
                       wi.x, wi.y, wi.z);
    glProgramUniform1i(g_gl.programs[PROGRAM_SPHERE],
                       g_gl.uniforms[UNIFORM_SPHERE_SAMPLES_PER_PASS],
                       g_framebuffer.samplesPerPass);
    glProgramUniform1i(g_gl.programs[PROGRAM_SPHERE],
                       g_gl.uniforms[UNIFORM_SPHERE_MERL_SAMPLER],
                       TEXTURE_MERL);
    glProgramUniform1i(g_gl.programs[PROGRAM_SPHERE],
                       g_gl.uniforms[UNIFORM_SPHERE_CMAP_SAMPLER],
                       TEXTURE_CMAP);
    glProgramUniform1f(g_gl.programs[PROGRAM_SPHERE],
                       g_gl.uniforms[UNIFORM_SPHERE_ALPHA],
                       g_sphere.brdf.ggxAlpha);
    glProgramUniform4f(g_gl.programs[PROGRAM_SPHERE],
                       g_gl.uniforms[UNIFORM_SPHERE_COLOR],
                       g_sphere.sphere.color.r,
                       g_sphere.sphere.color.g,
                       g_sphere.sphere.color.b,
                       g_sphere.sphere.color.a);
    glProgramUniform1f(g_gl.programs[PROGRAM_SPHERE],
                       g_gl.uniforms[UNIFORM_SPHERE_EXPOSURE],
                       g_app.viewer.exposure);
    glProgramUniform1f(g_gl.programs[PROGRAM_SPHERE],
                       g_gl.uniforms[UNIFORM_SPHERE_GAMMA],
                       g_app.viewer.gamma);
}
void configureParametricProgram()
{
    dja::vec3 wi = s2_to_r3(g_sphere.brdf.thetaI, g_sphere.brdf.phiI);

    glProgramUniform3f(g_gl.programs[PROGRAM_PARAMETRIC],
                       g_gl.uniforms[UNIFORM_PARAMETRIC_DIR],
                       wi.x, wi.y, wi.z);
    glProgramUniform1i(g_gl.programs[PROGRAM_PARAMETRIC],
                       g_gl.uniforms[UNIFORM_PARAMETRIC_MERL_SAMPLER],
                       TEXTURE_MERL);
    glProgramUniform1i(g_gl.programs[PROGRAM_PARAMETRIC],
                       g_gl.uniforms[UNIFORM_PARAMETRIC_CMAP_SAMPLER],
                       TEXTURE_CMAP);
    glProgramUniform1f(g_gl.programs[PROGRAM_PARAMETRIC],
                       g_gl.uniforms[UNIFORM_PARAMETRIC_ALPHA],
                       g_sphere.brdf.ggxAlpha);
    glProgramUniform1f(g_gl.programs[PROGRAM_PARAMETRIC],
                       g_gl.uniforms[UNIFORM_PARAMETRIC_EXPOSURE],
                       g_app.viewer.exposure);
    glProgramUniform4f(g_gl.programs[PROGRAM_PARAMETRIC],
                       g_gl.uniforms[UNIFORM_PARAMETRIC_COLOR],
                       g_sphere.sphere.color.r,
                       g_sphere.sphere.color.g,
                       g_sphere.sphere.color.b,
                       g_sphere.sphere.color.a);
}

// -----------------------------------------------------------------------------
// set Sphere program uniforms
void configureWireProgram()
{
    glProgramUniform1i(g_gl.programs[PROGRAM_WIRE],
                       g_gl.uniforms[UNIFORM_WIRE_INSTANCE_COUNT],
                       g_sphere.circles.instanceCnt);
}

// -----------------------------------------------------------------------------
//
void configurePrograms()
{
    configureBackgroundProgram();
    configureWireProgram();
    configureSphereProgram();
    configureParametricProgram();
    configureHelperWiDirProgram();
    configureHelperWiAngleProgram();
    configureSamplesProgram();
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

    LOG("Loading {Viewer-Program}\n");
    if (g_framebuffer.aa >= AA_MSAA2 && g_framebuffer.aa <= AA_MSAA16)
        djgp_push_string(djp, "#define MSAA_FACTOR %i\n", 1 << g_framebuffer.aa);
    if (g_sphere.shading.mode == SHADING_BRDF)
        djgp_push_string(djp, "#define FLAG_TONEMAP 1\n");

    FILE *pf = fopen(strcat2(buf, g_app.dir.shader, "viewer.glsl"), "r");
    if (!pf) abort();

    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "viewer.glsl"));

    if (!djgp_to_gl(djp, 450, false, true, program)) {
        LOG("=> Failure <=\n");
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    g_gl.uniforms[UNIFORM_VIEWER_FRAMEBUFFER_SAMPLER] =
        glGetUniformLocation(g_gl.programs[PROGRAM_VIEWER], "u_FramebufferSampler");
    g_gl.uniforms[UNIFORM_VIEWER_VIEWPORT] =
        glGetUniformLocation(g_gl.programs[PROGRAM_VIEWER], "u_Viewport");

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
    djgp_push_string(djp, "#define BUFFER_BINDING_TRANSFORMS %i\n", STREAM_TRANSFORM);
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
    switch (g_sphere.shading.mode) {
        case SHADING_DEBUG:
            djgp_push_string(djp, "#define SHADE_DEBUG 1\n");
            break;
        case SHADING_COLOR:
            djgp_push_string(djp, "#define SHADE_COLOR 1\n");
            break;
        case SHADING_BRDF:
            djgp_push_string(djp, "#define SHADE_BRDF 1\n");
            break;
        case SHADING_COLORMAP:
            djgp_push_string(djp, "#define SHADE_CMAP 1\n");
            break;
    };
    if (g_sphere.brdf.id == BRDF_MERL)
        djgp_push_string(djp, "#define BRDF_MERL 1\n");
    djgp_push_string(djp, "#define BUFFER_BINDING_RANDOM %i\n", STREAM_RANDOM);
    djgp_push_string(djp, "#define BUFFER_BINDING_TRANSFORMS %i\n", STREAM_TRANSFORM);
    djgp_push_string(djp, "#define BUFFER_BINDING_SPHERES %i\n", STREAM_SPHERES);
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "ggx.glsl"));
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "brdf_merl.glsl"));
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "sphere.glsl"));

    if (!djgp_to_gl(djp, 450, false, true, program)) {
        LOG("=> Failure <=\n");
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    g_gl.uniforms[UNIFORM_SPHERE_SAMPLES_PER_PASS] =
        glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_SamplesPerPass");
    g_gl.uniforms[UNIFORM_SPHERE_MERL_SAMPLER] =
        glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_MerlSampler");
    g_gl.uniforms[UNIFORM_SPHERE_ALPHA] =
        glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_Alpha");
    g_gl.uniforms[UNIFORM_SPHERE_COLOR] =
        glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_Color");
    g_gl.uniforms[UNIFORM_SPHERE_DIR] =
        glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_Dir");
    g_gl.uniforms[UNIFORM_SPHERE_CMAP_SAMPLER] =
        glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_CmapSampler");
    g_gl.uniforms[UNIFORM_SPHERE_EXPOSURE] =
        glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_Exposure");
    g_gl.uniforms[UNIFORM_SPHERE_GAMMA] =
        glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_Gamma");

    configureSphereProgram();

    return (glGetError() == GL_NO_ERROR);
}

bool loadParametricProgram()
{
    djg_program *djp = djgp_create();
    GLuint *program = &g_gl.programs[PROGRAM_PARAMETRIC];
    char buf[1024];

    LOG("Loading {Parametric-Program}\n");
    switch (g_sphere.shading.mode) {
        case SHADING_DEBUG:
            djgp_push_string(djp, "#define SHADE_DEBUG 1\n");
            break;
        case SHADING_COLOR:
            djgp_push_string(djp, "#define SHADE_COLOR 1\n");
            break;
        case SHADING_BRDF:
            djgp_push_string(djp, "#define SHADE_BRDF 1\n");
            break;
        case SHADING_COLORMAP:
            djgp_push_string(djp, "#define SHADE_CMAP 1\n");
            break;
    };
    if (g_sphere.brdf.id == BRDF_MERL)
        djgp_push_string(djp, "#define BRDF_MERL 1\n");
    if (g_sphere.samples.scheme == SCHEME_GGX) {
        djgp_push_string(djp, "#define SCHEME_GGX 1\n");
    }

    djgp_push_string(djp, "#define BUFFER_BINDING_RANDOM %i\n", STREAM_RANDOM);
    djgp_push_string(djp, "#define BUFFER_BINDING_TRANSFORMS %i\n", STREAM_TRANSFORM);
    djgp_push_string(djp, "#define BUFFER_BINDING_SPHERE %i\n", STREAM_SPHERES);
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "ggx.glsl"));
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "brdf_merl.glsl"));
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "parametric.glsl"));

    if (!djgp_to_gl(djp, 450, false, true, program)) {
        LOG("=> Failure <=\n");
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    g_gl.uniforms[UNIFORM_PARAMETRIC_MERL_SAMPLER] =
        glGetUniformLocation(g_gl.programs[PROGRAM_PARAMETRIC], "u_MerlSampler");
    g_gl.uniforms[UNIFORM_PARAMETRIC_DIR] =
        glGetUniformLocation(g_gl.programs[PROGRAM_PARAMETRIC], "u_Dir");
    g_gl.uniforms[UNIFORM_PARAMETRIC_ALPHA] =
        glGetUniformLocation(g_gl.programs[PROGRAM_PARAMETRIC], "u_Alpha");
    g_gl.uniforms[UNIFORM_PARAMETRIC_CMAP_SAMPLER] =
        glGetUniformLocation(g_gl.programs[PROGRAM_PARAMETRIC], "u_CmapSampler");
    g_gl.uniforms[UNIFORM_PARAMETRIC_EXPOSURE] =
        glGetUniformLocation(g_gl.programs[PROGRAM_PARAMETRIC], "u_Exposure");
    g_gl.uniforms[UNIFORM_PARAMETRIC_COLOR] =
        glGetUniformLocation(g_gl.programs[PROGRAM_PARAMETRIC], "u_Color");

    configureParametricProgram();

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Sphere Wire Program
 *
 * This program is responsible for rendering the shadow of the S2
 */
bool loadWireProgram()
{
    djg_program *djp = djgp_create();
    GLuint *program = &g_gl.programs[PROGRAM_WIRE];
    char buf[1024];

    LOG("Loading {Wire-Program}\n");
    djgp_push_string(djp, "#define BUFFER_BINDING_TRANSFORMS %i\n", STREAM_TRANSFORM);
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "wire.glsl"));

    if (!djgp_to_gl(djp, 450, false, true, program)) {
        LOG("=> Failure <=\n");
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    g_gl.uniforms[UNIFORM_WIRE_INSTANCE_COUNT] =
        glGetUniformLocation(g_gl.programs[PROGRAM_WIRE], "u_InstanceCount");

    configureWireProgram();

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Wi Helper Program
 *
 */
bool loadHelperWiDirProgram()
{
    djg_program *djp = djgp_create();
    GLuint *program = &g_gl.programs[PROGRAM_HELPER_WI_DIR];
    char buf[1024];

    LOG("Loading {Wi-Dir-Helper-Program}\n");
    djgp_push_string(djp, "#define BUFFER_BINDING_TRANSFORMS %i\n", STREAM_TRANSFORM);
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "wi_dir.glsl"));

    if (!djgp_to_gl(djp, 450, false, true, program)) {
        LOG("=> Failure <=\n");
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    g_gl.uniforms[UNIFORM_HELPER_WI_DIR_DIR] =
        glGetUniformLocation(g_gl.programs[PROGRAM_HELPER_WI_DIR], "u_Dir");

    configureHelperWiDirProgram();

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Wi Helper Program
 *
 */
bool loadHelperWiAngleProgram()
{
    djg_program *djp = djgp_create();
    GLuint *program = &g_gl.programs[PROGRAM_HELPER_WI_ANGLE];
    char buf[1024];

    LOG("Loading {Wi-Angle-Helper-Program}\n");
    djgp_push_string(djp, "#define BUFFER_BINDING_TRANSFORMS %i\n", STREAM_TRANSFORM);
    djgp_push_string(djp, "#define VERTEX_CNT %i\n", g_sphere.circles.vertexCnt);
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "wi_angle.glsl"));

    if (!djgp_to_gl(djp, 450, false, true, program)) {
        LOG("=> Failure <=\n");
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    g_gl.uniforms[UNIFORM_HELPER_WI_ANGLE_DIR] =
        glGetUniformLocation(g_gl.programs[PROGRAM_HELPER_WI_ANGLE], "u_Dir");

    configureHelperWiAngleProgram();

    return (glGetError() == GL_NO_ERROR);
}


// -----------------------------------------------------------------------------
/**
 * Load the sample Program
 *
 */
bool loadSamplesProgram()
{
    djg_program *djp = djgp_create();
    GLuint *program = &g_gl.programs[PROGRAM_SAMPLES];
    char buf[1024];

    LOG("Loading {Samples-Program}\n");
    djgp_push_string(djp, "#define BUFFER_BINDING_TRANSFORMS %i\n", STREAM_TRANSFORM);
    if (g_sphere.samples.scheme == SCHEME_GGX) {
        djgp_push_string(djp, "#define SCHEME_GGX 1\n");
        djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "ggx.glsl"));
    }
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "samples.glsl"));

    if (!djgp_to_gl(djp, 450, false, true, program)) {
        LOG("=> Failure <=\n");
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    g_gl.uniforms[UNIFORM_SAMPLES_DIR] =
        glGetUniformLocation(g_gl.programs[PROGRAM_SAMPLES], "u_Dir");
    g_gl.uniforms[UNIFORM_SAMPLES_ALPHA] =
        glGetUniformLocation(g_gl.programs[PROGRAM_SAMPLES], "u_Alpha");

    configureSamplesProgram();

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

    if (v) v&= loadViewerProgram();
    if (v) v&= loadBackgroundProgram();
    if (v) v&= loadSphereProgram();
    if (v) v&= loadParametricProgram();
    if (v) v&= loadWireProgram();
    if (v) v&= loadHelperWiDirProgram();
    if (v) v&= loadHelperWiAngleProgram();
    if (v) v&= loadSamplesProgram();

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
 * Load the MERL Texture
 *
 */
static bool loadMerlTexture(void)
{
    if (!g_sphere.shading.merl.files.empty()) {
        try {
        LOG("Loading {MERL-BRDF}\n");

        djb::merl merl(g_sphere.shading.merl.files[g_sphere.shading.merl.id]);
        djb::tab_r tab(merl, 90);
        djb::microfacet::args args = djb::tab_r::extract_ggx_args(djb::tab_r(merl, 90));
        g_sphere.brdf.ggxAlpha = args.minv[0][0];

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
 * Load the Colormap Texture
 *
 * This loads a texture used as a colormap.
 */
bool loadColormapTexture()
{
    LOG("Loading {Colormap-Texture}\n");
    if (g_sphere.shading.pathToCmap) {
        char buf[1024];

        if (glIsTexture(g_gl.textures[TEXTURE_CMAP]))
            glDeleteTextures(1, &g_gl.textures[TEXTURE_CMAP]);
        glGenTextures(1, &g_gl.textures[TEXTURE_CMAP]);

        djg_texture *djgt = djgt_create(0);
        GLuint *glt = &g_gl.textures[TEXTURE_CMAP];

        glActiveTexture(GL_TEXTURE0 + TEXTURE_CMAP);
        djgt_push_image_u8(djgt, g_sphere.shading.pathToCmap, false);

        if (!djgt_to_gl(djgt, GL_TEXTURE_1D, GL_RGBA8, 1, 0, glt)) {
            LOG("=> Failure <=\n");
            djgt_release(djgt);

            return false;
        }
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
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
    if (v) v&= loadMerlTexture();
    if (v) v&= loadColormapTexture();

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
        dja::mat4 modelView, projection, modelViewProjection, viewInv;
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
    transform.projection = projection;
    transform.modelView  = view * dja::mat4(1);
    transform.modelViewProjection = transform.projection * transform.modelView;
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
    djg_mesh *mesh = djgm_load_hemisphere(
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
 * Load Circle Vertex Buffer
 *
 * This loads a vertex buffer for a circle.
 */
bool loadCircleVertexBuffer()
{
    int vertexCnt = g_sphere.circles.vertexCnt;
    dja::vec4 *vertexBuffer = new dja::vec4[vertexCnt];

    for (int i = 0; i < vertexCnt; ++i) {
        float u = (float)i / vertexCnt;
        float phi = u * 2 * M_PI;
        float x = cos(phi), y = sin(phi);

        vertexBuffer[i] = dja::vec4(x, y, 0, 1);
    }

    LOG("Loading {Circle-Vertex-Buffer}\n");
    glGenBuffers(1, &g_gl.buffers[BUFFER_CIRCLE_VERTICES]);
    glBindBuffer(GL_ARRAY_BUFFER, g_gl.buffers[BUFFER_CIRCLE_VERTICES]);
    glBufferData(GL_ARRAY_BUFFER,
                 sizeof(dja::vec4) * vertexCnt,
                 (const void*)vertexBuffer,
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    delete[] vertexBuffer;

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

    if (v) v&= loadSphereDataBuffers();
    if (v) v&= loadRandomBuffer();
    if (v) v&= loadSphereMeshBuffers();
    if (v) v&= loadCircleVertexBuffer();

    return v;
}

////////////////////////////////////////////////////////////////////////////////
// Vertex Array Loading
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
/**
 * Load a Circle Vertex Array
 *
 */
bool loadCircleVertexArray()
{
    LOG("Loading {Circle-VertexArray}\n");
    if (glIsVertexArray(g_gl.vertexArrays[VERTEXARRAY_CIRCLE]))
        glDeleteVertexArrays(1, &g_gl.vertexArrays[VERTEXARRAY_CIRCLE]);

    glGenVertexArrays(1, &g_gl.vertexArrays[VERTEXARRAY_CIRCLE]);
    glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_CIRCLE]);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, g_gl.buffers[BUFFER_CIRCLE_VERTICES]);
    glVertexAttribPointer(0, 4, GL_FLOAT, 0, 0, BUFFER_OFFSET(0));
    glBindVertexArray(0);

    return (glGetError() == GL_NO_ERROR);
}

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

    if (v) v&= loadEmptyVertexArray();
    if (v) v&= loadSphereVertexArray();
    if (v) v&= loadCircleVertexArray();

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

        // draw background
        glUseProgram(g_gl.programs[PROGRAM_BACKGROUND]);
        glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // parametric space
        if (g_sphere.flags.showParametric) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glUseProgram(g_gl.programs[PROGRAM_PARAMETRIC]);
            glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glDisable(GL_BLEND);
        }

        // draw wire
        if (g_sphere.flags.showLines) {
            glLineWidth(1.5f);
            glUseProgram(g_gl.programs[PROGRAM_WIRE]);
            glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_CIRCLE]);
            glDrawArraysInstanced(GL_LINE_LOOP,
                                  0,
                                  g_sphere.circles.vertexCnt,
                                  g_sphere.circles.instanceCnt * 2);
        }

        // draw samples
        if (g_sphere.flags.showSamples) {
            glEnable(GL_PROGRAM_POINT_SIZE);
            glUseProgram(g_gl.programs[PROGRAM_SAMPLES]);
            glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
            glDrawArrays(GL_POINTS,
                         0,
                         256);
            glDisable(GL_PROGRAM_POINT_SIZE);
        }

        // draw wi helper
        if (g_sphere.flags.showWiHelper) {
            glLineWidth(3.0f);
            glUseProgram(g_gl.programs[PROGRAM_HELPER_WI_DIR]);
            glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
            glDrawArrays(GL_LINES, 0, 2);

            glLineWidth(2.5f);
            glUseProgram(g_gl.programs[PROGRAM_HELPER_WI_ANGLE]);
            glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_CIRCLE]);
            glDrawArrays(GL_LINE_LOOP,
                         0,
                         g_sphere.circles.vertexCnt);
        }


        // draw sphere
        if (g_sphere.flags.showSurface) {
#if 1
            glDisable(GL_CULL_FACE);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glUseProgram(g_gl.programs[PROGRAM_SPHERE]);
            glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_SPHERE]);
            glDrawElements(GL_TRIANGLES,
                           g_sphere.sphere.indexCnt,
                           GL_UNSIGNED_SHORT,
                           NULL);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthFunc(GL_EQUAL);
            glDepthMask(GL_FALSE);
            glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_SPHERE]);
            glDrawElements(GL_TRIANGLES,
                           g_sphere.sphere.indexCnt,
                           GL_UNSIGNED_SHORT,
                           NULL);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glEnable(GL_CULL_FACE);
#else
            glUseProgram(g_gl.programs[PROGRAM_SPHERE]);
            glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_SPHERE]);
            glDrawElements(GL_TRIANGLES,
                           g_sphere.sphere.indexCnt,
                           GL_UNSIGNED_SHORT,
                           NULL);
#endif
        }

        ++g_framebuffer.pass;
        g_framebuffer.flags.reset = true;
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
        glUseProgram(0);
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
            if (ImGui::SliderFloat("Exposure", &g_app.viewer.exposure, -3.0f, 3.0f)) {
                configureSphereProgram();
                configureParametricProgram();
                g_framebuffer.flags.reset = true;
            }
            if (ImGui::Button("Take Screenshot")) {
                static int cnt = 0;
                char buf[1024];

                snprintf(buf, 1024, "screenshot%03i", cnt);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                djgt_save_glcolorbuffer_png(GL_FRONT, GL_RGBA, buf);
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
                "Color",
                "Colormap",
                "BRDF",
                "Debug"
            };
            if (ImGui::Combo("Shading", &g_sphere.shading.mode, shadingModes, BUFFER_SIZE(shadingModes))) {
                loadSphereProgram();
                loadParametricProgram();
                loadViewerProgram();
                g_framebuffer.flags.reset = true;
            }
            if (!g_sphere.shading.merl.files.empty() > 0) {
                if (ImGui::Combo("Merl", &g_sphere.shading.merl.id, &g_sphere.shading.merl.files[0], g_sphere.shading.merl.files.size())) {
                    loadMerlTexture();
                    loadSphereProgram();
                    loadParametricProgram();
                    g_framebuffer.flags.reset = true;
                }
            }
            if (ImGui::CollapsingHeader("Flags", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Checkbox("Surface", &g_sphere.flags.showSurface))
                    g_framebuffer.flags.reset = true;
                if (ImGui::Checkbox("Wireframe", &g_sphere.flags.showLines))
                    g_framebuffer.flags.reset = true;
                if (ImGui::Checkbox("HelperWiDir", &g_sphere.flags.showWiHelper))
                    g_framebuffer.flags.reset = true;
                if (ImGui::Checkbox("Samples", &g_sphere.flags.showSamples))
                    g_framebuffer.flags.reset = true;
                if (ImGui::Checkbox("Parametric", &g_sphere.flags.showParametric))
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
                if (ImGui::ColorEdit4("Color", &g_sphere.sphere.color.r)) {
                    g_framebuffer.flags.reset = true;
                    configureSphereProgram();
                    configureParametricProgram();
                }
            }
            if (ImGui::CollapsingHeader("BRDF Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (!g_sphere.shading.merl.files.empty()) {
                    const char* brdfModes[] = {
                        "GGX",
                        "Merl"
                    };
                    if (ImGui::Combo("BRDF", &g_sphere.brdf.id, brdfModes, BUFFER_SIZE(brdfModes))) {
                        loadSphereProgram();
                        loadParametricProgram();
                        g_framebuffer.flags.reset = true;
                    }
                }
                if  (ImGui::SliderFloat("Alpha", &g_sphere.brdf.ggxAlpha, 0.0f, 1.f)) {
                    configurePrograms();
                    g_framebuffer.flags.reset = true;
                }
                if (ImGui::SliderFloat("thetaI", &g_sphere.brdf.thetaI, 0.0f, 89.f)) {
                    configurePrograms();
                    g_framebuffer.flags.reset = true;
                }
                if (ImGui::SliderFloat("PhiI", &g_sphere.brdf.phiI, 0.0f, 360.f)) {
                    configurePrograms();
                    g_framebuffer.flags.reset = true;
                }
            }
            if (ImGui::CollapsingHeader("Sample Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* samplingSchemes[] = {
                    "Merl",
                    "GGX"
                };
                if (ImGui::Combo("Scheme", &g_sphere.samples.scheme, samplingSchemes, BUFFER_SIZE(samplingSchemes))) {
                    loadSamplesProgram();
                    loadParametricProgram();
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
    printf("usage: %s [OPTION]\n\n", app);
    printf("Options\n"
           "  -h --help\n"
           "     Print help\n\n"
           "  --output-dir path_to_output_directory/\n"
           "     Specify the output directory\n"
           "     (default is ./)\n\n"
           "  --shader-dir path_to_shader_directory/\n"
           "     Specify the shader directory\n"
           "     (default is ./shaders/)\n\n"
           "  --record\n"
           "     Enables recorder\n"
           "     (disabled by default)\n\n"
           "  --hidden\n"
           "     Starts the application minimized\n"
           "     (disabled by default)\n\n"
           "  --no-hud\n"
           "     Disables HUD rendering\n"
           "     (enabled by default)\n\n"
           "  --cmap\n"
           "     Specifies a colormap\n"
           "     (null by default)\n\n"
           );
}

// -----------------------------------------------------------------------------
int main(int argc, const char **argv)
{
    GLenum startVisible = GL_TRUE;

    #define PARSE_SHADING_MODE(str, enumval)               \
    else if (!strcmp("--shading-" str, argv[i])) {      \
            g_sphere.shading.mode = enumval;              \
            LOG("Note: shading mode set to " str "\n");  \
    }
    #define PARSE_SAMPLING_SCHEME(str, enumval)               \
    else if (!strcmp("--scheme-" str, argv[i])) {      \
            g_sphere.samples.scheme = enumval;              \
            LOG("Note: scheme set to " str "\n");  \
    }
    #define PARSE_SPHERE_FLAG(str, flag)                    \
    else if (!strcmp("--enable-" str, argv[i])) {           \
        flag = true;                                        \
        LOG("Note: rendering flag " str " set to true\n");   \
    } else if  (!strcmp("--disable-" str, argv[i])) {       \
        flag = false;                                       \
        LOG("Note: rendering flag " str " set to false\n");  \
    }

    for (int i = 1; i < argc; ++i) {
        if (!strcmp("--merl", argv[i])) {
            g_sphere.shading.merl.files.resize(0);
            do {
                g_sphere.shading.merl.files.push_back(argv[++i]);
            } while ((i+1 < argc) && strncmp("-", argv[i+1], 1));
            LOG("Note: number of MERL BRDFs set to %i\n", (int)g_sphere.shading.merl.files.size());
        } else if (!strcmp("--output-dir", argv[i])) {
            g_app.dir.output = (const char *)argv[++i];
            LOG("Note: output directory set to %s\n", g_app.dir.output);
        } else if (!strcmp("--shader-dir", argv[i])) {
            g_app.dir.shader = (const char *)argv[++i];
            LOG("Note: shader directory set to %s\n", g_app.dir.shader);
        } else if (!strcmp("--record", argv[i])) {
            g_app.recorder.on = true;
            LOG("Note: recording enabled\n");
        } else if (!strcmp("--no-hud", argv[i])) {
            g_app.viewer.hud = false;
            LOG("Note: HUD rendering disabled\n");
        } else if (!strcmp("--hidden", argv[i])) {
            startVisible = GL_FALSE;
            LOG("Note: viewer will run hidden\n");
        } else if (!strcmp("--frame-limit", argv[i])) {
            g_app.frameLimit = atoi(argv[++i]);
            LOG("Note: frame limit set to %i\n", g_app.frameLimit);
        } else if (!strcmp("--cmap", argv[i])) {
            g_sphere.shading.pathToCmap = argv[++i];
            LOG("Note: cmap set to: %s\n", g_sphere.shading.pathToCmap);
        } else if (!strcmp("--dir", argv[i])) {
            g_sphere.brdf.thetaI = atof(argv[++i]);
            g_sphere.brdf.phiI = atof(argv[++i]);
            LOG("Note: wi set to: (%f %f)\n", g_sphere.brdf.thetaI, g_sphere.brdf.phiI);
        } else if (!strcmp("--alpha", argv[i])) {
            g_sphere.brdf.ggxAlpha = atof(argv[++i]);
            LOG("Note: GGX alpha set to: %f\n", g_sphere.brdf.ggxAlpha);
        }  else if (!strcmp("--sc", argv[i])) {
            g_sphere.brdf.ggxAlpha = atof(argv[++i]);
            LOG("Note: GGX alpha set to: %f\n", g_sphere.brdf.ggxAlpha);
        } else if (!strcmp("--color", argv[i])) {
            g_sphere.sphere.color.r = atof(argv[++i]);
            g_sphere.sphere.color.g = atof(argv[++i]);
            g_sphere.sphere.color.b = atof(argv[++i]);
            g_sphere.sphere.color.a = atof(argv[++i]);
            LOG("Note: surface color set to: (%f %f %f %f)\n",
                g_sphere.sphere.color.r,
                g_sphere.sphere.color.g,
                g_sphere.sphere.color.b,
                g_sphere.sphere.color.a);
        }
        PARSE_SPHERE_FLAG("sphere-lines", g_sphere.flags.showLines)
        PARSE_SPHERE_FLAG("sphere-surface", g_sphere.flags.showSurface)
        PARSE_SPHERE_FLAG("sphere-samples", g_sphere.flags.showSamples)
        PARSE_SPHERE_FLAG("sphere-wi-helper", g_sphere.flags.showWiHelper)
        PARSE_SPHERE_FLAG("parametric", g_sphere.flags.showParametric)
        PARSE_SHADING_MODE("color", SHADING_COLOR)
        PARSE_SHADING_MODE("cmap" , SHADING_COLORMAP)
        PARSE_SHADING_MODE("brdf" , SHADING_BRDF)
        PARSE_SAMPLING_SCHEME("ggx", SCHEME_GGX)
        PARSE_SAMPLING_SCHEME("merl", SCHEME_MERL)
    }

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
    glfwWindowHint(GLFW_VISIBLE, startVisible);

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
    log_debug_output();

    LOG("-- Begin -- Demo\n");
    try {
        //log_debug_output();
        ImGui::CreateContext();
        ImGui_ImplGlfwGL3_Init(window, false);
        ImGui::StyleColorsDark();
        init();

        while (!glfwWindowShouldClose(window) && (uint32_t)g_app.frame < (uint32_t)g_app.frameLimit) {
            glfwPollEvents();

            render();
            ++g_app.frame;

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

