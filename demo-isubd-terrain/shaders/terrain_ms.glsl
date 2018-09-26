#line 2

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

layout (std430, binding = BUFFER_BINDING_GEOMETRY_INDEXES)
readonly buffer IndexBuffer {
    uint u_IndexBuffer[];
};

layout (binding = BUFFER_BINDING_SUBD_COUNTER)
uniform atomic_uint u_SubdBufferCounter;

layout (binding = BUFFER_BINDING_SUBD_COUNTER_PREVIOUS)
uniform atomic_uint u_PreviousSubdBufferCounter;

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

layout(std140, binding = BUFFER_BINDING_SUBD_COUNTER)
readonly buffer PreviousSubdBufferCounter {
    uint u_PreviousSubdBufferCounter;
};

uniform sampler2D u_DmapSampler;
uniform float u_DmapFactor;
uniform float u_LodFactor;

// displacement map
float dmap(vec2 pos)
{
#if 0
    return cos(20.0 * pos.x) * cos(20.0 * pos.y) / 2.0 * u_DmapFactor;
#else
    return (texture(u_DmapSampler, pos * 0.5 + 0.5).x) * u_DmapFactor;
#endif
}

float distanceToLod(float z, float lodFactor)
{
    // Note that we multiply the result by two because the triangle's
    // edge lengths decreases by half every two subdivision steps.
    return -2.0 * log2(clamp(z * lodFactor, 0.0f, 1.0f));
}

// -----------------------------------------------------------------------------
/**
 * Task Shader
 *
 * This task shader is responsible for updating the
 * subdivision buffer and sending visible geometry to the mesh shader.
 */
#ifdef TASK_SHADER
layout(local_size_x = COMPUTE_THREAD_COUNT) in;

taskNV out Patch {
    vec4 vertices[3];
    uint key;
} o_Patch;

float computeLod(vec3 c)
{
#if FLAG_DISPLACE
    c.z += dmap(u_Transform.viewInv[3].xy);
#endif

    vec3 cxf = (u_Transform.modelView * vec4(c, 1)).xyz;
    float z = length(cxf);

    return distanceToLod(z, u_LodFactor);
}

float computeLod(in vec4 v[3])
{
    vec3 c = (v[1].xyz + v[2].xyz) / 2.0;
    return computeLod(c);
}

void writeKey(uint primID, uint key)
{
    uint idx = atomicCounterIncrement(u_SubdBufferCounter);

    u_SubdBufferOut[idx] = uvec2(primID, key);
}

void updateSubdBuffer(uint primID, uint key, int targetLod, int parentLod)
{
    // extract subdivision level associated to the key
    int keyLod = findMSB(key);

    // update the key accordingly
    if (/* subdivide ? */ keyLod < targetLod && !isLeafKey(key)) {
        uint children[2]; childrenKeys(key, children);

        writeKey(primID, children[0]);
        writeKey(primID, children[1]);
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
#if 1
    // get threadID (each key is associated to a thread)
#if 1
    int threadID = int(gl_GlobalInvocationID.x);
#else
    int threadID = int(gl_WorkGroupID.x * COMPUTE_THREAD_COUNT + gl_LocalInvocationID.x);
    threadID = 1;
#endif

    // early abort if the threadID exceeds the size of the subdivision buffer
    if (threadID >= u_PreviousSubdBufferCounter)
        return;

    // get coarse triangle associated to the key
    uint primID = u_SubdBufferIn[threadID].x;
    vec4 v_in[3] = vec4[3](
        u_VertexBuffer[u_IndexBuffer[primID * 3]],
        u_VertexBuffer[u_IndexBuffer[primID * 3 + 1]],
        u_VertexBuffer[u_IndexBuffer[primID * 3 + 2]]
    );

    // compute distance-based LOD
    uint key = u_SubdBufferIn[threadID].y;
    vec4 v[3], vp[3]; subd(key, v_in, v, vp);
    int targetLod = int(computeLod(v));
    int parentLod = int(computeLod(vp));
#if FLAG_FREEZE
    targetLod = parentLod = findMSB(key);
#endif
    updateSubdBuffer(primID, key, targetLod, parentLod);

#if 0//FLAG_CULL
    // Cull invisible nodes
    mat4 mvp = u_Transform.modelViewProjection;
    vec4 bmin = min(min(v[0], v[1]), v[2]);
    vec4 bmax = max(max(v[0], v[1]), v[2]);

    // account for displacement in bound computations
#   if FLAG_DISPLACE
    bmin.z = 0;
    bmax.z = u_DmapFactor;
#   endif

    if (/* is visible ? */frustumCullingTest(mvp, bmin.xyz, bmax.xyz)) {
#else
    if (true) {
#endif // FLAG_CULL

        // set output data
        o_Patch.vertices = v;
        o_Patch.key = key;

        gl_TaskCountNV = 1;
    } else {
        gl_TaskCountNV = 0;
    }
#endif
}
#endif

// -----------------------------------------------------------------------------
/**
 * Mesh Shader
 *
 * This mesh shader is responsible for placing the
 * geometry properly on the input mesh (here a terrain).
 */
#ifdef MESH_SHADER
#line 1

layout(local_size_x = 1) in;
layout(max_vertices = 32, max_primitives = 32) out;
layout(triangles) out;


taskNV in Patch {
    vec4 vertices[3];
    uint key;
} i_Patch;

layout(location = 0) out Interpolants{
    vec2 o_TexCoord;
} OUT[];

void main()
{
    //o_TexCoord = vec2(0);
#if 0
    vec3 v[3] = i_Patch.vertices;

#if FLAG_DISPLACE
    finalVertex.z+= dmap(finalVertex.xy);
#endif

    gl_PrimitiveCountNV = 1;

    /*gl_PrimitiveIndicesNV[0] = 0;
    gl_PrimitiveIndicesNV[1] = 1;
    gl_PrimitiveIndicesNV[2] = 2;*/

    int edgeCnt = 1;
    float edgeLength = 1.0 / float(edgeCnt);
    int vertexCnt = 3;
    int i = 0;

    // start a strip
    for (int j = 0; j < vertexCnt; ++j) {
        int ui = j >> 1;
        int vi = (edgeCnt - 1) - (i - (j & 1));
        vec2 tessCoord = vec2(ui, vi) * edgeLength;
        vec3 finalVertex = berp(v, tessCoord);

        OUT[0].o_TexCoord = tessCoord;
        gl_MeshVerticesNV[j].gl_Position = u_Transform.modelViewProjection * vec4(finalVertex, 1);
        for (int k = 0; k < 6; k++) {
            gl_MeshVerticesNV[j].gl_ClipDistance[k] = 1.0;
        }

        gl_PrimitiveIndicesNV[j] = j;
    }
#else
#line 242
#define NUM_CLIPPING_PLANES 6

    vec3 v[3] = vec3[3](i_Patch.vertices[0].xyz, i_Patch.vertices[1].xyz, i_Patch.vertices[2].xyz);
    //v = vec3[3](vec3(0), vec3(1,0,0), vec3(0,1,0));

    gl_PrimitiveCountNV = 1;

    /*gl_MeshVerticesNV[0].gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
    gl_MeshVerticesNV[1].gl_Position = vec4(1.0, 0.0, 0.0, 1.0);
    gl_MeshVerticesNV[2].gl_Position = vec4(0.0, 1.0, 0.0, 1.0);*/

    for (int vert = 0; vert < 3; ++vert) {
        vec2 uv = vec2(vert & 1, vert >> 1 & 1);
        vec3 finalVertex = berp(v, uv);
        //gl_MeshVerticesNV[vert].gl_Position = vec4(uv, 0.0, 1.0);
        gl_MeshVerticesNV[vert].gl_Position = u_Transform.modelViewProjection * vec4(finalVertex, 1);

        gl_PrimitiveIndicesNV[vert] = vert;

        for (int i = 0; i < NUM_CLIPPING_PLANES; i++) {
            gl_MeshVerticesNV[vert].gl_ClipDistance[i] = 1.0;
        }

        OUT[vert].o_TexCoord = uv;
    }



    /*gl_PrimitiveIndicesNV[0] = 0;
    gl_PrimitiveIndicesNV[1] = 1;
    gl_PrimitiveIndicesNV[2] = 2;*/
#endif
}
#endif

// -----------------------------------------------------------------------------
/**
 * Fragment Shader
 *
 * This fragment shader is responsible for shading the final geometry.
 */
#ifdef FRAGMENT_SHADER
//layout(location = 0) in vec2 i_TexCoord;
layout(location = 0) in Interpolants {
    vec2 o_TexCoord;
} IN;

layout(location = 0) out vec4 o_FragColor;

void main()
{
    vec2 i_TexCoord = IN.o_TexCoord;

    vec3 c[3] = vec3[3](vec3(0.0,1.0,1.0)/4.0,
                        vec3(0.86,0.00,0.00),
                        vec3(1.0,0.50,0.00)/1.0);
    vec3 color = berp(c, i_TexCoord);

    o_FragColor = vec4(color, 1);
}

#endif
