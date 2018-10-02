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
uniform sampler2D u_SmapSampler; // slope map
uniform float u_DmapFactor;
uniform float u_LodFactor;

// displacement map
float dmap(vec2 pos)
{
    return (texture(u_DmapSampler, pos * 0.5 + 0.5).x) * u_DmapFactor;
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
    o_TexCoord = i_TessCoord.xy;
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

