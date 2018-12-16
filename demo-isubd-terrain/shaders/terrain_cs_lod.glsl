/* terrain_cs_lod.glsl - public domain
    (created by Jonathan Dupuy and Cyril Crassin)

    This code has dependencies on the following GLSL sources:
    - fcull.glsl
    - isubd.glsl
    - terrain_common.glsl
*/

////////////////////////////////////////////////////////////////////////////////
// Implicit Subdivision Shader for Terrain Rendering
//

layout (std430, binding = BUFFER_BINDING_SUBD1)
readonly buffer SubdBufferIn {
    uvec2 u_SubdBufferIn[];
};

layout (std430, binding = BUFFER_BINDING_CULLED_SUBD)
buffer CulledSubdBuffer {
    uvec2 u_CulledSubdBuffer[];
};

layout(std430, binding = BUFFER_BINDING_INDIRECT_COMMAND)
buffer IndirectCommandBuffer {
	uint u_IndirectCommand[8];
};

//layout (binding = BUFFER_BINDING_SUBD_COUNTER, offset = 4)
layout(binding = BUFFER_BINDING_CULLED_SUBD_COUNTER)
uniform atomic_uint u_CulledSubdBufferCounter;



// -----------------------------------------------------------------------------
/**
 * Compute LoD Shader
 *
 * This compute shader is responsible for updating the subdivision
 * buffer and visible buffer that will be sent to the rasterizer.
 */
#ifdef COMPUTE_SHADER
layout (local_size_x = COMPUTE_THREAD_COUNT,
        local_size_y = 1,
        local_size_z = 1) in;


void main()
{
    // get threadID (each key is associated to a thread)
    uint threadID = gl_GlobalInvocationID.x;

    // early abort if the threadID exceeds the size of the subdivision buffer
    //if (threadID >= atomicCounter(u_PreviousSubdBufferCounter))
	if (threadID >= u_IndirectCommand[7])
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

#if FLAG_CULL
    // Cull invisible nodes
    mat4 mvp = u_Transform.modelViewProjection;
    vec4 bmin = min(min(v[0], v[1]), v[2]);
    vec4 bmax = max(max(v[0], v[1]), v[2]);

    // account for displacement in bound computations
#   if FLAG_DISPLACE
    bmin.z = 0;
    bmax.z = u_DmapFactor;
#   endif

    // update CulledSubdBuffer
    if (/* is visible ? */frustumCullingTest(mvp, bmin.xyz, bmax.xyz)) {
#else
    if (true) {
#endif // FLAG_CULL
        // write key
        //uint idx = atomicCounterIncrement(u_CulledSubdBufferCounter[1]);
		uint idx = atomicCounterIncrement(u_CulledSubdBufferCounter);

        u_CulledSubdBuffer[idx] = uvec2(primID, key);
    }
}
#endif

