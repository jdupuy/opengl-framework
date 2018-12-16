/* terrain_gs.glsl - public domain
    (created by Jonathan Dupuy and Cyril Crassin)

    This code has dependencies on the following GLSL sources:
    - fcull.glsl
    - isubd.glsl
    - terrain_common.glsl
*/

////////////////////////////////////////////////////////////////////////////////
// Implicit Subdivition Shader for Terrain Rendering (using a geometry shader)
//

layout (std430, binding = BUFFER_BINDING_SUBD1)
readonly buffer SubdBufferIn {
    uvec2 u_SubdBufferIn[];
};


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
 * Geometry Shader
 *
 * This geometry shader is responsible for updating the
 * subdivision buffer and sending visible geometry to the rasterizer.
 */
#ifdef GEOMETRY_SHADER
layout(points) in;
layout(triangle_strip, max_vertices = MAX_VERTICES) out;
layout(location = 0) out vec2 o_TexCoord;

void genVertex(in vec4 v[3], vec2 tessCoord, vec2 lodColor)
{
    vec4 finalVertex = berp(v, tessCoord);

#if FLAG_DISPLACE
    finalVertex.z+= dmap(finalVertex.xy);
#endif

#if SHADING_LOD
    o_TexCoord = lodColor;
#else
    o_TexCoord = finalVertex.xy * 0.5 + 0.5;
#endif
    gl_Position = u_Transform.modelViewProjection * finalVertex;
    EmitVertex();
}

void main()
{
    // get threadID (each key is associated to a thread)
    int threadID = gl_PrimitiveIDIn;

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

        int keyLod = findMSB(key);
        vec2 lodColor = intValToColor2(keyLod);

        /*
            The code below generates a tessellated triangle with a single triangle strip.
            The algorithm instances strips of 4 vertices, which produces 2 triangles.
            This is why there is a special case for subd_level == 0, where we expect
            only one triangle.
        */
#if PATCH_SUBD_LEVEL == 0
        genVertex(v, vec2(0, 0), lodColor);
        genVertex(v, vec2(1, 0), lodColor);
        genVertex(v, vec2(0, 1), lodColor);
        EndPrimitive();
#else
        int subdLevel = 2 * PATCH_SUBD_LEVEL - 1;
        int stripCnt = 1 << subdLevel;

        for (int i = 0; i < stripCnt; ++i) {
            uint key = i + stripCnt;
            vec4 vs[3];  subd(key, v, vs);

            genVertex(vs, vec2(0.0f, 1.0f), lodColor);
            genVertex(vs, vec2(0.0f, 0.0f), lodColor);
            genVertex(vs, vec2(0.5f, 0.5f), lodColor);
            genVertex(vs, vec2(1.0f, 0.0f), lodColor);
        }
        EndPrimitive();
#endif
    }

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
