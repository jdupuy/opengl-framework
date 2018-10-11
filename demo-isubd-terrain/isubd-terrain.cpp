/* isubd-terrain.cpp - public domain Implicit Subdivition for Terrain Rendering
    (created by Jonathan Dupuy)
*/

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_impl.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <algorithm>

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
#ifndef PATH_TO_ASSET_DIRECTORY
#   define PATH_TO_ASSSET_DIRECTORY "../assets/"
#endif

//Forces use of ad-hoc instanced geometry definition, with better vertex reuse
#define USE_ADHOC_INSTANCED_GEOM        1

////////////////////////////////////////////////////////////////////////////////
// Global Variables
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// Framebuffer Manager
enum { AA_NONE, AA_MSAA2, AA_MSAA4, AA_MSAA8, AA_MSAA16 };
struct FramebufferManager {
    int w, h, aa;
    struct { int fixed; } msaa;
    struct { float r, g, b; } clearColor;
} g_framebuffer = {
    VIEWER_DEFAULT_WIDTH, VIEWER_DEFAULT_HEIGHT, AA_NONE,
    {false},
    {61.0f / 255.0f, 119.0f / 255.0f, 192.0f / 255.0f}
};

// -----------------------------------------------------------------------------
// Camera Manager
#define INIT_POS dja::vec3(0.5f, 0.0f, 0.5f)
struct CameraManager {
    float fovy, zNear, zFar; // perspective settings
    dja::vec3 pos;           // 3D position
    dja::mat3 axis;          // 3D frame
} g_camera = {
    55.f, 0.0001f, 32.f,
    INIT_POS,
    dja::mat3::lookat(
        dja::vec3(0.f, 0.f, 0.2f),
        INIT_POS,
        dja::vec3(0, 0, 1)
    )
};
#undef INIT_POS

// -----------------------------------------------------------------------------
// Quadtree Manager
enum { METHOD_TS, METHOD_GS, METHOD_CS, METHOD_MS };
enum { SHADING_DIFFUSE, SHADING_NORMALS, SHADING_LOD };
struct TerrainManager {
    struct { bool displace, cull, freeze, wire, reset, freeze_step; } flags;
    struct {
        std::string pathToFile;
        float scale;
    } dmap;
    int method, computeThreadCount;
    int shading;
    int gpuSubd;
    int pingPong;
    float primitivePixelLengthTarget;
} g_terrain = {
    {true, true, false, false, true, false},
    {std::string(PATH_TO_ASSET_DIRECTORY "./dmap.png"), 0.45f},
    METHOD_CS, 5,
    SHADING_DIFFUSE,
    3,    //
    0,
    5.f
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
enum {
    STREAM_TRANSFORM,
    STREAM_COUNT
};
enum {
    VERTEXARRAY_EMPTY,
    VERTEXARRAY_INSTANCED_GRID, // compute-based pipeline only
    VERTEXARRAY_COUNT
};
enum {
    TEXTURE_BACK,
    TEXTURE_SCENE,
    TEXTURE_Z,
    TEXTURE_DMAP,
    TEXTURE_SMAP,
    TEXTURE_COUNT
};
enum {
    BUFFER_GEOMETRY_VERTICES = STREAM_COUNT,
    BUFFER_GEOMETRY_INDEXES,
    BUFFER_SUBD1, BUFFER_SUBD2,
    BUFFER_CULLED_SUBD1,                        // compute-based pipeline only
    BUFFER_INSTANCED_GEOMETRY_VERTICES,         // compute-based pipeline only
    BUFFER_INSTANCED_GEOMETRY_INDEXES,          // compute-based pipeline only
    BUFFER_DISPATCH_INDIRECT,                   // compute-based pipeline only
    BUFFER_DRAW_INDIRECT,
    BUFFER_ATOMIC_COUNTER,                      // New Atomic counter buffer
    BUFFER_ATOMIC_COUNTER2,                     // Just for the binding index
    BUFFER_COUNT
};
//Atomic counters bindings
enum {
    BINDING_ATOMIC_COUNTER,
    BINDING_ATOMIC_COUNTER2
};
enum {
    PROGRAM_VIEWER,
    PROGRAM_SUBD_CS_LOD,    // compute-based pipeline only
    PROGRAM_TERRAIN,
    PROGRAM_UPDATE_INDIRECT,    //Update indirect structures
    PROGRAM_UPDATE_INDIRECT_DRAW,
    PROGRAM_COUNT
};
enum {
    UNIFORM_VIEWER_FRAMEBUFFER_SAMPLER,
    UNIFORM_VIEWER_EXPOSURE,
    UNIFORM_VIEWER_GAMMA,
    UNIFORM_VIEWER_VIEWPORT,

    UNIFORM_SUBD_CS_LOD_DMAP_SAMPLER,   // compute-based pipeline only
    UNIFORM_SUBD_CS_LOD_DMAP_FACTOR,    // compute-based pipeline only
    UNIFORM_SUBD_CS_LOD_LOD_FACTOR,     // compute-based pipeline only

    UNIFORM_TERRAIN_DMAP_SAMPLER,
    UNIFORM_TERRAIN_SMAP_SAMPLER,
    UNIFORM_TERRAIN_DMAP_FACTOR,
    UNIFORM_TERRAIN_LOD_FACTOR,

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
} g_gl = { {0} };


int instancedMeshVertexCount = 0;
int instancedMeshPrimitiveCount = 0;


//Early declarations of ad-hoc instanced geom vertex/index buffers
extern const dja::vec2 verticesL0[3];
extern const uint16_t indexesL0[3];

extern const dja::vec2 verticesL1[6];
extern const uint16_t indexesL1[12];

extern const dja::vec2 verticesL2[15];
extern const uint16_t indexesL2[48];

extern const dja::vec2 verticesL3[45];
extern const uint16_t indexesL3[192];

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

static void APIENTRY
debug_output_logger(
    GLenum source,
    GLenum type,
    GLuint id,
    GLenum severity,
    GLsizei length,
    const GLchar* message,
    const void* userParam
) {
    char srcstr[32], typestr[32];

    switch (source) {
    case GL_DEBUG_SOURCE_API: strcpy(srcstr, "OpenGL"); break;
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM: strcpy(srcstr, "Windows"); break;
    case GL_DEBUG_SOURCE_SHADER_COMPILER: strcpy(srcstr, "Shader Compiler"); break;
    case GL_DEBUG_SOURCE_THIRD_PARTY: strcpy(srcstr, "Third Party"); break;
    case GL_DEBUG_SOURCE_APPLICATION: strcpy(srcstr, "Application"); break;
    case GL_DEBUG_SOURCE_OTHER: strcpy(srcstr, "Other"); break;
    default: strcpy(srcstr, "???"); break;
    };

    switch (type) {
    case GL_DEBUG_TYPE_ERROR: strcpy(typestr, "Error"); break;
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: strcpy(typestr, "Deprecated Behavior"); break;
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: strcpy(typestr, "Undefined Behavior"); break;
    case GL_DEBUG_TYPE_PORTABILITY: strcpy(typestr, "Portability"); break;
    case GL_DEBUG_TYPE_PERFORMANCE: strcpy(typestr, "Performance"); break;
    case GL_DEBUG_TYPE_OTHER: strcpy(typestr, "Message"); break;
    default: strcpy(typestr, "???"); break;
    }

    if (severity == GL_DEBUG_SEVERITY_HIGH || severity == GL_DEBUG_SEVERITY_MEDIUM) {
        LOG("djg_debug_output: %s %s\n"                \
            "-- Begin -- GL_debug_output\n" \
            "%s\n"                              \
            "-- End -- GL_debug_output\n",
            srcstr, typestr, message);
    }
    else if (severity == GL_DEBUG_SEVERITY_MEDIUM) {
        LOG("djg_debug_output: %s %s\n"                 \
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
void configureTerrainProgram()
{
    float lodFactor = 2.0f * tan(radians(g_camera.fovy) / 2.0f)
        / g_framebuffer.w * (1 << g_terrain.gpuSubd)
        * g_terrain.primitivePixelLengthTarget;

    glProgramUniform1i(g_gl.programs[PROGRAM_TERRAIN],
        g_gl.uniforms[UNIFORM_TERRAIN_DMAP_SAMPLER],
        TEXTURE_DMAP);
    glProgramUniform1i(g_gl.programs[PROGRAM_TERRAIN],
        g_gl.uniforms[UNIFORM_TERRAIN_SMAP_SAMPLER],
        TEXTURE_SMAP);
    glProgramUniform1f(g_gl.programs[PROGRAM_TERRAIN],
        g_gl.uniforms[UNIFORM_TERRAIN_DMAP_FACTOR],
        g_terrain.dmap.scale);
    glProgramUniform1f(g_gl.programs[PROGRAM_TERRAIN],
        g_gl.uniforms[UNIFORM_TERRAIN_LOD_FACTOR],
        lodFactor);
}

void configureSubdCsLodProgram()
{
    float lodFactor = 2.0f * tan(radians(g_camera.fovy) / 2.0f)
        / g_framebuffer.w * (1 << g_terrain.gpuSubd)
        * g_terrain.primitivePixelLengthTarget;

    glProgramUniform1i(g_gl.programs[PROGRAM_SUBD_CS_LOD],
        g_gl.uniforms[UNIFORM_SUBD_CS_LOD_DMAP_SAMPLER],
        TEXTURE_DMAP);
    glProgramUniform1f(g_gl.programs[PROGRAM_SUBD_CS_LOD],
        g_gl.uniforms[UNIFORM_SUBD_CS_LOD_DMAP_FACTOR],
        g_terrain.dmap.scale);
    glProgramUniform1f(g_gl.programs[PROGRAM_SUBD_CS_LOD],
        g_gl.uniforms[UNIFORM_SUBD_CS_LOD_LOD_FACTOR],
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
 * Set all shared defines in shaders
 */
void setShaderMacros(djg_program *djp)
{
    if (g_terrain.flags.displace)
        djgp_push_string(djp, "#define FLAG_DISPLACE 1\n");
    if (g_terrain.flags.cull)
        djgp_push_string(djp, "#define FLAG_CULL 1\n");
    if (g_terrain.flags.freeze)
        djgp_push_string(djp, "#define FLAG_FREEZE 1\n");

    switch (g_terrain.shading) {
    case SHADING_DIFFUSE:
        djgp_push_string(djp, "#define SHADING_DIFFUSE 1\n");
        break;
    case SHADING_NORMALS:
        djgp_push_string(djp, "#define SHADING_NORMALS 1\n");
        break;
    case SHADING_LOD:
        djgp_push_string(djp, "#define SHADING_LOD 1\n");
        break;
    }

    // constants
    if (g_terrain.method == METHOD_GS) {
        int subdLevel = g_terrain.gpuSubd;
        int vertexCnt = subdLevel == 0 ? 3 : 4 << (2 * subdLevel - 1);

        djgp_push_string(djp, "#define MAX_VERTICES %i\n", vertexCnt);
    }
    djgp_push_string(djp, "#define PATCH_TESS_LEVEL %i\n",1 << g_terrain.gpuSubd);
    djgp_push_string(djp, "#define PATCH_SUBD_LEVEL %i\n", g_terrain.gpuSubd);
    djgp_push_string(djp, "#define INSTANCED_MESH_VERTEX_COUNT %i\n", instancedMeshVertexCount);
    djgp_push_string(djp, "#define INSTANCED_MESH_PRIMITIVE_COUNT %i\n", instancedMeshPrimitiveCount);
    djgp_push_string(djp, "#define COMPUTE_THREAD_COUNT %i\n", 1u << g_terrain.computeThreadCount); //Compute Shader + Mesh Shader + Batch Program

    // bindings
    djgp_push_string(djp, "#define BUFFER_BINDING_TRANSFORMS %i\n", STREAM_TRANSFORM);
    djgp_push_string(djp, "#define BUFFER_BINDING_GEOMETRY_VERTICES %i\n", BUFFER_GEOMETRY_VERTICES);
    djgp_push_string(djp, "#define BUFFER_BINDING_GEOMETRY_INDEXES %i\n", BUFFER_GEOMETRY_INDEXES);
    djgp_push_string(djp, "#define BUFFER_BINDING_INSTANCED_GEOMETRY_VERTICES %i\n", BUFFER_INSTANCED_GEOMETRY_VERTICES);
    djgp_push_string(djp, "#define BUFFER_BINDING_INSTANCED_GEOMETRY_INDEXES %i\n", BUFFER_INSTANCED_GEOMETRY_INDEXES);
    djgp_push_string(djp, "#define BUFFER_BINDING_SUBD1 %i\n", BUFFER_SUBD1);
    djgp_push_string(djp, "#define BUFFER_BINDING_SUBD2 %i\n", BUFFER_SUBD2);
    djgp_push_string(djp, "#define BUFFER_BINDING_CULLED_SUBD %i\n", BUFFER_CULLED_SUBD1);
    djgp_push_string(djp, "#define BUFFER_BINDING_SUBD_COUNTER %i\n", BINDING_ATOMIC_COUNTER);
    djgp_push_string(djp, "#define BUFFER_BINDING_CULLED_SUBD_COUNTER %i\n", BINDING_ATOMIC_COUNTER2);
    djgp_push_string(djp, "#define BUFFER_BINDING_INDIRECT_COMMAND %i\n", BUFFER_DISPATCH_INDIRECT);
}

// -----------------------------------------------------------------------------
/**
 * Load the Terrain Program
 *
 * This program renders an adaptive terrain using the implicit subdivision
 * technique discribed in GPU Zen 2.
 */
bool loadTerrainProgram()
{
    djg_program *djp = djgp_create();
    GLuint *program = &g_gl.programs[PROGRAM_TERRAIN];
    char buf[1024];

    LOG("Loading {Terrain-Program}\n");
    if (g_terrain.method == METHOD_MS) {
        djgp_push_string(djp, "#ifndef FRAGMENT_SHADER\n#extension GL_NV_mesh_shader : require\n#endif\n");
        djgp_push_string(djp, "#extension GL_NV_shader_thread_group : require\n");
        djgp_push_string(djp, "#extension GL_NV_shader_thread_shuffle : require\n");
        djgp_push_string(djp, "#extension GL_NV_gpu_shader5 : require\n");
    }

    setShaderMacros(djp);

    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "fcull.glsl"));
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "isubd.glsl"));
    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "terrain_common.glsl"));

    switch (g_terrain.method) {
    case METHOD_TS:
        djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "terrain_ts.glsl"));
        break;
    case METHOD_GS:
        djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "terrain_gs.glsl"));
        break;
    case METHOD_CS:
        djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "terrain_cs_render.glsl"));
        break;
    case METHOD_MS:
        djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "terrain_ms.glsl"));
        break;
    }

    if (!djgp_to_gl(djp, 450, false, true, program)) {
        LOG("=> Failure <=\n");
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    g_gl.uniforms[UNIFORM_TERRAIN_DMAP_FACTOR] =
        glGetUniformLocation(g_gl.programs[PROGRAM_TERRAIN], "u_DmapFactor");
    g_gl.uniforms[UNIFORM_TERRAIN_DMAP_SAMPLER] =
        glGetUniformLocation(g_gl.programs[PROGRAM_TERRAIN], "u_DmapSampler");
    g_gl.uniforms[UNIFORM_TERRAIN_SMAP_SAMPLER] =
        glGetUniformLocation(g_gl.programs[PROGRAM_TERRAIN], "u_SmapSampler");
    g_gl.uniforms[UNIFORM_TERRAIN_LOD_FACTOR] =
        glGetUniformLocation(g_gl.programs[PROGRAM_TERRAIN], "u_LodFactor");

    configureTerrainProgram();

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load Computer-Shader LoD Program
 *
 * This program is responsible for updating the subdivision buffer.
 * It also prepares a buffer (the culled subd buffer) that only
 * contains visible triangles, which is sent for rendering.
 * For more details, see our GPU Zen 2 chapter.
 */
bool loadSubdCsLodProgram()
{
    if (g_terrain.method == METHOD_CS) {
        djg_program *djp = djgp_create();
        GLuint *program = &g_gl.programs[PROGRAM_SUBD_CS_LOD];
        char buf[1024];

        LOG("Loading {Compute-LoD-Program}\n");
        setShaderMacros(djp);
        djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "fcull.glsl"));
        djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "isubd.glsl"));
        djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "terrain_common.glsl"));

        djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "terrain_cs_lod.glsl"));

        if (!djgp_to_gl(djp, 450, false, true, program)) {
            LOG("=> Failure <=\n");
            djgp_release(djp);

            return false;
        }
        djgp_release(djp);

        g_gl.uniforms[UNIFORM_SUBD_CS_LOD_DMAP_FACTOR] =
            glGetUniformLocation(g_gl.programs[PROGRAM_SUBD_CS_LOD], "u_DmapFactor");
        g_gl.uniforms[UNIFORM_SUBD_CS_LOD_DMAP_SAMPLER] =
            glGetUniformLocation(g_gl.programs[PROGRAM_SUBD_CS_LOD], "u_DmapSampler");
        g_gl.uniforms[UNIFORM_SUBD_CS_LOD_LOD_FACTOR] =
            glGetUniformLocation(g_gl.programs[PROGRAM_SUBD_CS_LOD], "u_LodFactor");

        configureSubdCsLodProgram();
    }

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Indirect Program
 *
 * This program is responsible for preparing indirect batches. Every time
 * the subd buffer is updated, we execute this program to reset the atomic
 * counters that keep track of the size of the subdivision buffer, and
 * prepare the arguments of an indirect command.
 */
bool
loadUpdateIndirectProgram(
    int programName,
    bool updateIndirectStruct,
    bool resetCounter1,
    bool resetCounter2,
    int updateOffset,
    int divideValue,
    int addValue
) {
    djg_program *djp = djgp_create();
    GLuint *program = &g_gl.programs[programName];
    char buf[1024];

    LOG("Loading {Update-Indirect-Program}\n");
    if (GLAD_GL_ARB_shader_atomic_counter_ops) {
        djgp_push_string(djp, "#extension GL_ARB_shader_atomic_counter_ops : require\n");
        djgp_push_string(djp, "#define ATOMIC_COUNTER_EXCHANGE_ARB 1\n");
    } else if (GLAD_GL_AMD_shader_atomic_counter_ops) {
        djgp_push_string(djp, "#extension GL_AMD_shader_atomic_counter_ops : require\n");
        djgp_push_string(djp, "#define ATOMIC_COUNTER_EXCHANGE_AMD 1\n");
    }

    djgp_push_string(djp, "#define UPDATE_INDIRECT_STRUCT %i\n", updateIndirectStruct ? 1 : 0);
    djgp_push_string(djp, "#define UPDATE_INDIRECT_RESET_COUNTER1 %i\n", resetCounter1 ? 1 : 0);
    djgp_push_string(djp, "#define UPDATE_INDIRECT_RESET_COUNTER2 %i\n", resetCounter2 ? 1 : 0);

    djgp_push_string(djp, "#define BUFFER_BINDING_INDIRECT_COMMAND %i\n", BUFFER_DISPATCH_INDIRECT);
    djgp_push_string(djp, "#define BINDING_ATOMIC_COUNTER %i\n", BINDING_ATOMIC_COUNTER);
    djgp_push_string(djp, "#define BINDING_ATOMIC_COUNTER2 %i\n", BINDING_ATOMIC_COUNTER2);

    djgp_push_string(djp, "#define UPDATE_INDIRECT_OFFSET %i\n", updateOffset);
    djgp_push_string(djp, "#define UPDATE_INDIRECT_VALUE_DIVIDE %i\n", divideValue);
    djgp_push_string(djp, "#define UPDATE_INDIRECT_VALUE_ADD %i\n", addValue);

    djgp_push_file(djp, strcat2(buf, g_app.dir.shader, "terrain_updateIndirect_cs.glsl"));

    if (!djgp_to_gl(djp, 450, false, true, program)) {
        LOG("=> Failure <=\n");
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    return (glGetError() == GL_NO_ERROR);
}


bool loadUpdateIndirectPrograms()
{

    switch (g_terrain.method) {
    case METHOD_TS:
    case METHOD_GS:
        return loadUpdateIndirectProgram(PROGRAM_UPDATE_INDIRECT_DRAW, true, true, false, 0, 1, 0);
    case METHOD_CS:
        return loadUpdateIndirectProgram(PROGRAM_UPDATE_INDIRECT, true, true, true, 0, 1 << g_terrain.computeThreadCount, 1)
        && loadUpdateIndirectProgram(PROGRAM_UPDATE_INDIRECT_DRAW, true, true, false, 1, 1, 0);
    case METHOD_MS:
        return loadUpdateIndirectProgram(PROGRAM_UPDATE_INDIRECT, true, true, false, 0, 1 << g_terrain.computeThreadCount, 1);
    }

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

    if (v) v &= loadViewerProgram();
    if (v) v &= loadTerrainProgram();
    if (v) v &= loadSubdCsLodProgram();
    if (v) v &= loadUpdateIndirectPrograms();

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
        int maxSamplesDepth;
        //glGetIntegerv(GL_MAX_INTEGER_SAMPLES, &maxSamples); //Wrong enum !
        glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &maxSamples);
        glGetIntegerv(GL_MAX_DEPTH_TEXTURE_SAMPLES, &maxSamplesDepth);
        maxSamples = maxSamplesDepth < maxSamples ? maxSamplesDepth : maxSamples;

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
 * Load the Slope Texture Map
 *
 * This loads an RG32F texture used as a slope map
 */
void loadSmapTexture(const djg_texture *dmap)
{
    int w = dmap->next->x;
    int h = dmap->next->y;
    const uint16_t *texels = (const uint16_t *)dmap->next->texels;
    int mipcnt = djgt__mipcnt(w, h, 1);
    std::vector<float> smap(w * h * 2);

    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            int i1 = std::max(0, i - 1);
            int i2 = std::min(w - 1, i + 1);
            int j1 = std::max(0, j - 1);
            int j2 = std::min(h - 1, j + 1);
            uint16_t px_l = texels[i1 + w * j]; // in [0,2^16-1]
            uint16_t px_r = texels[i2 + w * j]; // in [0,2^16-1]
            uint16_t px_b = texels[i + w * j1]; // in [0,2^16-1]
            uint16_t px_t = texels[i + w * j2]; // in [0,2^16-1]
            float z_l = (float)px_l / 65535.0f; // in [0, 1]
            float z_r = (float)px_r / 65535.0f; // in [0, 1]
            float z_b = (float)px_b / 65535.0f; // in [0, 1]
            float z_t = (float)px_t / 65535.0f; // in [0, 1]
            float slope_x = (float)w * 0.5f * (z_r - z_l);
            float slope_y = (float)h * 0.5f * (z_t - z_b);

            smap[2 * (i + w * j)] = slope_x;
            smap[1 + 2 * (i + w * j)] = slope_y;
        }

    if (glIsTexture(g_gl.textures[TEXTURE_SMAP]))
        glDeleteTextures(1, &g_gl.textures[TEXTURE_SMAP]);

    glGenTextures(1, &g_gl.textures[TEXTURE_SMAP]);
    glActiveTexture(GL_TEXTURE0 + TEXTURE_SMAP);
    glBindTexture(GL_TEXTURE_2D, g_gl.textures[TEXTURE_SMAP]);
    glTexStorage2D(GL_TEXTURE_2D, mipcnt, GL_RG32F, w, h);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RG, GL_FLOAT, &smap[0]);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D,
        GL_TEXTURE_MIN_FILTER,
        GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_S,
        GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_T,
        GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE0);
}

/**
 * Load the Displacement Texture
 *
 * This loads an R16 texture used as a displacement map
 */
bool loadDmapTexture()
{
    if (!g_terrain.dmap.pathToFile.empty()) {
        djg_texture *djgt = djgt_create(1);
        GLuint *glt = &g_gl.textures[TEXTURE_DMAP];

        LOG("Loading {Dmap-Texture}\n");
        djgt_push_image_u16(djgt, g_terrain.dmap.pathToFile.c_str(), 1);

        // load smap from dmap
        loadSmapTexture(djgt);

        glActiveTexture(GL_TEXTURE0 + TEXTURE_DMAP);
        if (!djgt_to_gl(djgt, GL_TEXTURE_2D, GL_R16, 1, 1, glt)) {
            LOG("=> Failure <=\n");
            djgt_release(djgt);

            return false;
        }
        glTexParameteri(GL_TEXTURE_2D,
            GL_TEXTURE_MIN_FILTER,
            GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_S,
            GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_T,
            GL_CLAMP_TO_EDGE);
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

    if (v) v &= loadSceneFramebufferTexture();
    if (v) v &= loadBackFramebufferTexture();
    if (v) v &= loadDmapTexture();

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
    transform.modelView = view;
    transform.modelViewProjection = transform.projection * transform.modelView;
    transform.viewInv = viewInv;

	// transpose manually for AMD
	transform.projection = dja::transpose(transform.projection);
	transform.modelView = dja::transpose(transform.modelView);
	transform.modelViewProjection = dja::transpose(transform.modelViewProjection);
	transform.viewInv = dja::transpose(transform.viewInv);

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
bool loadGeometryBuffers()
{
    LOG("Loading {Mesh-Vertex-Buffer}\n");
    const dja::vec4 vertices[] = {
        {-1.0f, -1.0f, 0.0f, 1.0f},
        {+1.0f, -1.0f, 0.0f, 1.0f},
        {+1.0f, +1.0f, 0.0f, 1.0f},
        {-1.0f, +1.0f, 0.0f, 1.0f}
    };
    if (glIsBuffer(g_gl.buffers[BUFFER_GEOMETRY_VERTICES]))
        glDeleteBuffers(1, &g_gl.buffers[BUFFER_GEOMETRY_VERTICES]);
    glGenBuffers(1, &g_gl.buffers[BUFFER_GEOMETRY_VERTICES]);
    glBindBuffer(GL_ARRAY_BUFFER, g_gl.buffers[BUFFER_GEOMETRY_VERTICES]);
    glBufferData(GL_ARRAY_BUFFER,
        sizeof(vertices),
        (const void*)vertices,
        GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
        BUFFER_GEOMETRY_VERTICES,
        g_gl.buffers[BUFFER_GEOMETRY_VERTICES]);

    LOG("Loading {Mesh-Index-Buffer}\n");
    const uint32_t indexes[] = {
        0, 1, 3,
        2, 3, 1
    };
    if (glIsBuffer(g_gl.buffers[BUFFER_GEOMETRY_INDEXES]))
        glDeleteBuffers(1, &g_gl.buffers[BUFFER_GEOMETRY_INDEXES]);
    glGenBuffers(1, &g_gl.buffers[BUFFER_GEOMETRY_INDEXES]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gl.buffers[BUFFER_GEOMETRY_INDEXES]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        sizeof(indexes),
        (const void *)indexes,
        GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
        BUFFER_GEOMETRY_INDEXES,
        g_gl.buffers[BUFFER_GEOMETRY_INDEXES]);

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Instanced Geometry Buffer
 *
 * This procedure loads the geometry of a subdivided triangle into an
 * index and vertex buffer. Note that this buffer is only relevant
 * for the compute-shader based pipline.
 */

dja::mat3 bitToXform(uint32_t bit)
{
    float s = float(bit) - 0.5f;
    dja::vec3 c1 = dja::vec3(s, -0.5, 0);
    dja::vec3 c2 = dja::vec3(-0.5, -s, 0);
    dja::vec3 c3 = dja::vec3(+0.5, +0.5, 1);

    return dja::transpose(dja::mat3(c1, c2, c3));
}

dja::mat3 keyToXform(uint32_t key)
{
    dja::mat3 xf = dja::mat3(1.f);

    while (key > 1u) {
        xf = bitToXform(key & 1u) * xf;
        key = key >> 1u;
    }

    return xf;
}

bool loadInstancedGeometryBuffers()
{
    bool buffAllocated = false;
    dja::vec2 *vertices;
    uint16_t *indexes;


    if (g_terrain.gpuSubd == 0) {

        instancedMeshVertexCount = 3;
        instancedMeshPrimitiveCount = 1;

        vertices = (dja::vec2 *)verticesL0;
        indexes = (uint16_t *)indexesL0;
    }
#if USE_ADHOC_INSTANCED_GEOM
    else if (g_terrain.gpuSubd == 1) {
        instancedMeshVertexCount = 6;
        instancedMeshPrimitiveCount = 4;

        vertices = (dja::vec2 *)verticesL1;
        indexes = (uint16_t *)indexesL1;
    }
    else if (g_terrain.gpuSubd == 2) {
        instancedMeshVertexCount = 15;
        instancedMeshPrimitiveCount = 16;

        vertices = (dja::vec2 *)verticesL2;
        indexes = (uint16_t *)indexesL2;
    }
    else if (g_terrain.gpuSubd == 3) {
        instancedMeshVertexCount = 45;
        instancedMeshPrimitiveCount = 64;

        vertices = (dja::vec2 *)verticesL3;
        indexes = (uint16_t *)indexesL3;
    }
#endif
    else {
        int subdLevel = 2 * g_terrain.gpuSubd - 1;
        int stripCnt = 1 << subdLevel;
        int triangleCnt = stripCnt * 2;

        instancedMeshVertexCount = stripCnt * 4;
        instancedMeshPrimitiveCount = triangleCnt;

        vertices = new dja::vec2[instancedMeshVertexCount];
        indexes = new uint16_t[instancedMeshPrimitiveCount * 3];
        buffAllocated = true;

        for (int i = 0; i < stripCnt; ++i) {
            uint32_t key = i + stripCnt;
            dja::mat3 xf = keyToXform(key);
            dja::vec3 u1 = xf * dja::vec3(0.0f, 1.0f, 1.0f);
            dja::vec3 u2 = xf * dja::vec3(0.0f, 0.0f, 1.0f);
            dja::vec3 u3 = xf * dja::vec3(0.5f, 0.5f, 1.0f);
            dja::vec3 u4 = xf * dja::vec3(1.0f, 0.0f, 1.0f);

            // make sure triangle array is counter-clockwise
            if (subdLevel & 1) std::swap(u2, u3);

            vertices[4 * i] = dja::vec2(u1.x, u1.y);
            vertices[1 + 4 * i] = dja::vec2(u2.x, u2.y);
            vertices[2 + 4 * i] = dja::vec2(u3.x, u3.y);
            vertices[3 + 4 * i] = dja::vec2(u4.x, u4.y);
        }

        for (int i = 0; i < triangleCnt; ++i) {
            int e = i & 1; // 0 if even, 1 if odd

            indexes[3 * i] = i * 2;
            indexes[1 + 3 * i] = i * 2 + 1 - 2 * e;
            indexes[2 + 3 * i] = i * 2 + 2 - e;
        }
    }

    const GLsizeiptr bufferAllocationGranularity = 2048;        //Avoids alignment issues

    LOG("Loading {Instanced-Vertex-Buffer}\n");
    if (!glIsBuffer(g_gl.buffers[BUFFER_INSTANCED_GEOMETRY_VERTICES])) {
        glGenBuffers(1, &g_gl.buffers[BUFFER_INSTANCED_GEOMETRY_VERTICES]);
    }
    GLsizeiptr allocVertexBufferSize = sizeof(dja::vec2) * instancedMeshVertexCount;
    allocVertexBufferSize = ((allocVertexBufferSize + bufferAllocationGranularity - 1) / bufferAllocationGranularity) * bufferAllocationGranularity;
    glBindBuffer(GL_ARRAY_BUFFER, g_gl.buffers[BUFFER_INSTANCED_GEOMETRY_VERTICES]);
    glBufferStorage(GL_ARRAY_BUFFER, allocVertexBufferSize, NULL, GL_DYNAMIC_STORAGE_BIT);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(dja::vec2) * instancedMeshVertexCount, (const void*)vertices);


    LOG("Loading {Instanced-Index-Buffer}\n");
    if (!glIsBuffer(g_gl.buffers[BUFFER_INSTANCED_GEOMETRY_INDEXES])) {
        glGenBuffers(1, &g_gl.buffers[BUFFER_INSTANCED_GEOMETRY_INDEXES]);
    }
    GLsizeiptr allocIndexBufferSize = sizeof(uint16_t) * instancedMeshPrimitiveCount * 3;
    allocIndexBufferSize = ((allocIndexBufferSize + bufferAllocationGranularity - 1) / bufferAllocationGranularity) * bufferAllocationGranularity;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gl.buffers[BUFFER_INSTANCED_GEOMETRY_INDEXES]);
    glBufferStorage(GL_ELEMENT_ARRAY_BUFFER, allocIndexBufferSize, NULL, GL_DYNAMIC_STORAGE_BIT);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, sizeof(uint16_t) * instancedMeshPrimitiveCount * 3, (const void*)indexes);


    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);


    if (buffAllocated) {
        delete[] vertices;
        delete[] indexes;
    }

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
    const uint32_t data[] = { 0, 1, 1, 1 };

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
    if (g_terrain.method == METHOD_CS) {
        LOG("Loading {Culled-Subd-Buffer}\n");
        loadSubdBuffer(BUFFER_CULLED_SUBD1, bufferCapacity);
        //loadSubdBuffer(BUFFER_CULLED_SUBD2, bufferCapacity);
    }

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

    if (v) v &= loadTransformBuffer();
    if (v) v &= loadGeometryBuffers();
    if (v) v &= loadInstancedGeometryBuffers();
    if (v) v &= loadSubdivisionBuffers();

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
 * Load the Instanced Vertex Array (compute shader pass only)
 *
 * This will be used to instantiate a triangle grid for each subdivision
 * key present in the subd buffer.
 */
bool loadInstancedGeometryVertexArray()
{
    LOG("Loading {Instanced-Grid-VertexArray}\n");
    if (glIsVertexArray(g_gl.vertexArrays[VERTEXARRAY_INSTANCED_GRID]))
        glDeleteVertexArrays(1, &g_gl.vertexArrays[VERTEXARRAY_INSTANCED_GRID]);

    glGenVertexArrays(1, &g_gl.vertexArrays[VERTEXARRAY_INSTANCED_GRID]);
    glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_INSTANCED_GRID]);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,
        g_gl.buffers[BUFFER_INSTANCED_GEOMETRY_VERTICES]);
    glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, BUFFER_OFFSET(0));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
        g_gl.buffers[BUFFER_INSTANCED_GEOMETRY_INDEXES]);

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

    if (v) v &= loadEmptyVertexArray();
    if (v) v &= loadInstancedGeometryVertexArray();

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
    }
    else {
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

    if (v) v &= loadBackFramebuffer();
    if (v) v &= loadSceneFramebuffer();

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

    if (v) v &= loadTextures();
    if (v) v &= loadBuffers();
    if (v) v &= loadFramebuffers();
    if (v) v &= loadVertexArrays();
    if (v) v &= loadPrograms();

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
 * Utility functions
 */
union IndirectCommand {
    struct {
        uint32_t num_groups_x,
            num_groups_y,
            num_groups_z,
            align[5];
    } dispatchIndirect;
    struct {
        uint32_t count,
            primCount,
            first,
            baseInstance,
            align[4];
    } drawArraysIndirect;
    struct {
        uint32_t count,
            primCount,
            firstIndex,
            baseVertex,
            baseInstance,
            align[3];
    } drawElementsIndirect;
    struct {
        uint32_t count,
            first,
            align[6];
    } drawMeshTasksIndirectCommandNV;
};

bool
createIndirectCommandBuffer(
    GLuint binding,
    int bufferid,
    IndirectCommand drawArrays
) {
    if (!glIsBuffer(g_gl.buffers[bufferid]))
        glGenBuffers(1, &g_gl.buffers[bufferid]);

    glBindBuffer(binding, g_gl.buffers[bufferid]);
    glBufferData(binding, sizeof(drawArrays), &drawArrays, GL_STATIC_DRAW);
    glBindBuffer(binding, 0);

    return (glGetError() == GL_NO_ERROR);
}

bool createAtomicCounters(GLint atomicData[8])
{
    if (!glIsBuffer(g_gl.buffers[BUFFER_ATOMIC_COUNTER]))
        glGenBuffers(1, &g_gl.buffers[BUFFER_ATOMIC_COUNTER]);

    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, g_gl.buffers[BUFFER_ATOMIC_COUNTER]);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER,
                 sizeof(GLint) * 8,
                 atomicData,
                 GL_STREAM_DRAW);

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Launch the Indirect Program
 *
 * This is a generic function to launch the indirect batcher program.
 */
void callUpdateIndirectProgram(
    int programName,
    GLuint counter1,
    GLintptr counterOffset1,
    GLuint counter2,
    GLintptr counterOffset2,
    GLuint indirectBuffer
) {
    glBindBufferRange(GL_ATOMIC_COUNTER_BUFFER,
                      BINDING_ATOMIC_COUNTER,
                      counter1,
                      counterOffset1,
                      sizeof(int));
    glBindBufferRange(GL_ATOMIC_COUNTER_BUFFER,
                      BINDING_ATOMIC_COUNTER2,
                      counter2,
                      counterOffset2,
                      sizeof(int));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_DISPATCH_INDIRECT,
                     indirectBuffer);
    glUseProgram(g_gl.programs[programName]);
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
}

// -----------------------------------------------------------------------------
/**
 * Terrain Rendering -- Tessellation Shader Pipeline
 *
 * The tessellation shader pipeline updates the subd buffer in the
 * tessellation control shader stage and generates a tessellated patch
 * with fixed tessellation factors for each key that leads to a visible
 * triangle. Note that this pipeline is essentially the same as the compute
 * pipeline, but done in one pass instead of two.
 */
void renderSceneTs() {
    static int offset = 0;
    int nextOffset = 0;

    // render terrain
    glPatchParameteri(GL_PATCH_VERTICES, 1);
    if (g_terrain.flags.reset) {
        IndirectCommand drawArrays = { 2u, 1u, 0u, 0u, 0u, 0u, 0u, 0u };
        GLint atomicData[] = { 0, 0, 0, 0, 0, 0, 0, 0 };

        loadSubdivisionBuffers();
        createIndirectCommandBuffer(GL_DRAW_INDIRECT_BUFFER,
                                    BUFFER_DRAW_INDIRECT,
                                    drawArrays);
        createAtomicCounters(atomicData);

        g_terrain.pingPong = 1;
        offset = 0;
        g_terrain.flags.reset = false;
    }

    // render the terrain
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER,
                     BINDING_ATOMIC_COUNTER,
                     g_gl.buffers[BUFFER_ATOMIC_COUNTER]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_SUBD1,
                     g_gl.buffers[BUFFER_SUBD1 + 1 - g_terrain.pingPong]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_SUBD2,
                     g_gl.buffers[BUFFER_SUBD1 + g_terrain.pingPong]);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, g_gl.buffers[BUFFER_DRAW_INDIRECT]);
    glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
    glUseProgram(g_gl.programs[PROGRAM_TERRAIN]);
    glDrawArraysIndirect(GL_PATCHES, 0);

    // prepare next batch
    callUpdateIndirectProgram(PROGRAM_UPDATE_INDIRECT_DRAW,
                              g_gl.buffers[BUFFER_ATOMIC_COUNTER],
                              0, 0, 0,
                              g_gl.buffers[BUFFER_DRAW_INDIRECT]);

    g_terrain.pingPong = 1 - g_terrain.pingPong;
    offset = nextOffset;
}

// -----------------------------------------------------------------------------
/**
 * Terrain Rendering -- Geometry Shader Pipeline
 *
 * The geometry shader pipeline updates the subd buffer in the
 * geometry shader stage and generates a tessellated patch
 * with fixed tessellation factors for each key that leads to a visible
 * triangle. The tessellation is done procedurally with triangle strips.
 * Note that this pipeline is essentially the same as the compute pipeline,
 * but done in one pass instead of two.
 */
void renderSceneGs() {
    static int offset = 0;
    int nextOffset = 0;

    // render terrain
    if (g_terrain.flags.reset) {
        IndirectCommand drawArrays = { 2u, 1u, 0u, 0u, 0u, 0u, 0u, 0u };
        GLint atomicData[] = { 0, 0, 0, 0, 0, 0, 0, 0 };

        loadSubdivisionBuffers();
        createIndirectCommandBuffer(GL_DRAW_INDIRECT_BUFFER,
                                    BUFFER_DRAW_INDIRECT,
                                    drawArrays);
        createAtomicCounters(atomicData);

        g_terrain.pingPong = 1;
        offset = 0;
        g_terrain.flags.reset = false;
    }

    // render terrain
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER,
                     BINDING_ATOMIC_COUNTER,
                     g_gl.buffers[BUFFER_ATOMIC_COUNTER]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_SUBD1,
                     g_gl.buffers[BUFFER_SUBD1 + 1 - g_terrain.pingPong]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_SUBD2,
                     g_gl.buffers[BUFFER_SUBD1 + g_terrain.pingPong]);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, g_gl.buffers[BUFFER_DRAW_INDIRECT]);
    glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
    glUseProgram(g_gl.programs[PROGRAM_TERRAIN]);
    glDrawArraysIndirect(GL_POINTS, 0);

    // update indirect draw call
    callUpdateIndirectProgram(PROGRAM_UPDATE_INDIRECT_DRAW,
                              g_gl.buffers[BUFFER_ATOMIC_COUNTER],
                              0, 0, 0,
                              g_gl.buffers[BUFFER_DRAW_INDIRECT]);

    g_terrain.pingPong = 1 - g_terrain.pingPong;
    offset = nextOffset;
}

// -----------------------------------------------------------------------------
/**
 * Terrain Rendering -- Mesh Shader Pipeline
 *
 * The mesh shader pipeline updates the subd buffer in the
 * task shader stage and generates a tessellated patch
 * with fixed tessellation factors for each key that leads to a visible
 * triangle in the mesh shader. The tessellation is done procedurally.
 * Note that this pipeline is essentially the same as the compute pipeline,
 * but done in one pass instead of two.
 */
void renderSceneMs() {
    static int offset = 0;
    int nextOffset = 0;

    // Init
    if (g_terrain.flags.reset) {
        GLint atomicData[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        IndirectCommand cmd = {
            2u / (1u << g_terrain.computeThreadCount) + 1u,
            0u, 0u, 0u, 0u, 0u, 0u, 2u        //Hack:last value is number of primitives
        };

        loadSubdivisionBuffers();
        createAtomicCounters(atomicData);
        createIndirectCommandBuffer(GL_DRAW_INDIRECT_BUFFER,
                                    BUFFER_DISPATCH_INDIRECT,
                                    cmd);

        g_terrain.pingPong = 1;
        offset = 0;
        g_terrain.flags.reset = false;
    }

    // Bind buffers to binding points
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_INSTANCED_GEOMETRY_VERTICES,
                     g_gl.buffers[BUFFER_INSTANCED_GEOMETRY_VERTICES]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_INSTANCED_GEOMETRY_INDEXES,
                     g_gl.buffers[BUFFER_INSTANCED_GEOMETRY_INDEXES]);
    glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_SUBD1,
                     g_gl.buffers[BUFFER_SUBD1 + 1 - g_terrain.pingPong]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_SUBD2,
                     g_gl.buffers[BUFFER_SUBD1 + g_terrain.pingPong]);
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER,
                     BINDING_ATOMIC_COUNTER,
                     g_gl.buffers[BUFFER_ATOMIC_COUNTER]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_DISPATCH_INDIRECT,
                     g_gl.buffers[BUFFER_DISPATCH_INDIRECT]);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER,
                 g_gl.buffers[BUFFER_DISPATCH_INDIRECT]);

    // draw terrain
    glUseProgram(g_gl.programs[PROGRAM_TERRAIN]);
    glDrawMeshTasksIndirectNV(0);

    // update batch
    callUpdateIndirectProgram(PROGRAM_UPDATE_INDIRECT,
                              g_gl.buffers[BUFFER_ATOMIC_COUNTER], 0,
                              g_gl.buffers[BUFFER_ATOMIC_COUNTER], 0,
                              g_gl.buffers[BUFFER_DISPATCH_INDIRECT]);

    g_terrain.pingPong = 1 - g_terrain.pingPong;
    offset = nextOffset;
}

// -----------------------------------------------------------------------------
/**
 * Terrain Rendering -- Compute Shader Pipeline
 *
 * This is the orginal implementation of the GPU Zen 2 chapter.
 * The compute shader pipeline updates the subd buffer a dedicated
 * compute shader stage. Then a tessellated patch with fixed tessellation
 * factors is instanced for each key that leads to a visible
 * triangle in a seperate rendering program.
 */
void renderSceneCs() {
    static int offset = 0;
    int nextOffset = 0;

    // update the subd buffers
    if (g_terrain.flags.reset) {
        GLint atomicData[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        IndirectCommand cmd = {
            2u / (1u << g_terrain.computeThreadCount) + 1u,
            1u, 1u, 0u, 0u, 0u, 0u, 2u
        };
        const int subdLevel = 2 * g_terrain.gpuSubd - 1;
        const uint32_t cnt = subdLevel > 0 ? 6u << subdLevel : 3u;
        IndirectCommand drawElements = { cnt, 0u, 0u, 0u, 0u, 0u, 0u, 0u };

        loadSubdivisionBuffers();
        createAtomicCounters(atomicData);
        createIndirectCommandBuffer(GL_DISPATCH_INDIRECT_BUFFER,
                                    BUFFER_DISPATCH_INDIRECT,
                                    cmd);
        createIndirectCommandBuffer(GL_DRAW_INDIRECT_BUFFER,
                                    BUFFER_DRAW_INDIRECT,
                                    drawElements);

        g_terrain.pingPong = 1;
        offset = 0;
        g_terrain.flags.reset = false;
    }


    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_SUBD1,
                     g_gl.buffers[BUFFER_SUBD1 + 1 - g_terrain.pingPong]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_SUBD2,
                     g_gl.buffers[BUFFER_SUBD1 + g_terrain.pingPong]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_CULLED_SUBD1,
                     g_gl.buffers[BUFFER_CULLED_SUBD1]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_DISPATCH_INDIRECT,
                     g_gl.buffers[BUFFER_DISPATCH_INDIRECT]);
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER,
                     BINDING_ATOMIC_COUNTER,
                     g_gl.buffers[BUFFER_ATOMIC_COUNTER]);
    glBindBufferRange(GL_ATOMIC_COUNTER_BUFFER,
                      BINDING_ATOMIC_COUNTER2,
                      g_gl.buffers[BUFFER_DRAW_INDIRECT],
                      sizeof(int), sizeof(int));

    // update the subd buffer
    glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,
                 g_gl.buffers[BUFFER_DISPATCH_INDIRECT]);
    glUseProgram(g_gl.programs[PROGRAM_SUBD_CS_LOD]);
    glDispatchComputeIndirect(0);

    // render the terrain
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
    glUseProgram(g_gl.programs[PROGRAM_TERRAIN]);
    glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_INSTANCED_GRID]);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER,
                 g_gl.buffers[BUFFER_DRAW_INDIRECT]);
    glDrawElementsIndirect(GL_TRIANGLES,
                           GL_UNSIGNED_SHORT,
                           NULL);

    // update batch
    callUpdateIndirectProgram(PROGRAM_UPDATE_INDIRECT,
                              g_gl.buffers[BUFFER_ATOMIC_COUNTER], 0,
                              g_gl.buffers[BUFFER_DRAW_INDIRECT], sizeof(int),
                              g_gl.buffers[BUFFER_DISPATCH_INDIRECT]);

    g_terrain.pingPong = 1 - g_terrain.pingPong;
    offset = nextOffset;
}

// -----------------------------------------------------------------------------
/**
 * Render Scene
 *
 * This procedure renders the scene to the back buffer.
 */
void renderScene()
{
    // configure GL state
    glBindFramebuffer(GL_FRAMEBUFFER, g_gl.framebuffers[FRAMEBUFFER_SCENE]);
    glViewport(0, 0, g_framebuffer.w, g_framebuffer.h);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    if (g_terrain.flags.wire)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    // clear framebuffer
    glClearColor(0.5, 0.5, 0.5, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    loadTransformBuffer();

    // render
    switch (g_terrain.method) {
    case METHOD_TS:
        renderSceneTs();
        break;
    case METHOD_GS:
        renderSceneGs();
        break;
    case METHOD_CS:
        renderSceneCs();
        break;
    case METHOD_MS:
        renderSceneMs();
        break;
    default:
        break;
    }

    // reset GL state
    if (g_terrain.flags.wire)
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_DEPTH_TEST);

    if (g_terrain.flags.freeze_step) {
        g_terrain.flags.freeze = true;
        loadPrograms();
        g_terrain.flags.freeze_step = false;
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
                configureTerrainProgram();
            }
            if (ImGui::SliderFloat("zNear", &g_camera.zNear, 0.0001f, 1.f)) {
                if (g_camera.zNear >= g_camera.zFar)
                    g_camera.zNear = g_camera.zFar - 0.01f;
            }
            if (ImGui::SliderFloat("zFar", &g_camera.zFar, 1.f, 32.f)) {
                if (g_camera.zFar <= g_camera.zNear)
                    g_camera.zFar = g_camera.zNear + 0.01f;
            }
        }
        ImGui::End();
        // Terrain Widgets
        ImGui::SetNextWindowPos(ImVec2(10, 140)/*, ImGuiSetCond_FirstUseEver*/);
        ImGui::SetNextWindowSize(ImVec2(510, 210)/*, ImGuiSetCond_FirstUseEver*/);
        ImGui::Begin("Terrain");
        {
            const char* eShadings[] = {
                "Diffuse",
                "Normals",
                "LoD"
            };
            std::vector<const char *> eMethods = {
                "Tessellation Shader",
                "Geometry Shader",
                "Compute Shader"
            };
            if (GLAD_GL_NV_mesh_shader)
                eMethods.push_back("Mesh Shader");
            ImGui::Text("CPU_dt: %.3f %s",
                cpuDt < 1. ? cpuDt * 1e3 : cpuDt,
                cpuDt < 1. ? "ms" : " s");
            ImGui::SameLine();
            ImGui::Text("GPU_dt: %.3f %s",
                gpuDt < 1. ? gpuDt * 1e3 : gpuDt,
                gpuDt < 1. ? "ms" : " s");
            if (ImGui::Combo("Shading", &g_terrain.shading, &eShadings[0], BUFFER_SIZE(eShadings))) {
                loadTerrainProgram();
                g_terrain.flags.reset = true;
            }
            if (ImGui::Combo("Method", &g_terrain.method, &eMethods[0], eMethods.size())) {
                if (g_terrain.method == METHOD_MS && g_terrain.computeThreadCount>5) {
                    g_terrain.computeThreadCount = 5;
                }

                loadPrograms();
                g_terrain.flags.reset = true;
            }
            ImGui::Text("flags: ");
            ImGui::SameLine();
            if (ImGui::Checkbox("cull", &g_terrain.flags.cull))
                loadPrograms();
            ImGui::SameLine();
            ImGui::Checkbox("wire", &g_terrain.flags.wire);
            ImGui::SameLine();
            if (ImGui::Checkbox("freeze", &g_terrain.flags.freeze)) {
                loadTerrainProgram();
                if (g_terrain.method == METHOD_CS)
                    configureSubdCsLodProgram();
            }
            if (!g_terrain.dmap.pathToFile.empty()) {
                ImGui::SameLine();
                if (ImGui::Checkbox("displace", &g_terrain.flags.displace))
                    loadTerrainProgram();
            }
            if (ImGui::SliderInt("PatchSubdLevel", &g_terrain.gpuSubd, 0, 3)) {
                loadInstancedGeometryBuffers();
                loadInstancedGeometryVertexArray();
                loadPrograms();
                g_terrain.flags.reset = true;

                LOG("Patch Vertex Count: %d\nPatch Primitive Count: %d\n", instancedMeshVertexCount, instancedMeshPrimitiveCount);

            }
            if (ImGui::SliderFloat("PixelsPerEdge", &g_terrain.primitivePixelLengthTarget, 1, 16)) {
                configureTerrainProgram();
                if (g_terrain.method == METHOD_CS)
                    configureSubdCsLodProgram();
            }
            if (ImGui::SliderFloat("DmapScale", &g_terrain.dmap.scale, 0.f, 1.f)) {
                configureTerrainProgram();
                if (g_terrain.method == METHOD_CS)
                    configureSubdCsLodProgram();
            }
            if (g_terrain.method == METHOD_CS || g_terrain.method == METHOD_MS) {
                char buf[64];

                int maxValue = 8;
                if (g_terrain.method == METHOD_MS) {
                    maxValue = 5;
                }

                sprintf(buf, "ComputeThreadCount (%02i)", 1 << g_terrain.computeThreadCount);
                if (ImGui::SliderInt(buf, &g_terrain.computeThreadCount, 0, maxValue)) {
                    loadPrograms();
                    g_terrain.flags.reset = true;
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
            g_terrain.flags.reset = true;
            break;
        case GLFW_KEY_S:
            loadPrograms();
            break;
        case GLFW_KEY_F:
            g_terrain.flags.freeze = !g_terrain.flags.freeze;
            loadPrograms();
            break;
        case GLFW_KEY_G:
            g_terrain.flags.freeze = false;
            loadPrograms();
            g_terrain.flags.freeze_step = true;
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
    }
    else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        dja::mat3 axis = dja::transpose(g_camera.axis);
        g_camera.pos -= axis[1] * dx * 5e-3 * norm(g_camera.pos);
        g_camera.pos += axis[2] * dy * 5e-3 * norm(g_camera.pos);
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
    g_camera.pos -= axis[0] * yoffset * 5e-2 * norm(g_camera.pos);
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


    //int maxUniformBlocks = 0;
    //glGetIntegerv(GL_MAX_GEOMETRY_UNIFORM_BLOCKS, &maxUniformBlocks);
    //LOG("[MAX BLOCKS] : %d\n", maxUniformBlocks);

    LOG("-- Begin -- Demo\n");
    try {
        log_debug_output();
        ImGui::CreateContext();
        ImGui_ImplGlfwGL3_Init(window, false);
        ImGui::StyleColorsDark();
        LOG("-- Begin -- Init\n");
        init();
        LOG("-- End -- Init\n");

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            render();

            glfwSwapBuffers(window);
        }

        release();
        ImGui_ImplGlfwGL3_Shutdown();
        ImGui::DestroyContext();
        glfwTerminate();
    }
    catch (std::exception& e) {
        LOG("%s", e.what());
        ImGui_ImplGlfwGL3_Shutdown();
        ImGui::DestroyContext();
        glfwTerminate();
        LOG("(!) Demo Killed (!)\n");

        return EXIT_FAILURE;
    }
    catch (...) {
        ImGui_ImplGlfwGL3_Shutdown();
        ImGui::DestroyContext();
        glfwTerminate();
        LOG("(!) Demo Killed (!)\n");

        return EXIT_FAILURE;
    }
    LOG("-- End -- Demo\n");

    return 0;
}

////Instanced patch geometry at various subdiv levels////

//gpuSubd == 0
const dja::vec2 verticesL0[] = {
    { 0.0f, 0.0f },
    { 1.0f, 0.0f },
    { 0.0f, 1.0f }
};
const uint16_t indexesL0[] = { 0u, 1u, 2u };

//gpuSubd == 1
const dja::vec2 verticesL1[] = {
    { 0.0f, 1.0f },
    { 0.5f, 0.5f },
    { 0.0f, 0.5f },
    { 0.0f, 0.0f },
    { 0.5f, 0.0f },
    { 1.0f, 0.0f }
};
const uint16_t indexesL1[] = {
    1u, 0u, 2u,
    1u, 2u, 3u,
    1u, 3u, 4u,
    1u, 4u, 5u
};

//gpuSubd == 2
const dja::vec2 verticesL2[] = {
    { 0.25f, 0.75f },
    { 0.0f, 1.0f },
    { 0.0f, 0.75f },
    { 0.0f, 0.5f },
    { 0.25f, 0.5f },
    { 0.5f, 0.5f },

    { 0.25f, 0.25f },
    { 0.0f, 0.25f },
    { 0.0f, 0.0f },
    { 0.25f, 0.0f },
    { 0.5f, 0.0f },
    { 0.5f, 0.25f },
    { 0.75f, 0.25f },
    { 0.75f, 0.0f },
    { 1.0f, 0.0f }        //14
};
const uint16_t indexesL2[] = {
    0u, 1u, 2u,
    0u, 2u, 3u,
    0u, 3u, 4u,
    0u, 4u, 5u,

    6u, 5u, 4u,
    6u, 4u, 3u,
    6u, 3u, 7u,
    6u, 7u, 8u,

    6u, 8u, 9u,
    6u, 9u, 10u,
    6u, 10u, 11u,
    6u, 11u, 5u,

    12u, 5u, 11u,
    12u, 11u, 10u,
    12u, 10u, 13u,
    12u, 13u, 14u
};

//gpuSubd == 3
const dja::vec2 verticesL3[] = {
    { 0.25f*0.5f, 0.75f*0.5f + 0.5f },
    { 0.0f*0.5f, 1.0f*0.5f + 0.5f },
    { 0.0f*0.5f, 0.75f*0.5f + 0.5f },
    { 0.0f*0.5f , 0.5f*0.5f + 0.5f },
    { 0.25f*0.5f, 0.5f*0.5f + 0.5f },
    { 0.5f*0.5f, 0.5f*0.5f + 0.5f },
    { 0.25f*0.5f, 0.25f*0.5f + 0.5f },
    { 0.0f*0.5f, 0.25f*0.5f + 0.5f },
    { 0.0f*0.5f, 0.0f*0.5f + 0.5f },
    { 0.25f*0.5f, 0.0f*0.5f + 0.5f },
    { 0.5f*0.5f, 0.0f*0.5f + 0.5f },
    { 0.5f*0.5f, 0.25f*0.5f + 0.5f },
    { 0.75f*0.5f, 0.25f*0.5f + 0.5f },
    { 0.75f*0.5f, 0.0f*0.5f + 0.5f },
    { 1.0f*0.5f, 0.0f*0.5f + 0.5f },        //14

    { 0.375f, 0.375f },
    { 0.25f, 0.375f },
    { 0.25f, 0.25f },
    { 0.375f, 0.25f },
    { 0.5f, 0.25f },
    { 0.5f, 0.375f },    //20

    { 0.125f, 0.375f },
    { 0.0f, 0.375f },
    { 0.0f, 0.25f },
    { 0.125f, 0.25f },    //24

    { 0.125f, 0.125f },
    { 0.0f, 0.125f },
    { 0.0f, 0.0f },
    { 0.125f, 0.0f },
    { 0.25f, 0.0f },
    { 0.25f, 0.125f },    //30

    { 0.375f, 0.125f },
    { 0.375f, 0.0f },
    { 0.5f, 0.0f },
    { 0.5f, 0.125f },    //34

    { 0.625f, 0.375f },
    { 0.625f, 0.25f },
    { 0.75f, 0.25f },    //37

    { 0.625f, 0.125f },
    { 0.625f, 0.0f },
    { 0.75f, 0.0f },
    { 0.75f, 0.125f },    //41

    { 0.875f, 0.125f },
    { 0.875f, 0.0f },
    { 1.0f, 0.0f }    //44
};
const uint16_t indexesL3[] = {
    0u, 1u, 2u,
    0u, 2u, 3u,
    0u, 3u, 4u,
    0u, 4u, 5u,

    6u, 5u, 4u,
    6u, 4u, 3u,
    6u, 3u, 7u,
    6u, 7u, 8u,

    6u, 8u, 9u,
    6u, 9u, 10u,
    6u, 10u, 11u,
    6u, 11u, 5u,

    12u, 5u, 11u,
    12u, 11u, 10u,
    12u, 10u, 13u,
    12u, 13u, 14u,        //End fo first big triangle

    15u, 14u, 13u,
    15u, 13u, 10u,
    15u, 10u, 16u,
    15u, 16u, 17u,
    15u, 17u, 18u,
    15u, 18u, 19u,
    15u, 19u, 20u,
    15u, 20u, 14u,

    21u, 10u, 9u,
    21u, 9u, 8u,
    21u, 8u, 22u,
    21u, 22u, 23u,
    21u, 23u, 24u,
    21u, 24u, 17u,
    21u, 17u, 16u,
    21u, 16u, 10u,

    25u, 17u, 24u,
    25u, 24u, 23u,
    25u, 23u, 26u,
    25u, 26u, 27u,
    25u, 27u, 28u,
    25u, 28u, 29u,
    25u, 29u, 30u,
    25u, 30u, 17u,

    31u, 19u, 18u,
    31u, 18u, 17u,
    31u, 17u, 30u,
    31u, 30u, 29u,
    31u, 29u, 32u,
    31u, 32u, 33u,
    31u, 33u, 34u,
    31u, 34u, 19u,

    35u, 14u, 20u,
    35u, 20u, 19u,
    35u, 19u, 36u,
    35u, 36u, 37u,

    38u, 37u, 36u,
    38u, 36u, 19u,
    38u, 19u, 34u,
    38u, 34u, 33u,
    38u, 33u, 39u,
    38u, 39u, 40u,
    38u, 40u, 41u,
    38u, 41u, 37u,

    42u, 37u, 41u,
    42u, 41u, 40u,
    42u, 40u, 43u,
    42u, 43u, 44u
};

