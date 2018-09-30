#line 1
////////////////////////////////////////////////////////////////////////////////
// Implicit Subdivition Sahder for Terrain Rendering
//

layout (std430, binding = BUFFER_BINDING_SUBD1)
readonly buffer SubdBufferIn {
    uvec2 u_SubdBufferIn[];
};

layout (std430, binding = BUFFER_BINDING_SUBD2)
buffer SubdBufferOut {
    uvec2 u_SubdBufferOut[];
};

layout (std430, binding = BUFFER_BINDING_GEOMETRY_VERTICES)
readonly buffer VertexBuffer {
    vec4 u_VertexBuffer[];
};

layout (std430, binding = BUFFER_BINDING_GEOMETRY_EDGES)
readonly buffer EdgeBuffer {
    ivec4 u_EdgeBuffer[];
};

layout (binding = BUFFER_BINDING_SUBD_COUNTER)
uniform atomic_uint u_SubdBufferCounter;

struct Transform {
    mat4 modelView;
    mat4 projection;
    mat4 modelViewProjection;
    mat4 viewInv;
};

layout(std140, row_major, binding = BUFFER_BINDING_TRANSFORMS)
uniform Transforms {
    Transform u_Transform;
};

uniform sampler2D u_DmapSampler;
uniform float u_DmapFactor;
uniform float u_LodFactor;

float distanceToLod(float z, float lodFactor)
{
    return -log2(clamp(z * lodFactor, 0.0f, 1.0f));
}

float computeLod(vec3 c)
{
    vec3 cxf = (u_Transform.modelView * vec4(c, 1)).xyz;
    float z = length(cxf);

    return distanceToLod(z, u_LodFactor);
}

float computeLod(in vec4 v[4])
{
    vec3 c = (v[0].xyz + v[1].xyz + v[2].xyz + v[3].xyz) / 4.0;
    return computeLod(c);
}

// -----------------------------------------------------------------------------
/**
 * Vertex Shader
 *
 * The vertex shader is empty
 */
#ifdef VERTEX_SHADER
void main()
{ }
#endif

// -----------------------------------------------------------------------------
/**
 * Tessellation Control Shader
 *
 * This tessellaction control shader is responsible for updating the
 * subdivision buffer and sending visible geometry to the rasterizer.
 */
#ifdef TESS_CONTROL_SHADER
layout (vertices = 1) out;
layout (location = 0) out ccpatch o_Patch[];

void writeKey(uint primID, uint key)
{
    uint idx = atomicCounterIncrement(u_SubdBufferCounter);

    u_SubdBufferOut[idx] = uvec2(primID, key);
}

void updateSubdBuffer(uint primID, uint key, int targetLod, int parentLod)
{
    // extract subdivision level associated to the key
    int keyLod = findMSB(key) / 2;

    // update the key accordingly
    if (/* subdivide ? */ keyLod < targetLod && !isLeafKey(key)) {
        uint children[4]; childrenKeys(key, children);

        writeKey(primID, children[0]);
        writeKey(primID, children[1]);
        writeKey(primID, children[2]);
        writeKey(primID, children[3]);
    } else if (/* keep ? */ keyLod < (parentLod + 1)) {
        writeKey(primID, key);
    } else /* merge ? */ {
        if (/* is root ? */isRootKey(key)) {
            writeKey(primID, key);
        } else if (/* is zero child ? */isChildZeroKey(key)) {
            writeKey(primID, parentKey(key));
        }
    }
}

void main()
{
    // get threadID (each key is associated to a thread)
    int threadID = gl_PrimitiveID;

    // get coarse patch associated to the key
    uint primID = u_SubdBufferIn[threadID].x;
    /* precomputations */
    int i0 = 4 * int(primID);
    int i1 = 4 * int(primID) + 1;
    int i2 = 4 * int(primID) + 2;
    int i3 = 4 * int(primID) + 3;
    /* get main quad control points */
    int v05 = u_EdgeBuffer[i0].x;
    int v06 = u_EdgeBuffer[i1].x;
    int v10 = u_EdgeBuffer[i2].x;
    int v09 = u_EdgeBuffer[i3].x;
    /* get 1-neighbour points via half-edge */
    int v02 = u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[i0].w].z].x;
    int v11 = u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[i1].w].z].x;
    int v13 = u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[i2].w].z].x;
    int v04 = u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[i3].w].z].x;
    /* get 2-neighbour points via half-edge */
    int v01 = u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[i0].w].z].z].x;
    int v07 = u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[i1].w].z].z].x;
    int v14 = u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[i2].w].z].z].x;
    int v08 = u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[i3].w].z].z].x;
    /* get 3-neighbour points via half-edge */
    int v00 = u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[i0].w].y].w].z].x;
    int v03 = u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[i1].w].y].w].z].x;
    int v15 = u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[i2].w].y].w].z].x;
    int v12 = u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[u_EdgeBuffer[i3].w].y].w].z].x;

    vec4 v_in[16] = vec4[16](
        u_VertexBuffer[v00].zxyw,
        u_VertexBuffer[v01].zxyw,
        u_VertexBuffer[v02].zxyw,
        u_VertexBuffer[v03].zxyw,
        u_VertexBuffer[v04].zxyw,
        u_VertexBuffer[v05].zxyw,
        u_VertexBuffer[v06].zxyw,
        u_VertexBuffer[v07].zxyw,
        u_VertexBuffer[v08].zxyw,
        u_VertexBuffer[v09].zxyw,
        u_VertexBuffer[v10].zxyw,
        u_VertexBuffer[v11].zxyw,
        u_VertexBuffer[v12].zxyw,
        u_VertexBuffer[v13].zxyw,
        u_VertexBuffer[v14].zxyw,
        u_VertexBuffer[v15].zxyw
    );

    // compute distance-based LOD
    uint key = u_SubdBufferIn[threadID].y;
    ccpatch ccp; subd(key, v_in, ccp);
    int targetLod = int(computeLod(ccp.v));
    int parentLod = targetLod;
#if FLAG_FREEZE
    parentLod = targetLod = findMSB(key) / 2;
#endif
#if FLAG_UNIFORM
    parentLod = targetLod = UNIFORM_SUBD_FACTOR;
#endif
    updateSubdBuffer(primID, key, targetLod, parentLod);

#if FLAG_CULL
    // Cull invisible nodes
    mat4 mvp = u_Transform.modelViewProjection;
    vec3 bmin = min(min(ccp.v[0], ccp.v[1]), min(ccp.v[2], ccp.v[3])).xyz;
    vec3 bmax = max(max(ccp.v[0], ccp.v[1]), max(ccp.v[2], ccp.v[3])).xyz;

    if (/* is visible ? */frustumCullingTest(mvp, bmin, bmax)) {
#else
    if (true) {
#endif // FLAG_CULL
        // set tess levels
        int tessLevel = PATCH_TESS_LEVEL;
        gl_TessLevelInner[0] =
        gl_TessLevelInner[1] =
        gl_TessLevelOuter[0] =
        gl_TessLevelOuter[1] =
        gl_TessLevelOuter[2] =
        gl_TessLevelOuter[3] = tessLevel;

        // set output data
        o_Patch[gl_InvocationID] = ccp;
    } else /* is not visible ? */ {
        // cull the geometry
        gl_TessLevelInner[0] =
        gl_TessLevelInner[1] =
        gl_TessLevelOuter[0] =
        gl_TessLevelOuter[1] =
        gl_TessLevelOuter[2] =
        gl_TessLevelOuter[3] = 0;
    }
}
#endif

// -----------------------------------------------------------------------------
/**
 * Tessellation Evaluation Shader
 *
 * This tessellaction evaluation shader is responsible for placing the
 * geometry properly on the input mesh (here a terrain).
 */
#ifdef TESS_EVALUATION_SHADER
layout (quads, ccw, equal_spacing) in;
layout (location = 0) in ccpatch i_Patch[];

out FragData {
    vec4 tgU;
    vec4 bgV;
} o_FragData;

vec2 berp(in vec2 v_in[4], vec2 u)
{
    return mix(mix(v_in[0], v_in[1], u.x), mix(v_in[3], v_in[2], u.x), u.y);
}

vec4 berp(in vec4 v_in[4], vec2 u)
{
    return mix(mix(v_in[0], v_in[1], u.x), mix(v_in[3], v_in[2], u.x), u.y);
}

void main()
{
    vec4 v[4] = i_Patch[0].v;
    vec4 finalVertex = berp(v, gl_TessCoord.xy);
    vec2 uv = berp(i_Patch[0].uv, gl_TessCoord.xy);

    o_FragData.tgU.xyz = normalize(berp(i_Patch[0].tg, gl_TessCoord.xy).xyz);
    o_FragData.bgV.xyz = normalize(berp(i_Patch[0].bg, gl_TessCoord.xy).xyz);
    o_FragData.tgU.w = uv.x;
    o_FragData.bgV.w = uv.y;
    gl_Position = u_Transform.modelViewProjection * finalVertex;
}
#endif

// -----------------------------------------------------------------------------
/**
 * Fragment Shader
 *
 * This fragment shader is responsible for shading the final geometry.
 */
#ifdef FRAGMENT_SHADER
in FragData {
    vec4 tgU;
    vec4 bgV;
} i_FragData;

layout(location = 0) out vec4 o_FragColor;

// barycentric interpolation
vec3 berp(in vec3 v_in[4], in vec2 u)
{
    return mix(mix(v_in[0], v_in[1], u.x), mix(v_in[3], v_in[2], u.x), u.y);
}

void main()
{
#if 0
    vec3 c[4] = vec3[4](vec3(0.0,0.25,0.25),
                        vec3(0.86,0.00,0.00),
                        vec3(1.0,0.50,0.00),
                        vec3(0.5));
    vec3 color = berp(c, i_TexCoord);

    o_FragColor = vec4(color, 1);
#endif
    vec3 tg = i_FragData.tgU.xyz;
    vec3 bg = i_FragData.bgV.xyz;
    vec3 n = normalize(cross(tg, bg));

    vec2 uv = vec2(i_FragData.tgU.w, i_FragData.bgV.w);
    o_FragColor = vec4(clamp((normalize(n)), 0.0, 1.0), 1);
    o_FragColor = vec4(clamp(n.zzz, 0.0, 1.0), 1);
}

#endif
