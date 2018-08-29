#line 1
////////////////////////////////////////////////////////////////////////////////
// Implicit Subdivition Sahder for Terrain Rendering
//

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

uniform sampler2D u_DmapSampler;
uniform float u_DmapFactor;
uniform float u_LodFactor;

float dmap(vec2 pos)
{
    return cos(20.0 * pos.x) * cos(20.0 * pos.y) / 2.0 * u_DmapFactor;
}

float computeLod(vec3 c)
{
#if FLAG_DISPLACE
    c.z+= dmap(u_Transform.viewInv[3].xy);
#endif

    vec3 cxf = (u_Transform.modelView * vec4(c, 1)).xyz;
    float z = length(cxf);

    return distanceToLod(z, u_LodFactor);
}

float computeLod(in vec3 v[3])
{
#if 0
    vec3 c = (v[0] + v[1] + v[2]) / 3.0f;
#else
    // crack-free !!!
    vec3 c = (v[1] + v[2]) / 2.0;
#endif
    return computeLod(c);
}

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

void updateSubdBuffer(uint primID, uint key, int targetLod, int parentLod) {
    // extract subdivision level associated to the key
    int keyLod = findMSB(key);

    // update the key accordingly
    if (/* subdivide ? */ keyLod < targetLod && !isLeafKey(key)) {
        uint children[2]; childrenKeys(key, children);
        uint idx1 = atomicCounterIncrement(u_SubdBufferCounter);
        uint idx2 = atomicCounterIncrement(u_SubdBufferCounter);

        u_SubdBufferOut[idx1] = uvec2(primID, children[0]);
        u_SubdBufferOut[idx2] = uvec2(primID, children[1]);
    } else if (/* keep ? */ keyLod <= parentLod + 1) {
        uint idx = atomicCounterIncrement(u_SubdBufferCounter);

        u_SubdBufferOut[idx] = uvec2(primID, key);
    } else /* merge ? */ {
        if (isRootKey(key)) {
            uint idx = atomicCounterIncrement(u_SubdBufferCounter);

            u_SubdBufferOut[idx] = uvec2(primID, key);
        } else if (isChildZeroKey(key)) {
            uint idx = atomicCounterIncrement(u_SubdBufferCounter);

            u_SubdBufferOut[idx] = uvec2(primID, parentKey(key));
        }
    }
#if 0
    else /* keep ? */ {
        uint idx = atomicCounterIncrement(u_SubdBufferCounter);

        u_SubdBufferOut[idx] = uvec2(primID, key);
    }
#endif
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
    vec3 v[3], vp[3]; subd(key, v_in, v, vp);
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
    bmin.z = -u_DmapFactor / 2.0;
    bmax.z = +u_DmapFactor / 2.0;
#   endif

    if (/* is visible ? */dj_culltest(mvp, bmin, bmax)) {
#else
    if (true) {
#endif // FLAG_CULL
        // set tess levels
        int tessLevel = PATCH_TESS_LEVEL;
        gl_TessLevelInner[0] =
        gl_TessLevelInner[1] =
        gl_TessLevelOuter[0] =
        gl_TessLevelOuter[1] =
        gl_TessLevelOuter[2] = tessLevel;

        // set output data
        o_Patch[gl_InvocationID].vertices = v;
        o_Patch[gl_InvocationID].key = key;
    } else /* is not visible ? */ {
        // cull the geometry
        gl_TessLevelInner[0] =
        gl_TessLevelInner[1] =
        gl_TessLevelOuter[0] =
        gl_TessLevelOuter[1] =
        gl_TessLevelOuter[2] = 0;
    }
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

layout(location = 0) out vec2 o_TexCoord;

void main()
{
    vec3 v[3] = i_Patch[0].vertices;
    vec3 finalVertex = berp(v, gl_TessCoord.xy);

#if FLAG_DISPLACE
    finalVertex.z+= dmap(finalVertex.xy);
#endif

    o_TexCoord = gl_TessCoord.xy;
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
layout(location = 0) in vec2 i_TexCoord;
layout(location = 0) out vec4 o_FragColor;

void main()
{
    o_FragColor = vec4(i_TexCoord, 0, 1);
    o_FragColor = vec4(1);
}

#endif
