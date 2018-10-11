/* terrain_cs_lod.glsl - public domain
    (created by Jonathan Dupuy and Cyril Crassin)

    This code has dependencies on the following GLSL sources:
    - fcull.glsl
    - isubd.glsl
    - terrain_common.glsl
*/

#define USE_OPTIMIZED_TASK_PARAMETER_BLOCK          1

#define NUM_CLIPPING_PLANES                         6

#define MeshPatchAttributes() vec4 vertices[3]; uint key; 


////////////////////////////////////////////////////////////////////////////////
// Implicit Subdivition Sahder for Terrain Rendering
//

layout(std430, binding = BUFFER_BINDING_SUBD1)
readonly buffer SubdBufferIn {
    uvec2 u_SubdBufferIn[];
};

layout(std430, binding = BUFFER_BINDING_INSTANCED_GEOMETRY_VERTICES)
readonly buffer VertexBufferInstanced {
    vec2 u_VertexBufferInstanced[];
};

layout(std430, binding = BUFFER_BINDING_INSTANCED_GEOMETRY_INDEXES)
readonly buffer IndexBufferInstanced {
    uint16_t u_IndexBufferInstanced[];
};

layout(std430, binding = BUFFER_BINDING_INDIRECT_COMMAND)
buffer IndirectCommandBuffer {
    uint u_IndirectCommand[8];
};


// -----------------------------------------------------------------------------
/**
 * Task Shader
 *
 * This task shader is responsible for updating the
 * subdivision buffer and sending visible geometry to the mesh shader.
 */
#ifdef TASK_SHADER
layout(local_size_x = COMPUTE_THREAD_COUNT) in;

#if USE_OPTIMIZED_TASK_PARAMETER_BLOCK == 0
taskNV out Patch{
    MeshPatchAttributes()
} o_Patch[COMPUTE_THREAD_COUNT];
#else
taskNV out Patch{
    vec4 vertices[3 * COMPUTE_THREAD_COUNT];
} o_Patch;
#endif



void main()
{

    // get threadID (each key is associated to a thread)
    uint threadID = gl_GlobalInvocationID.x;

    bool isVisible = true;

    uint key; vec3 v[3];

    // early abort if the threadID exceeds the size of the subdivision buffer
    if (threadID >= u_IndirectCommand[7]) {   //Num triangles is stored in the last reserved field of the draw indiretc structure

        isVisible = false;

    } else {

        // get coarse triangle associated to the key
        uint primID = u_SubdBufferIn[threadID].x;
        vec3 v_in[3] = vec3[3](
            u_VertexBuffer[u_IndexBuffer[primID * 3]].xyz,
            u_VertexBuffer[u_IndexBuffer[primID * 3 + 1]].xyz,
            u_VertexBuffer[u_IndexBuffer[primID * 3 + 2]].xyz
            );

        // compute distance-based LOD
        key = u_SubdBufferIn[threadID].y;
        vec3 vp[3]; subd(key, v_in, v, vp);
        int targetLod = int(computeLod(v));
        int parentLod = int(computeLod(vp));
#if FLAG_FREEZE
        targetLod = parentLod = findMSB(key);
#endif
        


#if FLAG_CULL
        // Cull invisible nodes
        mat4 mvp = u_Transform.modelViewProjection;
        vec3 bmin = min(min(v[0], v[1]), v[2]);
        vec3 bmax = max(max(v[0], v[1]), v[2]);

        // account for displacement in bound computations
#   if FLAG_DISPLACE
        bmin.z = 0;
        bmax.z = u_DmapFactor;
#   endif

        isVisible = frustumCullingTest(mvp, bmin.xyz, bmax.xyz);
#endif // FLAG_CULL


        updateSubdBuffer(primID, key, targetLod, parentLod);
    }


    uint laneID = gl_LocalInvocationID.x;
    uint voteVisible = ballotThreadNV(isVisible);
    uint numTasks = bitCount(voteVisible);

    if (laneID == 0) {
        gl_TaskCountNV = numTasks;
    }


    if (isVisible) {
        uint idxOffset = bitCount(voteVisible & gl_ThreadLtMaskNV);

        // set output data
#if USE_OPTIMIZED_TASK_PARAMETER_BLOCK == 0
        o_Patch[idxOffset].vertices = vec4[3](vec4(v[0], 1.0), vec4(v[1], 1.0), vec4(v[2], 1.0));
        o_Patch[idxOffset].key = key;
#else
        o_Patch.vertices[idxOffset * 3 + 0] = vec4(v[0].xyz, v[1].x);
        o_Patch.vertices[idxOffset * 3 + 1] = vec4(v[1].yz, v[2].xy);
        o_Patch.vertices[idxOffset * 3 + 2] = vec4(v[2].z, uintBitsToFloat(key), 0.0, 0.0);
#endif

    }

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

const int gpuSubd = PATCH_SUBD_LEVEL;

layout(local_size_x = COMPUTE_THREAD_COUNT) in;
layout(max_vertices = INSTANCED_MESH_VERTEX_COUNT, max_primitives = INSTANCED_MESH_PRIMITIVE_COUNT) out;
layout(triangles) out;

#if USE_OPTIMIZED_TASK_PARAMETER_BLOCK == 0
taskNV in Patch{
    MeshPatchAttributes()
} i_Patch[COMPUTE_THREAD_COUNT];
#else
taskNV in Patch{
    vec4 vertices[3 * COMPUTE_THREAD_COUNT];
} i_Patch;
#endif


layout(location = 0) out Interpolants{
    vec2 o_TexCoord;
} OUT[INSTANCED_MESH_VERTEX_COUNT];

void main()
{

    int id = int(gl_WorkGroupID.x);
    uint laneID = gl_LocalInvocationID.x;


    //Multi-threads, *load* instanced geom
#if USE_OPTIMIZED_TASK_PARAMETER_BLOCK == 0
    vec3 v[3] = vec3[3](
        i_Patch[id].vertices[0].xyz,
        i_Patch[id].vertices[1].xyz,
        i_Patch[id].vertices[2].xyz
        );

    uint key = i_Patch[id].key;
#else
    vec3 v[3] = vec3[3](
        i_Patch.vertices[id * 3 + 0].xyz,
        vec3(i_Patch.vertices[id * 3 + 0].w, i_Patch.vertices[id * 3 + 1].xy),
        vec3(i_Patch.vertices[id * 3 + 1].zw, i_Patch.vertices[id * 3 + 2].x)
        );

    uint key = floatBitsToUint(i_Patch.vertices[id * 3 + 2].y);
#endif


    const int vertexCnt = INSTANCED_MESH_VERTEX_COUNT;
    const int triangleCnt = INSTANCED_MESH_PRIMITIVE_COUNT;
    const int indexCnt = triangleCnt * 3;

    gl_PrimitiveCountNV = triangleCnt;


    const int numLoop = (vertexCnt + COMPUTE_THREAD_COUNT-1) / COMPUTE_THREAD_COUNT;
    for (int l = 0; l < numLoop; ++l) {
        int curVert = int(laneID) + l * COMPUTE_THREAD_COUNT;

        curVert = min(curVert, vertexCnt - 1);
        {

            vec2 instancedBaryCoords = u_VertexBufferInstanced[curVert];

            vec3 finalVertex = berp(v, instancedBaryCoords);



#if FLAG_DISPLACE
            finalVertex.z += dmap(finalVertex.xy);
#endif
#if SHADING_LOD
            //vec2 tessCoord = instancedBaryCoords;
            int keyLod = findMSB(key);

            vec2 tessCoord = intValToColor2(keyLod);
#else
            vec2 tessCoord = finalVertex.xy * 0.5 + 0.5;
#endif

            OUT[curVert].o_TexCoord = tessCoord;
            gl_MeshVerticesNV[curVert].gl_Position = u_Transform.modelViewProjection * vec4(finalVertex, 1.0);
            for (int d = 0; d < NUM_CLIPPING_PLANES; d++) {
                gl_MeshVerticesNV[curVert].gl_ClipDistance[d] = 1.0;
            }
        }

    }


    const int numLoopIdx = (indexCnt + COMPUTE_THREAD_COUNT -1) / COMPUTE_THREAD_COUNT;
    for (int l = 0; l < numLoopIdx; ++l) {
        int curIdx = int(laneID) + l * COMPUTE_THREAD_COUNT;

        curIdx = min(curIdx, indexCnt - 1);
        {
            uint indexVal = u_IndexBufferInstanced[curIdx];

            gl_PrimitiveIndicesNV[curIdx] = indexVal;
        }

    }

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
layout(location = 0) in Interpolants{
    vec2 o_TexCoord;
} IN;

layout(location = 0) out vec4 o_FragColor;

void main()
{
    o_FragColor = shadeFragment(IN.o_TexCoord);
}

#endif
