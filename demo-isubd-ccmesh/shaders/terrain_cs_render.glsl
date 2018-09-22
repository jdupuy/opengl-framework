#line 1
////////////////////////////////////////////////////////////////////////////////
// Implicit Subdivition Sahder for Terrain Rendering
//

layout (std430, binding = BUFFER_BINDING_CULLED_SUBD)
buffer CulledSubdBuffer {
    uvec2 u_CulledSubdBuffer[];
};

layout (std430, binding = BUFFER_BINDING_GEOMETRY_VERTICES)
readonly buffer VertexBuffer {
    vec4 u_VertexBuffer[];
};

layout (std430, binding = BUFFER_BINDING_GEOMETRY_INDEXES)
readonly buffer IndexBuffer {
    uint u_IndexBuffer[];
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

uniform sampler2D u_DmapSampler;
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
        vec3(u_VertexBuffer[u_IndexBuffer[primID * 3    ]].xyz),
        vec3(u_VertexBuffer[u_IndexBuffer[primID * 3 + 1]].xyz),
        vec3(u_VertexBuffer[u_IndexBuffer[primID * 3 + 2]].xyz)
    );

    // compute sub-triangle associated to the key
    uint key = u_CulledSubdBuffer[threadID].y;
    vec3 v[3]; subd(key, v_in, v);

    // compute vertex location
    vec3 finalVertex = berp(v, i_TessCoord);
#if FLAG_DISPLACE
    finalVertex.z+= dmap(finalVertex.xy);
#endif

    o_TexCoord = i_TessCoord;
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
    vec3 c[3] = vec3[3](vec3(0.0,1.0,1.0)/4.0,
                        vec3(0.86,0.00,0.00),
                        vec3(1.0,0.50,0.00)/1.0);
    vec3 color = berp(c, i_TexCoord);

    o_FragColor = vec4(color, 1);
}
#endif

