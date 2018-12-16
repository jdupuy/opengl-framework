/* terrain_cs_render.glsl - public domain
    (created by Jonathan Dupuy and Cyril Crassin)

    This code has dependencies on the following GLSL sources:
    - isubd.glsl
    - terrain_common.glsl
*/

////////////////////////////////////////////////////////////////////////////////
// Implicit Subdivition Shader for Terrain Rendering
//

layout (std430, binding = BUFFER_BINDING_CULLED_SUBD)
buffer CulledSubdBuffer {
    uvec2 u_CulledSubdBuffer[];
};


// -----------------------------------------------------------------------------
/**
 * Compute LoD Shader
 *
 * This compute shader is responsible for updating the subdivision
 * buffer and visible buffer that will be sent to the rasterizer.
 */
#ifdef VERTEX_SHADER
layout(location = 0) in vec2 i_TessCoord;
layout(location = 0) out vec2 o_TexCoord;

void main()
{
    // get threadID (each key is associated to a thread)
    int threadID = gl_InstanceID;

    // get coarse triangle associated to the key
    uint primID = u_CulledSubdBuffer[threadID].x;
    vec4 v_in[3] = vec4[3](
        u_VertexBuffer[u_IndexBuffer[primID * 3    ]],
        u_VertexBuffer[u_IndexBuffer[primID * 3 + 1]],
        u_VertexBuffer[u_IndexBuffer[primID * 3 + 2]]
    );

    // compute sub-triangle associated to the key
    uint key = u_CulledSubdBuffer[threadID].y;
    vec4 v[3]; subd(key, v_in, v);

    // compute vertex location
    vec4 finalVertex = berp(v, i_TessCoord);
#if FLAG_DISPLACE
    finalVertex.z+= dmap(finalVertex.xy);
#endif

#if SHADING_LOD
    //o_TexCoord = i_TessCoord.xy;
    int keyLod = findMSB(key);
    o_TexCoord = intValToColor2(keyLod);
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
    o_FragColor = shadeFragment(i_TexCoord);
}
#endif

