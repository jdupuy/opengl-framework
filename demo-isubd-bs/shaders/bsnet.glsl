layout (std430, binding = BUFFER_BINDING_PATCH)
readonly buffer VertexBuffer {
    vec4 u_VertexBuffer[];
};

// -----------------------------------------------------------------------------
/**
 * Vertex Shader
 *
 */
#ifdef VERTEX_SHADER
void main()
{
}
#endif

// -----------------------------------------------------------------------------
/**
 * Geometry Shader
 *
 * The vertex shader is empty
 */
#ifdef GEOMETRY_SHADER
layout(points) in;
layout(triangle_strip, max_vertices = 12) out;

layout(location = 0) out vec4 o_Color;
void main()
{
    float pointSize = 1.0 / 20.0;
    int vertexID = gl_PrimitiveIDIn;
    vec4 vertexPos = u_VertexBuffer[vertexID];

    for (int i = 0; i < 4; ++i) {
        vec2 u = (vec2(i & 1, i >> 1 & 1) - 0.5) * pointSize;
        vec4 v = vertexPos + vec4(u, 0, 0);

        o_Color = vec4(1, 0.25, 0, 1);
        gl_Position = v;
        EmitVertex();
    }
    EndPrimitive();

    if (vertexID < 4) {
        float lineScale = 1.0 / 200.0;
        vec4 nextVertexPos = u_VertexBuffer[(vertexID + 1) % 4];
        vec2 dir = normalize(nextVertexPos.xy - vertexPos.xy);
        vec2 dirOrtho = vec2(-dir.y, +dir.x);
        vec4 p0 = vertexPos     + vec4(+dirOrtho, 0, 0) * lineScale;
        vec4 p1 = nextVertexPos + vec4(+dirOrtho, 0, 0) * lineScale;
        vec4 p2 = nextVertexPos + vec4(-dirOrtho, 0, 0) * lineScale;
        vec4 p3 = vertexPos     + vec4(-dirOrtho, 0, 0) * lineScale;

        for (int i = 0; i < 4; ++i) {
            vec2 u = vec2(i & 1, i >> 1 & 1);
            vec4 v = mix(mix(p0, p1, u.x), mix(p3, p2, u.x), u.y);

            o_Color = vec4(1, 1, 0, 1);
            gl_Position = v;
            EmitVertex();
        }
        EndPrimitive();
    }
}
#endif


// -----------------------------------------------------------------------------
/**
 * Fragment Shader
 *
 */
#ifdef FRAGMENT_SHADER
layout(location = 0) in vec4 i_Color;
layout(location = 0) out vec4 o_FragColor;
void main()
{
    o_FragColor = i_Color;
}
#endif
