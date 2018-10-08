#line 2


//#undef COMPUTE_THREAD_COUNT
//#define COMPUTE_THREAD_COUNT 1
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

layout(std430, binding = BUFFER_BINDING_INSTANCED_GEOMETRY_VERTICES)
readonly buffer VertexBufferInstanced {
	vec2 u_VertexBufferInstanced[];
};

layout(std430, binding = BUFFER_BINDING_INSTANCED_GEOMETRY_INDEXES)
readonly buffer IndexBufferInstanced {
	uint16_t u_IndexBufferInstanced[];
};


layout (binding = BUFFER_BINDING_SUBD_COUNTER)
uniform atomic_uint u_SubdBufferCounter;

//Deprecated
//layout (binding = BUFFER_BINDING_SUBD_COUNTER_PREVIOUS)
//uniform atomic_uint u_PreviousSubdBufferCounter;

layout(std430, binding = BUFFER_BINDING_INDIRECT_COMMAND)		//BUFFER_DISPATCH_INDIRECT
buffer IndirectCommandBuffer {
	uint u_IndirectCommand[8];
};


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

uniform sampler2D u_DmapSampler; // displacement map
uniform sampler2D u_SmapSampler; // slope map
uniform float u_DmapFactor;
uniform float u_LodFactor;


vec2 intValToColor2(int keyLod) {
	keyLod = keyLod % 64;

	int bx = (keyLod & 0x1) | ((keyLod >> 1) & 0x2) | ((keyLod >> 2) & 0x4);
	int by = ((keyLod >> 1) & 0x1) | ((keyLod >> 2) & 0x2) | ((keyLod >> 3) & 0x4);

	return vec2(float(bx) / 7.0f, float(by) / 7.0f);
}

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


//#define MeshPatchAttributes() vec4 vertices[3]; uint key; 
#define MeshPatchAttributes() vec4 vertices0; vec4 vertices1; vec2 vertices2;



// -----------------------------------------------------------------------------
/**
 * Task Shader
 *
 * This task shader is responsible for updating the
 * subdivision buffer and sending visible geometry to the mesh shader.
 */
#ifdef TASK_SHADER
layout(local_size_x = COMPUTE_THREAD_COUNT) in;

#if 0
taskNV out Patch {
	MeshPatchAttributes()
} o_Patch[COMPUTE_THREAD_COUNT];
#else
taskNV out Patch{
	vec4 vertices[3* COMPUTE_THREAD_COUNT];
} o_Patch;
#endif


float computeLod(vec3 c)
{
#if FLAG_DISPLACE
    c.z += dmap(u_Transform.viewInv[3].xy);
#endif

	vec4 cxf4 = (u_Transform.modelView * vec4(c, 1));
    vec3 cxf = cxf4.xyz;
    float z = length(cxf);

    return distanceToLod(z, u_LodFactor);
}

float computeLod(in vec4 v[3])
{
    vec3 c = (v[1].xyz + v[2].xyz) / 2.0;
    return computeLod(c);
}
float computeLod(in vec3 v[3])
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

		if (/* is root ? */isRootKey(key)) 
		{
			writeKey(primID, key);
		}
#if 1
		else if (/* is zero child ? */isChildZeroKey(key)) {
			writeKey(primID, parentKey(key));
		}
#else
		else {
			int numMergeLevels = keyLod - (parentLod - 1);

			uint mergeMask = (key & ((1 << numMergeLevels) - 1));
			if (mergeMask == 0) 
			{
				key = (key >> numMergeLevels);
				writeKey(primID, key);
			}

		}
#endif
    }
}

void main()
{

    // get threadID (each key is associated to a thread)
    uint threadID = gl_GlobalInvocationID.x;

	bool isVisible = true;
	
	uint key; vec3 v[3];

    // early abort if the threadID exceeds the size of the subdivision buffer
	//if (threadID >= atomicCounter(u_PreviousSubdBufferCounter)) 
	if ( threadID >= u_IndirectCommand[7] )
	{

		isVisible = false;
		//return;
	} else {

		// get coarse triangle associated to the key
		//CC: Why loading that from VRAM ??
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
		updateSubdBuffer(primID, key, targetLod, parentLod);


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


		//isVisible = true;
	}



	//if (gl_WorkGroupID.x != 0)
	//	isVisible = false;


	uint laneID = gl_LocalInvocationID.x;
	uint voteVisible = ballotThreadNV(isVisible);
	uint numTasks = bitCount(voteVisible);

	if (laneID == 0) {
		gl_TaskCountNV = numTasks;
	}


	if (isVisible) {
		uint idxOffset = bitCount(voteVisible & gl_ThreadLtMaskNV);

        // set output data
        //o_Patch[idxOffset].vertices = v;
#if 0
		o_Patch[idxOffset].vertices = vec4[3](vec4(v[0], 1.0), vec4(v[1], 1.0), vec4(v[2], 1.0));
		o_Patch[idxOffset].key = key;
#elif 0
		o_Patch[idxOffset].vertices0.xyz = v[0].xyz;
		o_Patch[idxOffset].vertices0.w = v[1].x;
		o_Patch[idxOffset].vertices1.xy = v[1].yz;
		o_Patch[idxOffset].vertices1.zw = v[2].xy;
		o_Patch[idxOffset].vertices2.x = v[2].z;
		o_Patch[idxOffset].vertices2.y = uintBitsToFloat(key);
#else
		o_Patch.vertices[idxOffset * 3 + 0] = vec4(v[0].xyz, v[1].x);
		o_Patch.vertices[idxOffset * 3 + 1] = vec4(v[1].yz, v[2].xy);
		o_Patch.vertices[idxOffset * 3 + 2] = vec4(v[2].z, uintBitsToFloat(key), 0.0, 0.0);
		//o_Patch.key[idxOffset] = key;
#endif
        
       
		//if(gl_LocalInvocationID.x == 1)
		//	gl_TaskCountNV = 0;

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

#if 0
taskNV in Patch {
	MeshPatchAttributes()
} i_Patch[COMPUTE_THREAD_COUNT];
#else
taskNV in Patch{
	vec4 vertices[3 * COMPUTE_THREAD_COUNT];
} i_Patch;
#endif


layout(location = 0) out Interpolants{
    vec2 o_TexCoord;
} OUT[INSTANCED_MESH_VERTEX_COUNT];  //COMPUTE_THREAD_COUNT

void main()
{

#define NUM_CLIPPING_PLANES 6
	//int id = int(gl_LocalInvocationID.x);
	int id = int(gl_WorkGroupID.x);
	uint laneID = gl_LocalInvocationID.x;

#if 0  //Naive 1 thread - 1 triangle
	if (laneID == 0) {
		vec3 v[3] = vec3[3](
			i_Patch[id].vertices[0].xyz,
			i_Patch[id].vertices[1].xyz,
			i_Patch[id].vertices[2].xyz
			);
		//v = vec3[3](vec3(0), vec3(1,0,0), vec3(0,1,0));

		gl_PrimitiveCountNV = 1;

		/*gl_MeshVerticesNV[0].gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
		gl_MeshVerticesNV[1].gl_Position = vec4(1.0, 0.0, 0.0, 1.0);
		gl_MeshVerticesNV[2].gl_Position = vec4(0.0, 1.0, 0.0, 1.0);*/

		for (int vert = 0; vert < 3; ++vert) {
			vec2 uv = vec2(vert & 1, vert >> 1 & 1);
			vec3 finalVertex = berp(v, uv);

			//int idx = 3 * id + vert;
			int idx = vert;

			//gl_MeshVerticesNV[vert].gl_Position = vec4(uv, 0.0, 1.0);
			gl_MeshVerticesNV[idx].gl_Position = u_Transform.modelViewProjection * vec4(finalVertex, 1);

			gl_PrimitiveIndicesNV[idx] = idx;

			for (int i = 0; i < NUM_CLIPPING_PLANES; i++) {
				gl_MeshVerticesNV[idx].gl_ClipDistance[i] = 1.0;
			}

			OUT[idx].o_TexCoord = uv;
		}

	}
#elif 0
	//Naive multi-threads, analytic instanced geom

	vec3 v[3] = vec3[3](
		i_Patch[id].vertices[0].xyz,
		i_Patch[id].vertices[1].xyz,
		i_Patch[id].vertices[2].xyz
		);

	gl_PrimitiveCountNV = 0;

	// set tess levels
	int edgeCnt = PATCH_TESS_LEVEL;
	float edgeLength = 1.0 / float(edgeCnt);

	int numTriangles = 0;
	int curVert = 0;

	for (int i = 0; i < edgeCnt; ++i) 
	{

		int vertexCnt = 2 * i + 3;

		// start a strip
		//for (int j = 0; j < vertexCnt; ++j) 
		if(laneID<vertexCnt)
		{
			int j = int(laneID);

			int curVertOK = curVert + j;
			int numTrianglesOK = numTriangles + j - 2;

			int ui = j >> 1;
			int vi = (edgeCnt - 1) - (i - (j & 1));
			vec2 tessCoord = vec2(ui, vi) * edgeLength;
			vec3 finalVertex = berp(v, tessCoord);

#if FLAG_DISPLACE
			finalVertex.z += dmap(finalVertex.xy);
#endif

			OUT[curVertOK].o_TexCoord = tessCoord;
			gl_MeshVerticesNV[curVertOK].gl_Position = u_Transform.modelViewProjection * vec4(finalVertex, 1.0);
			for (int d = 0; d < NUM_CLIPPING_PLANES; d++) {
				gl_MeshVerticesNV[curVertOK].gl_ClipDistance[d] = 1.0;
			}
				
				
			if (j >= 2) {
				//if (numTriangles % 2 == 0) {  //If backface  culling...
					gl_PrimitiveIndicesNV[numTrianglesOK * 3 + 0] = curVertOK - 2;
					gl_PrimitiveIndicesNV[numTrianglesOK * 3 + 1] = curVertOK - 1;
					gl_PrimitiveIndicesNV[numTrianglesOK * 3 + 2] = curVertOK;
				/*}
				else {
					gl_PrimitiveIndicesNV[numTriangles * 3 + 0] = curVertOK - 1;
					gl_PrimitiveIndicesNV[numTriangles * 3 + 1] = curVertOK - 2;
					gl_PrimitiveIndicesNV[numTriangles * 3 + 2] = curVertOK;
				}*/

				///numTriangles++;
			}
			///curVert++;
			//EmitVertex();
		}
		curVert+= vertexCnt;
		numTriangles += vertexCnt - 2;
		//EndPrimitive();
	}

	//gl_PrimitiveCountNV = numTriangles;
	gl_PrimitiveCountNV = (3 << (gpuSubd * 2)) / 3;

#elif 1
	//Multi-threads, *load* instanced geom
#if 0
	vec3 v[3] = vec3[3](
		i_Patch[id].vertices[0].xyz,
		i_Patch[id].vertices[1].xyz,
		i_Patch[id].vertices[2].xyz
		);

	uint key = i_Patch[id].key;
#elif 0
	vec3 v[3] = vec3[3](
		i_Patch[id].vertices0.xyz,
		vec3(i_Patch[id].vertices0.w, i_Patch[id].vertices1.xy),
		vec3(i_Patch[id].vertices1.zw, i_Patch[id].vertices2.x)
		);

	uint key = floatBitsToUint(i_Patch[id].vertices2.y);
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


	int numLoop = (vertexCnt % COMPUTE_THREAD_COUNT) != 0 ? (vertexCnt / COMPUTE_THREAD_COUNT) + 1 : (vertexCnt / COMPUTE_THREAD_COUNT);
	for (int l = 0; l < numLoop; ++l) {
		int curVert = int(laneID) + l * COMPUTE_THREAD_COUNT;

		if (curVert < vertexCnt) {

			vec2 instancedBaryCoords = u_VertexBufferInstanced[curVert];
			
			vec3 finalVertex = berp(v, instancedBaryCoords);

			

#if FLAG_DISPLACE
			finalVertex.z += dmap(finalVertex.xy);
#endif
#if SHADING_LOD
			//vec2 tessCoord = instancedBaryCoords;
			int keyLod = findMSB(key);


			vec2 tessCoord = intValToColor2(keyLod);
			//vec2 tessCoord = intValToColor2(int(gl_WorkGroupID.x));
			//vec2 tessCoord = intValToColor2( int(i_Patch[id].taskId) );
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


	int numLoopIdx = (indexCnt % COMPUTE_THREAD_COUNT) != 0 ? (indexCnt / COMPUTE_THREAD_COUNT) + 1 : (indexCnt / COMPUTE_THREAD_COUNT);
	for (int l = 0; l < numLoopIdx; ++l) {
		int curIdx = int(laneID) + l * COMPUTE_THREAD_COUNT;

		if (curIdx < indexCnt) {
			uint indexVal = u_IndexBufferInstanced[curIdx];

			gl_PrimitiveIndicesNV[curIdx] = indexVal;
		}

	}

#elif 0
	//Analytic optimized (dedup) instanced patch
	//NOT FINISHED !!!

	vec3 v[3] = vec3[3](
		i_Patch[id].vertices[0].xyz,
		i_Patch[id].vertices[1].xyz,
		i_Patch[id].vertices[2].xyz
		);

	int edgeCnt = PATCH_TESS_LEVEL;
	float edgeLength = 1.0 / float(edgeCnt);

	//
	int sliceCnt = (1 << gpuSubd) + 1;     // side vertices;
	int vertexCnt = (sliceCnt * (sliceCnt + 1)) / 2;
	int indexCnt = 3 << (gpuSubd * 2);

	int numLoop = (vertexCnt % COMPUTE_THREAD_COUNT)!=0 ? (vertexCnt / COMPUTE_THREAD_COUNT) + 1 : (vertexCnt / COMPUTE_THREAD_COUNT);

	for (int l = 0; l < numLoop; ++l) {
		int curVert = int(laneID) + l* COMPUTE_THREAD_COUNT;

		if (curVert < vertexCnt) {
			//int prevVertexCnt = (sliceCnt * (sliceCnt + 1)) / 2;

			//endVert = sum(i, 1, sliceCnt) = sliceCnt * (sliceCnt + 1) / 2 + sliceCnt;

			int sliceCntPrev = (int(sqrt(8 * curVert + 9)) - 3) / 2;

			//int sliceCnt = (prevVertexCnt*2)
			int i = sliceCntPrev;
			//int j = curVert % sliceCntPrev;
			int j = curVert - (i * (i + 1) / 2);

			//for (int i = 0; i < sliceCnt; ++i)
			//for (int j = 0; j < i + 1; ++j)

			


			int idx = i * (i + 1) / 2 + j;
			//(curVert - j) * 2 = i * (i + 1) ;
			//int j = curVert - i * (i + 1) / 2;

			float ui = float(j) / (sliceCnt - 1);
			float vi = 1.f - float(i) / (sliceCnt - 1);

			//vertices[idx] = dja::vec2(u, v);
			vec2 tessCoord = vec2(ui, vi) * edgeLength;
			vec3 finalVertex = berp(v, tessCoord);

#if FLAG_DISPLACE
			finalVertex.z += dmap(finalVertex.xy);
#endif
			int curVertOK = idx; // curVert;

			OUT[curVertOK].o_TexCoord = tessCoord;
			gl_MeshVerticesNV[curVertOK].gl_Position = u_Transform.modelViewProjection * vec4(finalVertex, 1.0);
			for (int d = 0; d < NUM_CLIPPING_PLANES; d++) {
				gl_MeshVerticesNV[curVertOK].gl_ClipDistance[d] = 1.0;
			}

		}
	}


	int indexCntOK =  indexCnt / 6;

	int numLoopIdx = (indexCntOK % COMPUTE_THREAD_COUNT) != 0 ? (indexCntOK / COMPUTE_THREAD_COUNT) + 1 : (indexCntOK / COMPUTE_THREAD_COUNT);

	for (int l = 0; l < numLoopIdx; ++l) {
		int curSlot = int(laneID) + l * COMPUTE_THREAD_COUNT;

		if (curSlot < indexCntOK) {
			
			//endIdx = sum(i, 0, sliceCnt - 1) = sliceCnt * (sliceCnt + 1) / 2;
			//curIdx = sliceCntPrev * (sliceCntPrev + 1) / 2;

			int i = (int(sqrt(4 * curSlot + 1)) - 1);
			int j = curSlot - (i * (i + 1) / 2) ;

			int curIdxOK = (3 * i * i) + j * 6;

			gl_PrimitiveIndicesNV[curIdxOK + 0] = (i + 1) * (i + 2) / 2 + j;
			gl_PrimitiveIndicesNV[curIdxOK + 1] = (i + 1) * (i + 2) / 2 + j + 1;
			gl_PrimitiveIndicesNV[curIdxOK + 2] = i * (i + 1) / 2 + j;
			gl_PrimitiveIndicesNV[curIdxOK + 3] = gl_PrimitiveIndicesNV[curIdxOK + 2];
			gl_PrimitiveIndicesNV[curIdxOK + 4] = gl_PrimitiveIndicesNV[curIdxOK + 1];
			gl_PrimitiveIndicesNV[curIdxOK + 5] = gl_PrimitiveIndicesNV[curIdxOK + 2] + 1;


		}

		if (curSlot < sliceCnt - 1) {
			int i = curSlot;

			int curIdxOK = (3 * i * i) + i * 6;
			gl_PrimitiveIndicesNV[curIdxOK + 0] = (i + 1) * (i + 2) / 2 + i;
			gl_PrimitiveIndicesNV[curIdxOK + 1] = (i + 2) * (i + 3) / 2 - 1;
			gl_PrimitiveIndicesNV[curIdxOK + 2] = (i + 1) * (i + 2) / 2 - 1;

		}
	}


#else
	

	
	if (laneID < 3) {
		gl_PrimitiveCountNV = 1;

		vec3 v = i_Patch[id].vertices[laneID].xyz;


		/*gl_MeshVerticesNV[0].gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
		gl_MeshVerticesNV[1].gl_Position = vec4(1.0, 0.0, 0.0, 1.0);
		gl_MeshVerticesNV[2].gl_Position = vec4(0.0, 1.0, 0.0, 1.0);*/
		int vert = int(laneID);


		vec2 uv = vec2(vert & 1, vert >> 1 & 1);
		//vec3 finalVertex = berp(v, uv);
		vec3 finalVertex = v;

		int idx = vert;

		//gl_MeshVerticesNV[vert].gl_Position = vec4(uv, 0.0, 1.0);
		gl_MeshVerticesNV[idx].gl_Position = u_Transform.modelViewProjection * vec4(finalVertex, 1);

		gl_PrimitiveIndicesNV[idx] = idx;

		for (int i = 0; i < NUM_CLIPPING_PLANES; i++) {
			gl_MeshVerticesNV[idx].gl_ClipDistance[i] = 1.0;
		}

		OUT[idx].o_TexCoord = uv;

	}
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
#if SHADING_LOD
	vec3 c[3] = vec3[3](vec3(0.0, 0.25, 0.25),
		vec3(0.86, 0.00, 0.00),
		vec3(1.0, 0.50, 0.00));
	vec3 color = berp(c, i_TexCoord);
	o_FragColor = vec4(color, 1);
	//o_FragColor = vec4(i_TexCoord,0, 1);
#elif SHADING_DIFFUSE
	vec2 s = texture(u_SmapSampler, i_TexCoord).rg * u_DmapFactor;
	vec3 n = normalize(vec3(-s, 1));
	float d = clamp(n.z, 0.0, 1.0);

	o_FragColor = vec4(vec3(d / 3.14159), 1);

#elif SHADING_NORMALS
	vec2 s = texture(u_SmapSampler, i_TexCoord).rg * u_DmapFactor;
	vec3 n = normalize(vec3(-s, 1));

	o_FragColor = vec4(abs(n), 1);

#else
	o_FragColor = vec4(1, 0, 0, 1);
#endif

}

#endif
