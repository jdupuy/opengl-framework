#line 2
//The rest of the code is inside those headers which are included by the C-code:
//Include isubd.glsl
//Include terrain_common.glsl

////////////////////////////////////////////////////////////////////////////////
// Implicit Subdivition Sahder for Terrain Rendering
//

layout (std430, binding = BUFFER_BINDING_SUBD1)
readonly buffer SubdBufferIn {
    uvec2 u_SubdBufferIn[];
};


layout (std430, binding = BUFFER_BINDING_GEOMETRY_VERTICES)
readonly buffer VertexBuffer {
    vec4 u_VertexBuffer[];
};

layout (std430, binding = BUFFER_BINDING_GEOMETRY_INDEXES)
readonly buffer IndexBuffer {
    uint u_IndexBuffer[];
};


// -----------------------------------------------------------------------------
/**
 * Vertex Shader
 *
 * The vertex shader is empty
 */
#ifdef VERTEX_SHADER
void main()
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
    vec4 vertices[3];
    flat uint key;
} o_Patch[];



void main()
{
    // get threadID (each key is associated to a thread)
    int threadID = gl_PrimitiveID;

    // get coarse triangle associated to the key
    uint primID = u_SubdBufferIn[threadID].x;
    vec4 v_in[3] = vec4[3](
        u_VertexBuffer[u_IndexBuffer[primID * 3    ]],
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

    if (/* is visible ? */frustumCullingTest(mvp, bmin.xyz, bmax.xyz)) {
#else
    if (true) {
#endif // FLAG_CULL
        // set tess levels
        //int tessLevel = PATCH_TESS_LEVEL-1;

# if PATCH_SUBD_LEVEL == 2
		gl_TessLevelInner[0] = PATCH_TESS_LEVEL+0;
		gl_TessLevelInner[1] = 0;
		gl_TessLevelOuter[0] = PATCH_TESS_LEVEL-2;
		gl_TessLevelOuter[1] = PATCH_TESS_LEVEL-2;
        gl_TessLevelOuter[2] = PATCH_TESS_LEVEL-2;
# elif PATCH_SUBD_LEVEL == 3
		gl_TessLevelInner[0] = PATCH_TESS_LEVEL - 2;
		gl_TessLevelInner[1] = 0;
		gl_TessLevelOuter[0] = PATCH_TESS_LEVEL - 0;
		gl_TessLevelOuter[1] = PATCH_TESS_LEVEL - 0;
		gl_TessLevelOuter[2] = PATCH_TESS_LEVEL - 0;
# else
		gl_TessLevelInner[0] = PATCH_TESS_LEVEL;
		gl_TessLevelInner[1] = 0;
		gl_TessLevelOuter[0] = PATCH_TESS_LEVEL;
		gl_TessLevelOuter[1] = PATCH_TESS_LEVEL;
		gl_TessLevelOuter[2] = PATCH_TESS_LEVEL;
# endif

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
    vec4 vertices[3];
    flat uint key;
} i_Patch[];

layout(location = 0) out vec2 o_TexCoord;

void main()
{
    vec4 v[3] = i_Patch[0].vertices;
    vec4 finalVertex = berp(v, gl_TessCoord.xy);

#if FLAG_DISPLACE
    finalVertex.z+= dmap(finalVertex.xy);
#endif

#if SHADING_LOD
    o_TexCoord = gl_TessCoord.xy;
#else
    o_TexCoord = finalVertex.xy * 0.5 + 0.5;
#endif

    gl_Position = u_Transform.modelViewProjection * finalVertex;
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
#if SHADING_LOD
    vec3 c[3] = vec3[3](vec3(0.0,0.25,0.25),
                        vec3(0.86,0.00,0.00),
                        vec3(1.0,0.50,0.00));
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
