#line 1

#ifdef COMPUTE_SHADER
layout (std430, binding = BUFFER_BINDING_SUBD_COUNTER)
buffer DispatchIndirectCommandBuffer {
    int u_DispatchIndirectCommandBuffer[];
};

layout (local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

void main()
{
    u_DispatchIndirectCommandBuffer[0] =
        u_DispatchIndirectCommandBuffer[0] / COMPUTE_THREAD_COUNT + 1;
}
#endif
