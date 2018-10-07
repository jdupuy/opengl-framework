#line 1

#ifdef COMPUTE_SHADER
layout (binding = BUFFER_BINDING_SUBD_COUNTER)
uniform atomic_uint u_SubdBufferCounter;

//Just for reseting
layout(binding = BUFFER_BINDING_CULLED_SUBD_COUNTER)
uniform atomic_uint u_CulledSubdBufferCounter;


layout (std430, binding = BUFFER_BINDING_INDIRECT_COMMAND)		//BUFFER_DISPATCH_INDIRECT
buffer IndirectCommandBuffer {
	uint u_IndirectCommand[8];
};

layout (local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

void main()
{

#if UPDATE_INDIRECT_STRUCT
    uint cnt = atomicCounter(u_SubdBufferCounter) / UPDATE_INDIRECT_VALUE_DIVIDE + UPDATE_INDIRECT_VALUE_ADD;

    u_IndirectCommand[UPDATE_INDIRECT_OFFSET] = cnt;

	//Hack
	u_IndirectCommand[7] = atomicCounter(u_SubdBufferCounter);
#endif


	//Reset atomic counters
#if UPDATE_INDIRECT_RESET_COUNTER1
	atomicCounterExchange(u_SubdBufferCounter, 0);
#endif
#if UPDATE_INDIRECT_RESET_COUNTER2
	atomicCounterExchange(u_CulledSubdBufferCounter, 0);
#endif

}
#endif
