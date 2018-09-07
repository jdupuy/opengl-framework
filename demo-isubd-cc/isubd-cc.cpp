////////////////////////////////////////////////////////////////////////////////
// Implicit Subdivition for Terrain Rendering
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
#define VIEWER_DEFAULT_WIDTH  1680
#define VIEWER_DEFAULT_HEIGHT 1050

// default path to the directory holding the source files
#ifndef PATH_TO_SRC_DIRECTORY
#   define PATH_TO_SRC_DIRECTORY "./"
#endif

////////////////////////////////////////////////////////////////////////////////
// Global Variables
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// Framebuffer Manager
enum { AA_NONE, AA_MSAA2, AA_MSAA4, AA_MSAA8, AA_MSAA16 };
struct FramebufferManager {
    int w, h, aa;
    struct {int fixed;} msaa;
    struct {float r, g, b;} clearColor;
} g_framebuffer = {
    VIEWER_DEFAULT_WIDTH, VIEWER_DEFAULT_HEIGHT, AA_MSAA2,
    {false},
    {61./255., 119./255., 192./225}
};

// -----------------------------------------------------------------------------
// Camera Manager
#define INIT_POS dja::vec3(0, 0, 1)
struct CameraManager {
    float fovy, zNear, zFar; // perspective settings
    dja::vec3 pos;           // 3D position
    dja::mat3 axis;          // 3D frame
} g_camera = {
    55.f, 0.001f, 1024.f,
    INIT_POS,
    dja::mat3::lookat(
        dja::vec3(1.5f, 1.5f, 0),
        INIT_POS,
        dja::vec3(0, 0, 1)
    )
};
#undef INIT_POS

// -----------------------------------------------------------------------------
// Quadtree Manager
enum {METHOD_TS, METHOD_GS};
struct PatchManager {
    dja::vec4 vertices[16];
    struct {bool uniform, cull, freeze, wire, reset, net;} flags;
    int method;
    int gpuSubd, uniformSubd;
    int pingPong;
    float primitivePixelLengthTarget;
} g_patch = {
    {
        {0, 0, 0, 1},
        {1, 0, 0, 1},
        {2, 0, 0, 1},
        {3, 0, 0, 1},
        {0, 1, 0, 1},
        {1, 1, 1, 1},
        {2, 1, 1, 1},
        {3, 1, 0, 1},
        {0, 2, 0, 1},
        {1, 2, 1, 1},
        {2, 2, 1, 1},
        {3, 2, 0, 1},
        {0, 3, 0, 1},
        {1, 3, 0, 1},
        {2, 3, 0, 1},
        {3, 3, 0, 1}
    },
    {true, false, false, true, true, true},
    METHOD_TS,
    0, 5,
    0,
    10.f
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
    /*dir*/     {
                    PATH_TO_SRC_DIRECTORY "./shaders/",
                    PATH_TO_SRC_DIRECTORY "./"
                },
    /*viewer*/  {
                   VIEWER_DEFAULT_WIDTH, VIEWER_DEFAULT_HEIGHT,
                   true,
                   2.2f, 0.4f
                },
    /*record*/  {false, 0, 0},
    /*frame*/   0, -1
};

// -----------------------------------------------------------------------------
// OpenGL Manager
enum { CLOCK_SPF, CLOCK_COUNT };
enum { FRAMEBUFFER_BACK, FRAMEBUFFER_SCENE, FRAMEBUFFER_COUNT };
enum { STREAM_TRANSFORM, STREAM_SUBD_COUNTER, STREAM_COUNT };
enum {
    VERTEXARRAY_EMPTY,
    VERTEXARRAY_COUNT
};
enum {
    TEXTURE_BACK,
    TEXTURE_SCENE,
    TEXTURE_Z,
    TEXTURE_DMAP,
    TEXTURE_COUNT
};
enum {
    BUFFER_GEOMETRY_VERTICES = STREAM_COUNT,
    BUFFER_GEOMETRY_INDEXES,
    BUFFER_SUBD1,
    BUFFER_SUBD2,
    BUFFER_PATCH,
    BUFFER_COUNT
};
enum {
    PROGRAM_VIEWER,
    PROGRAM_CC,
    PROGRAM_CCNET,
    PROGRAM_COUNT
};
enum {
    UNIFORM_VIEWER_FRAMEBUFFER_SAMPLER,
    UNIFORM_VIEWER_EXPOSURE,
    UNIFORM_VIEWER_GAMMA,
    UNIFORM_VIEWER_VIEWPORT,

    UNIFORM_CC_LOD_FACTOR,

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
// set terrain program uniforms
void configureCatmullClarkProgram()
{
    float lodFactor = 2.0f * tan(radians(g_camera.fovy) / 2.0f)
                    / g_framebuffer.w * (1 << g_patch.gpuSubd)
                    * g_patch.primitivePixelLengthTarget;

    glProgramUniform1f(g_gl.programs[PROGRAM_CC],
                       g_gl.uniforms[UNIFORM_CC_LOD_FACTOR],
                       lodFactor);
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
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "viewer.glsl"));
    LOG("loading: %s\n", strcat2(buf, g_app.dir.shader, "viewer.glsl"));
    if (!djgp_to_gl(djp, 450, false, true, program)) {
        LOG("=> Failure <=\n");
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    g_gl.uniforms[UNIFORM_VIEWER_FRAMEBUFFER_SAMPLER] =
        glGetUniformLocation(g_gl.programs[PROGRAM_VIEWER], "u_FramebufferSampler");
    g_gl.uniforms[UNIFORM_VIEWER_EXPOSURE] =
        glGetUniformLocation(g_gl.programs[PROGRAM_VIEWER], "u_Exposure");
    g_gl.uniforms[UNIFORM_VIEWER_GAMMA] =
        glGetUniformLocation(g_gl.programs[PROGRAM_VIEWER], "u_Gamma");

    configureViewerProgram();

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Catmull Clark Program
 *
 * This program renders an adaptive terrain using the implicit subdivision
 * technique discribed in XXX.
 */
bool loadCatmullClarkProgram()
{
    djg_program *djp = djgp_create();
    GLuint *program = &g_gl.programs[PROGRAM_CC];
    char buf[1024];

    LOG("Loading {CC-Program}\n");
    if (g_patch.flags.cull)
        djgp_push_string(djp, "#define FLAG_CULL 1\n");
    if (g_patch.flags.freeze)
        djgp_push_string(djp, "#define FLAG_FREEZE 1\n");
    if (g_patch.flags.uniform) {
        djgp_push_string(djp, "#define FLAG_UNIFORM 1\n");
        djgp_push_string(djp, "#define UNIFORM_SUBD_FACTOR %i\n", g_patch.uniformSubd);
    }

    djgp_push_string(djp, "#define BUFFER_BINDING_PATCH %i\n", BUFFER_PATCH);
    djgp_push_string(djp, "#define PATCH_TESS_LEVEL %i\n", 1 << g_patch.gpuSubd);
    djgp_push_string(djp, "#define BUFFER_BINDING_TRANSFORMS %i\n", STREAM_TRANSFORM);
    djgp_push_string(djp, "#define BUFFER_BINDING_SUBD_COUNTER %i\n", STREAM_SUBD_COUNTER);
    djgp_push_string(djp, "#define BUFFER_BINDING_SUBD1 %i\n", BUFFER_SUBD1);
    djgp_push_string(djp, "#define BUFFER_BINDING_SUBD2 %i\n", BUFFER_SUBD2);
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "fcull.glsl"));
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "isubd.glsl"));
    if (g_patch.method == METHOD_TS)
        djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "cc.glsl"));
    else if (g_patch.method == METHOD_GS) {
        int edgeCnt = 1 << g_patch.gpuSubd;
        int vertexCnt = 0;

        for (int i = 0; i < edgeCnt; ++i) {
            vertexCnt+= 2 * i + 3;
        }

        djgp_push_string(djp, "#define VERTICES_OUT %i\n", vertexCnt);
        djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "cc_gs.glsl"));
    }
    if (!djgp_to_gl(djp, 450, false, true, program)) {
        LOG("=> Failure <=\n");
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    g_gl.uniforms[UNIFORM_CC_LOD_FACTOR] =
        glGetUniformLocation(g_gl.programs[PROGRAM_CC], "u_LodFactor");

    configureCatmullClarkProgram();

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Catmull Clark Net Program
 *
 * This program renders the net of the CC patch using a geometry shader.
 */
bool loadCatmullClarkNetProgram()
{
    djg_program *djp = djgp_create();
    GLuint *program = &g_gl.programs[PROGRAM_CCNET];
    char buf[1024];

    LOG("Loading {CCNet-Program}\n");
    djgp_push_string(djp, "#define BUFFER_BINDING_PATCH %i\n", BUFFER_PATCH);
    djgp_push_string(djp, "#define BUFFER_BINDING_TRANSFORMS %i\n", STREAM_TRANSFORM);
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "ccnet.glsl"));
    if (!djgp_to_gl(djp, 450, false, true, program)) {
        LOG("=> Failure <=\n");
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

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
    if (v) v&= loadCatmullClarkProgram();
    if (v) v&= loadCatmullClarkNetProgram();

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
 * Load All Textures
 */
bool loadTextures()
{
    bool v = true;

    if (v) v&= loadSceneFramebufferTexture();
    if (v) v&= loadBackFramebufferTexture();

    return v;
}

////////////////////////////////////////////////////////////////////////////////
// Buffer Loading
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
/**
 * Load Transform Buffer
 *
 * This procedure updates the transformation matrices; it is updated each frame.
 */
bool loadTransformBuffer()
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


    // set transformations
    transform.projection = projection;
    transform.modelView  = view * dja::mat4(1);
    transform.modelViewProjection = transform.projection * transform.modelView;
    transform.viewInv = viewInv;

    // upload to GPU
    djgb_to_gl(g_gl.streams[STREAM_TRANSFORM], (const void *)&transform, NULL);
    djgb_glbindrange(g_gl.streams[STREAM_TRANSFORM],
                     GL_UNIFORM_BUFFER,
                     STREAM_TRANSFORM);

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Geometry Buffer
 *
 * This procedure loads the scene geometry into an index and
 * vertex buffer. Here, we only load 2 triangles to define the
 * terrain.
 */
bool loadPatchBuffer()
{
    LOG("Loading {Patch-Vertex-Buffer}\n");
    if (glIsBuffer(g_gl.buffers[BUFFER_PATCH]))
        glDeleteBuffers(1, &g_gl.buffers[BUFFER_PATCH]);
    glGenBuffers(1, &g_gl.buffers[BUFFER_PATCH]);
    glBindBuffer(GL_ARRAY_BUFFER, g_gl.buffers[BUFFER_PATCH]);
    glBufferData(GL_ARRAY_BUFFER,
                 sizeof(g_patch.vertices),
                 (const void*)g_patch.vertices,
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_PATCH,
                     g_gl.buffers[BUFFER_PATCH]);

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Subdivision Buffers
 *
 * This procedure allocates and initialises the subdivision buffers.
 * We allocate 256 MBytes of memory to store the data.
 */
void loadSubdBuffer(int id, size_t bufferCapacity)
{
    const uint32_t data[] = {0, 1};

    if (glIsBuffer(g_gl.buffers[id]))
        glDeleteBuffers(1, &g_gl.buffers[id]);
    glGenBuffers(1, &g_gl.buffers[id]);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_gl.buffers[id]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferCapacity, NULL, GL_STATIC_DRAW);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                    0, sizeof(data), (const GLvoid *)data);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, id, g_gl.buffers[id]);
}

bool loadSubdivisionBuffers()
{
    LOG("Loading {Subd-Buffer}\n");
    const size_t bufferCapacity = 1 << 28;

    loadSubdBuffer(BUFFER_SUBD1, bufferCapacity);
    loadSubdBuffer(BUFFER_SUBD2, bufferCapacity);

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load Subd Counter Buffer
 *
 * This procedure creates a buffer that stores indirect drawing commands.
 */
bool loadSubdCounterBuffer(int *bufferOffset = NULL)
{
    static bool first = true;
    struct DrawArraysIndirect {
        uint32_t count, primCount, first, baseInstance;
    } drawCmd = {0, 1, 0, 0};

    if (first) {
        g_gl.streams[STREAM_SUBD_COUNTER] = djgb_create(sizeof(drawCmd));
        first = false;
    }

    // upload to GPU
    djgb_to_gl(g_gl.streams[STREAM_SUBD_COUNTER],
               (const void *)&drawCmd,
               bufferOffset);
    djgb_glbindrange(g_gl.streams[STREAM_SUBD_COUNTER],
                     GL_ATOMIC_COUNTER_BUFFER,
                     STREAM_SUBD_COUNTER);

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

    if (v) v&= loadTransformBuffer();
    if (v) v&= loadPatchBuffer();
    if (v) v&= loadSubdivisionBuffers();
    if (v) v&= loadSubdCounterBuffer();

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
 * Load All Vertex Arrays
 *
 */
bool loadVertexArrays()
{
    bool v = true;

    if (v) v&= loadEmptyVertexArray();

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

    if (v) v&= loadBackFramebuffer();
    if (v) v&= loadSceneFramebuffer();

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
void renderSceneTs(int offset) {
    // render terrain
    if (g_patch.flags.reset) {
        loadSubdivisionBuffers();
        g_patch.pingPong = 0;

        glDrawArrays(GL_PATCHES, 0, 1);

        g_patch.flags.reset = false;
    } else {
        glMemoryBarrier(GL_ATOMIC_COUNTER_BARRIER_BIT);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                         BUFFER_SUBD1,
                         g_gl.buffers[BUFFER_SUBD1 + 1 - g_patch.pingPong]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                         BUFFER_SUBD2,
                         g_gl.buffers[BUFFER_SUBD1 + g_patch.pingPong]);
        glDrawArraysIndirect(GL_PATCHES, BUFFER_OFFSET(offset));
        g_patch.pingPong = 1 - g_patch.pingPong;
    }
}

// -----------------------------------------------------------------------------
void renderSceneGs(int offset) {
    // render terrain
    if (g_patch.flags.reset) {
        loadSubdivisionBuffers();
        g_patch.pingPong = 0;

        glDrawArrays(GL_POINTS, 0, 2);

        g_patch.flags.reset = false;
    } else {
        glMemoryBarrier(GL_ATOMIC_COUNTER_BARRIER_BIT);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                         BUFFER_SUBD1,
                         g_gl.buffers[BUFFER_SUBD1 + 1 - g_patch.pingPong]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                         BUFFER_SUBD2,
                         g_gl.buffers[BUFFER_SUBD1 + g_patch.pingPong]);
        glDrawArraysIndirect(GL_POINTS, BUFFER_OFFSET(offset));
        g_patch.pingPong = 1 - g_patch.pingPong;
    }
}

void renderScene()
{
    static int offset = 0;
    int nextOffset = 0;

    // configure GL state
    glBindFramebuffer(GL_FRAMEBUFFER, g_gl.framebuffers[FRAMEBUFFER_SCENE]);
    glViewport(0, 0, g_framebuffer.w, g_framebuffer.h);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glPatchParameteri(GL_PATCH_VERTICES, 1);
    if (g_patch.flags.wire)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    // clear framebuffer
    glClearColor(0.5, 0.5, 0.5, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // bind buffers
    djgb_glbind(g_gl.streams[STREAM_SUBD_COUNTER], GL_DRAW_INDIRECT_BUFFER);
    loadSubdCounterBuffer(&nextOffset);
    loadTransformBuffer();
    glUseProgram(g_gl.programs[PROGRAM_CC]);
    glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);

    // render terrain
    if (g_patch.method == METHOD_TS)
        renderSceneTs(offset);
    else if (g_patch.method == METHOD_GS)
        renderSceneGs(offset);

    offset = nextOffset;

    // reset GL state
    if (g_patch.flags.wire)
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // render the CC net
    if (g_patch.flags.net) {
        glPointSize(10.f);
        glUseProgram(g_gl.programs[PROGRAM_CCNET]);
        glDrawArrays(GL_POINTS, 0, 16);
    }

    // reset GL state
    glDisable(GL_DEPTH_TEST);
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
}

void renderGui(double cpuDt, double gpuDt)
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
            if (ImGui::Button("Screenshot")) {
                static int cnt = 0;
                char buf[1024];

                snprintf(buf, 1024, "screenshot%03i", cnt);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                djgt_save_glcolorbuffer_png(GL_FRONT, GL_RGBA, buf);
                ++cnt;
            }
            ImGui::SameLine();
            if (ImGui::Button("Record"))
                g_app.recorder.on = !g_app.recorder.on;
            if (g_app.recorder.on) {
                ImGui::SameLine();
                ImGui::Text("Recording...");
            }
        }
        ImGui::End();
#if 0
        // Framebuffer Widgets
        ImGui::SetNextWindowPos(ImVec2(530, 10)/*, ImGuiSetCond_FirstUseEver*/);
        ImGui::SetNextWindowSize(ImVec2(250, 120)/*, ImGuiSetCond_FirstUseEver*/);
        ImGui::Begin("Viewer");
        {
            if (ImGui::SliderFloat("Exposure", &g_app.viewer.exposure, -3.0f, 3.0f))
                configureViewerProgram();
            if (ImGui::SliderFloat("Gamma", &g_app.viewer.gamma, 1.0f, 4.0f))
                configureViewerProgram();
        }
        ImGui::End();
#endif
        // Camera Widgets
        ImGui::SetNextWindowPos(ImVec2(10, 10)/*, ImGuiSetCond_FirstUseEver*/);
        ImGui::SetNextWindowSize(ImVec2(250, 120)/*, ImGuiSetCond_FirstUseEver*/);
        ImGui::Begin("Camera");
        {
            if (ImGui::SliderFloat("FOVY", &g_camera.fovy, 1.0f, 179.0f)) {
                configureCatmullClarkProgram();
            }
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
        // Subd Patch Widgets
        ImGui::SetNextWindowPos(ImVec2(10, 140)/*, ImGuiSetCond_FirstUseEver*/);
        ImGui::SetNextWindowSize(ImVec2(510, 580)/*, ImGuiSetCond_FirstUseEver*/);
        ImGui::Begin("Patch");
        {
            const char* eMethods[] = {
                "Tessellation Shader",
                "Geometry Shader"
            };
            ImGui::Text("CPU_dt: %.3f %s",
                        cpuDt < 1. ? cpuDt * 1e3 : cpuDt,
                        cpuDt < 1. ? "ms" : " s");
            ImGui::SameLine();
            ImGui::Text("GPU_dt: %.3f %s",
                        gpuDt < 1. ? gpuDt * 1e3 : gpuDt,
                        gpuDt < 1. ? "ms" : " s");
            if (ImGui::Combo("Method", &g_patch.method, eMethods, BUFFER_SIZE(eMethods))) {
                loadCatmullClarkProgram();
                g_patch.flags.reset = true;
            }
            ImGui::Text("flags: ");
            ImGui::SameLine();
            if (ImGui::Checkbox("uniform", &g_patch.flags.uniform))
                loadCatmullClarkProgram();
            ImGui::SameLine();
            if (ImGui::Checkbox("cull", &g_patch.flags.cull))
                loadCatmullClarkProgram();
            ImGui::SameLine();
            ImGui::Checkbox("wire", &g_patch.flags.wire);
            ImGui::SameLine();
            if (ImGui::Checkbox("freeze", &g_patch.flags.freeze))
                loadCatmullClarkProgram();
            ImGui::SameLine();
            ImGui::Checkbox("net", &g_patch.flags.net);

            if (ImGui::SliderInt("PatchSubdLevel", &g_patch.gpuSubd, 0, 6)) {
                loadCatmullClarkProgram();
                g_patch.flags.reset = true;
            }
            if (g_patch.flags.uniform) {
                if (ImGui::SliderInt("SubdLevel", &g_patch.uniformSubd, 0, 15)) {
                    loadCatmullClarkProgram();
                }
            } else {
                if (ImGui::SliderFloat("ScreenRes", &g_patch.primitivePixelLengthTarget, 1, 64)) {
                    configureCatmullClarkProgram();
                }
            }
            ImGui::Text("control patch vertices:");
            for (int i = 0; i < BUFFER_SIZE(g_patch.vertices); ++i) {
                char name[64];

                sprintf(name, "v%02i", i);
                if (ImGui::SliderFloat3(name, &(g_patch.vertices[i].x), -4.0f, 4.0f)) {
                    loadPatchBuffer();
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
    renderGui(cpuDt, gpuDt);
    renderBack();
    ++g_app.frame;
}


////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
void
keyboardCallback(
    GLFWwindow* window,
    int key, int, int action, int
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
                g_patch.flags.reset = true;
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
    } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        dja::mat3 axis = dja::transpose(g_camera.axis);
        g_camera.pos-= axis[1] * dx * 5e-3 * norm(g_camera.pos);
        g_camera.pos+= axis[2] * dy * 5e-3 * norm(g_camera.pos);
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
}

void usage(const char *app)
{
    printf("%s -- OpenGL Terrain Renderer\n", app);
    printf("usage: %s --shader-dir path_to_shader_dir\n", app);
}

// -----------------------------------------------------------------------------
int main(int argc, char **argv)
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);

    // Create the Window
    LOG("Loading {Window-Main}\n");
    GLFWwindow* window = glfwCreateWindow(
        VIEWER_DEFAULT_WIDTH, VIEWER_DEFAULT_HEIGHT,
        "Implicit GPU Subdivision Demo", NULL, NULL
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
        log_debug_output();
        ImGui::CreateContext();
        ImGui_ImplGlfwGL3_Init(window, false);
        ImGui::StyleColorsDark();
        init();

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

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

