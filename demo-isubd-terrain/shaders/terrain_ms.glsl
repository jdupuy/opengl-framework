#line 2

#define PATCH_TESS_LEVEL_LOG	3

#if 0
# define MESH_VERTEX_COUNT			126
# define MESH_PRIMITIVE_COUNT		126
#elif PATCH_TESS_LEVEL_LOG==2
# define MESH_VERTEX_COUNT			15
# define MESH_PRIMITIVE_COUNT		16
#elif PATCH_TESS_LEVEL_LOG==3
# define MESH_VERTEX_COUNT			45
# define MESH_PRIMITIVE_COUNT		64
#endif


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
	uint u_IndexBufferInstanced[];
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

uniform sampler2D u_DmapSampler; // displacement map
uniform sampler2D u_SmapSampler; // slope map
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
} o_Patch[COMPUTE_THREAD_COUNT];

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

    // get threadID (each key is associated to a thread)
    uint threadID = gl_GlobalInvocationID.x;

	bool isVisible = true;
	
	uint key; vec4 v[3];

    // early abort if the threadID exceeds the size of the subdivision buffer
	if (threadID >= atomicCounter(u_PreviousSubdBufferCounter)) {
		gl_TaskCountNV = 0;	//Removes last processed triangle

		isVisible = false;
		//return;
	} else {

		// get coarse triangle associated to the key
		//CC: Why loading that from VRAM ??
		uint primID = u_SubdBufferIn[threadID].x;
		vec4 v_in[3] = vec4[3](
			u_VertexBuffer[u_IndexBuffer[primID * 3]],
			u_VertexBuffer[u_IndexBuffer[primID * 3 + 1]],
			u_VertexBuffer[u_IndexBuffer[primID * 3 + 2]]
			);

		// compute distance-based LOD
		key = u_SubdBufferIn[threadID].y;
		vec4 vp[3]; subd(key, v_in, v, vp);
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

		isVisible = frustumCullingTest(mvp, bmin.xyz, bmax.xyz);
#endif // FLAG_CULL



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
        o_Patch[idxOffset].vertices = v;
        o_Patch[idxOffset].key = key;

       

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
#line 1

const int gpuSubd = PATCH_TESS_LEVEL_LOG;


layout(local_size_x = COMPUTE_THREAD_COUNT) in;
layout(max_vertices = MESH_VERTEX_COUNT, max_primitives = MESH_PRIMITIVE_COUNT) out;
layout(triangles) out;

taskNV in Patch {
    vec4 vertices[3];
    uint key;
} i_Patch[COMPUTE_THREAD_COUNT];

layout(location = 0) out Interpolants{
    vec2 o_TexCoord;
} OUT[MESH_VERTEX_COUNT];  //COMPUTE_THREAD_COUNT

void main()
{
#line 242
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

	vec3 v[3] = vec3[3](
		i_Patch[id].vertices[0].xyz,
		i_Patch[id].vertices[1].xyz,
		i_Patch[id].vertices[2].xyz
		);
	uint key = i_Patch[id].key;

	gl_PrimitiveCountNV = (3 << (gpuSubd * 2)) / 3;

	int sliceCnt = (1 << gpuSubd) + 1;     // side vertices;
	int vertexCnt = (sliceCnt * (sliceCnt + 1)) / 2;
	int indexCnt = 3 << (gpuSubd * 2);

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
			vec2 tessCoord = instancedBaryCoords;
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
