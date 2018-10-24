/* terrain_updateIndirect_cs.glsl - public domain
    (created by Jonathan Dupuy and Cyril Crassin)

*/
#line 6

#ifdef COMPUTE_SHADER

layout(std430, binding = BUFFER_BINDING_INDIRECT_COMMAND)		//BUFFER_DISPATCH_INDIRECT
buffer IndirectCommandBuffer {
    uint u_IndirectCommand[8];
};

layout(local_size_x = SUBD_COMPACTION_NUM_THREADS, local_size_y = 1, local_size_z = 1) in;


void main() {
    uint threadID = gl_GlobalInvocationID.x;

    if (threadID >= u_IndirectCommand[7])
        return;

#if 1

    uint keyIdx = u_DeletedSubdBuffer[threadID];

    if (keyIdx < (u_IndirectCommand[6] - u_IndirectCommand[7]) )
    {
        
#  if 0
        uint movedIdx = atomicCounterSubtract(u_SubdBufferCounterEnd, 1)-1;
        uvec2 movedVal = u_SubdBufferIn[movedIdx];
        //uvec2 movedVal = uvec2(0,0);

        //if (movedIdx > keyIdx) {
            u_SubdBufferIn[keyIdx] = movedVal;
            u_SubdBufferIn[movedIdx] = uvec2(0, 0);
        /*} else {
            uint destdIdx = atomicCounterAdd(u_SubdBufferCounterEnd, 1);
            u_SubdBufferIn[destdIdx] = movedVal;
            u_SubdBufferIn[movedIdx] = uvec2(0, 0);
        }*/
#  elif 0
        uint movedIdx = atomicCounter(u_SubdBufferCounterEnd) - 1 - threadID;
        uvec2 movedVal = u_SubdBufferIn[movedIdx];
        //uvec2 movedVal = uvec2(0,0);

        u_SubdBufferIn[keyIdx] = movedVal;
        u_SubdBufferIn[movedIdx] = uvec2(0, 0);
#  elif 1

        uvec2 movedVal;
        //do {
            uint movedIdx = atomicCounterSubtract(u_SubdBufferCounterEnd, 1) - 1;
            movedVal = u_SubdBufferIn[movedIdx];

            if (movedVal != uvec2(0, 0)) {
                u_SubdBufferIn[keyIdx] = movedVal;
                u_SubdBufferIn[movedIdx] = uvec2(0, 0);
            }

        //} while (movedVal == uvec2(0, 0));
#  endif

    }
    /*else {
        u_SubdBufferIn[keyIdx] = uvec2(0, 0);
    }*/
#endif
    //u_SubdBufferIn[threadID] = uvec2(0,0);
}

#endif
