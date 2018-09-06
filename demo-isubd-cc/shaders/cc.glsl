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

layout (std430, binding = BUFFER_BINDING_PATCH)
readonly buffer VertexBuffer {
	vec4 u_VertexBuffer[];
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
    // Note that we multiply the result by two because the triangle's
    // edge lengths decreases by half every two subdivision steps.
    return -2.0 * log2(clamp(z * lodFactor / 2.0, 0.0f, 1.0f));
}

float computeLod(vec3 c)
{
#if FLAG_DISPLACE
    c.z+= dmap(u_Transform.viewInv[3].xy);
#endif

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
out Patch {
    vec4 vertices[4];
    flat uint key;
} o_Patch[];

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
    } else if (/* keep ? */ keyLod <= (parentLod + 1)) {
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

    // get coarse triangle associated to the key
    uint primID = u_SubdBufferIn[threadID].x;
    vec4 v_in[16] = vec4[16](
        u_VertexBuffer[0],
        u_VertexBuffer[1],
        u_VertexBuffer[2],
        u_VertexBuffer[3],
        u_VertexBuffer[4],
        u_VertexBuffer[5],
        u_VertexBuffer[6],
        u_VertexBuffer[7],
        u_VertexBuffer[8],
        u_VertexBuffer[9],
        u_VertexBuffer[10],
        u_VertexBuffer[11],
        u_VertexBuffer[12],
        u_VertexBuffer[13],
        u_VertexBuffer[14],
        u_VertexBuffer[15]
    );

    // compute distance-based LOD
    uint key = u_SubdBufferIn[threadID].y;
    vec4 v[4], vp[4]; subd(key, v_in, v/*, vp*/);
    int targetLod = int(computeLod(v));
    int parentLod = int(computeLod(vp));
#if FLAG_FREEZE
    targetLod = parentLod = findMSB(key);
#endif
    parentLod = targetLod = 4;
    updateSubdBuffer(primID, key, targetLod, parentLod);

#if FLAG_CULL
    // Cull invisible nodes
    mat4 mvp = u_Transform.modelViewProjection;
    vec3 bmin = min(min(v[0], v[1]), v[2]).xyz;
    vec3 bmax = max(max(v[0], v[1]), v[2]).xyz;

    // account for displacement in bound computations
#   if FLAG_DISPLACE
    bmin.z = 0;
    bmax.z = u_DmapFactor;
#   endif

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
        o_Patch[gl_InvocationID].vertices = v;
        o_Patch[gl_InvocationID].key = key;

#if 0
        o_Patch[gl_InvocationID].vertices = vec4[4](vec4(0, 0, 0, 1),
                                                    vec4(1, 0, 0, 1),
                                                    vec4(1, 1, 0, 1),
                                                    vec4(0, 1, 0, 1));
#endif
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
in Patch {
    vec4 vertices[4];
    flat uint key;
} i_Patch[];

layout(location = 0) out vec2 o_TexCoord;

vec4 berp(in vec4 v_in[4], vec2 u)
{
    return mix(mix(v_in[0], v_in[1], u.x), mix(v_in[3], v_in[2], u.x), u.y);
}

void main()
{
    vec4 v[4] = i_Patch[0].vertices;
    vec4 finalVertex = berp(v, gl_TessCoord.xy);

#if FLAG_DISPLACE
    finalVertex.z+= dmap(finalVertex.xy);
#endif

    o_TexCoord = finalVertex.xy;
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
layout(location = 0) in vec2 i_TexCoord;
layout(location = 0) out vec4 o_FragColor;

void main()
{
    o_FragColor = vec4(0, 0, 0, 1);
}

#endif
