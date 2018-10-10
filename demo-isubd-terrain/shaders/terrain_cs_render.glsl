#line 2
//The rest of the code is inside those headers which are included by the C-code:
//Include isubd.glsl
//Include terrain_common.glsl

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
    vec3 v_in[3] = vec3[3](
        u_VertexBuffer[u_IndexBuffer[primID * 3    ]].xyz,
        u_VertexBuffer[u_IndexBuffer[primID * 3 + 1]].xyz,
        u_VertexBuffer[u_IndexBuffer[primID * 3 + 2]].xyz
    );

    // compute sub-triangle associated to the key
    uint key = u_CulledSubdBuffer[threadID].y;
    vec3 v[3]; subd(key, v_in, v);

    // compute vertex location
    vec3 finalVertex = berp(v, i_TessCoord);
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

    gl_Position = u_Transform.modelViewProjection * vec4(finalVertex, 1.0);
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

