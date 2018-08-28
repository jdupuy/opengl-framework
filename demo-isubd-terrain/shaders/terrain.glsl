#line 1
////////////////////////////////////////////////////////////////////////////////
// Implicit Subdivition Sahder for Terrain Rendering
//
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

layout (std430, binding = BUFFER_BINDING_SUBD1)
readonly buffer SubdBuffer1 {
	uvec2 u_SubdBufferIn[];
};

layout (std430, binding = BUFFER_BINDING_SUBD2)
buffer SubdBuffer2 {
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

uniform sampler2D u_DmapSampler;
uniform float u_DmapFactor;
uniform float u_LodFactor;

// -----------------------------------------------------------------------------
/**
 * Vertex Shader
 *
 * The vertex shader is empty
 */
#ifdef VERTEX_SHADER
void main(void)
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
    vec3 vertices[3];
    flat uint key;
} o_Patch[];

void updateSubdBuffer(uint primID, uint key, int targetLod) {
    // extract subdivision level associated to the key
    int keyLod = findMSB(key);

    // update the key accordingly
    if (/* subdivide ? */ keyLod < targetLod && !isLeaf(key)) {
        uint children[2]; childrenKeys(key, children);
        uint idx1 = atomicCounterIncrement(u_SubdBufferCounter);
        uint idx2 = atomicCounterIncrement(u_SubdBufferCounter);

        u_SubdBufferOut[idx1] = uvec2(primID, children[0]);
        u_SubdBufferOut[idx2] = uvec2(primID, children[1]);
    } else if (/* merge ? */ keyLod > targetLod && !isRoot(key)) {
        if (isChildZero(key)) {
            uint idx = atomicCounterIncrement(u_SubdBufferCounter);

            u_SubdBufferOut[idx] = uvec2(primID, parentKey(key));
        }
    } else /* keep ? */ {
        uint idx = atomicCounterIncrement(u_SubdBufferCounter);

        u_SubdBufferOut[idx] = uvec2(primID, key);
    }
}

void main()
{
    // get threadID (each key is associated to a thread)
    int threadID = gl_PrimitiveID;

    // get coarse triangle associated to the key
    uint primID = u_SubdBufferIn[threadID].x;
    vec3 v_in[3] = vec3[3](
        vec3(u_VertexBuffer[u_IndexBuffer[primID * 3    ]].xyz),
        vec3(u_VertexBuffer[u_IndexBuffer[primID * 3 + 1]].xyz),
        vec3(u_VertexBuffer[u_IndexBuffer[primID * 3 + 2]].xyz)
    );

    // compute distance-based LOD
    uint key = u_SubdBufferIn[threadID].y;
    vec3 v[3]; subd(key, v_in, v);
    vec3 triangleCenter = (v[0] + v[1] + v[2]) / 3.0f;
    mat4 mv = u_Transform.modelView;
    vec3 triangleCenterMV = (mv * vec4(triangleCenter, 1)).xyz;
    float z = length(triangleCenterMV);
    int targetLod = int(distanceToLod(z, u_LodFactor));
    targetLod = 0;
    updateSubdBuffer(primID, key, targetLod);

    // set output data
    o_Patch[gl_InvocationID].vertices = v;
    o_Patch[gl_InvocationID].key = key;

    // set tess levels
    int tessLevel = PATCH_TESS_LEVEL;
    gl_TessLevelInner[0] = tessLevel;
    gl_TessLevelInner[1] = tessLevel;
    gl_TessLevelOuter[0] = tessLevel;
    gl_TessLevelOuter[1] = tessLevel;
    gl_TessLevelOuter[2] = tessLevel;
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
layout (triangles, ccw, equal_spacing) in;
in Patch {
    vec3 vertices[3];
    flat uint key;
} i_Patch[];

void main()
{
    vec3 v[3] = i_Patch[0].vertices;
    vec3 finalVertex = berp(v, gl_TessCoord.xy);

    gl_Position = u_Transform.modelViewProjection * vec4(finalVertex, 1);
}
#endif

// -----------------------------------------------------------------------------
/**
 * Fragment Shader
 *
 * This fragment shader is responsible for shading the final geometry.
 */
#ifdef FRAGMENT_SHADER
layout(location = 0) out vec4 o_FragColor;

void main()
{
    o_FragColor = vec4(0, 1, 0, 1);
}

#endif
