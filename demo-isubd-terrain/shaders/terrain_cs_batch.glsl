#line 1

#ifdef COMPUTE_SHADER
layout (binding = BUFFER_BINDING_SUBD_COUNTER)
uniform atomic_uint u_SubdBufferCounter;

struct IndirectCommand {
#if FLAG_COMPUTE_PATH
    uint local_size_x;
    uint local_size_y;
    uint local_size_z;
    uint align[5];
#elif FLAG MESH PATH
    uint count;
    uint first;
    uint align[6];
#else
    uint data[8];
#endif
};

layout (std430, binding = BUFFER_BINDING_INDIRECT_COMMAND)
buffer IndirectCommandBuffer {
    IndirectCommand u_IndirectCommand;
};

layout (local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

void main()
{
    uint cnt = atomicCounter(u_SubdBufferCounter) / COMPUTE_THREAD_COUNT + 1u;

#if FLAG_COMPUTE_PATH
    u_IndirectCommand.local_size_x = cnt;
#elif FLAG MESH PATH
    u_IndirectCommand.count = cnt;
#endif
}
#endif
