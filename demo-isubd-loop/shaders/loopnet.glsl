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
void main()
{
    float pointSize = 1.0 / 20.0;
    mat4 modelView = u_Transform.modelView;
    int vertexID = gl_PrimitiveIDIn;
    vec4 vertexPos = modelView * u_VertexBuffer[vertexID];

    for (int i = 0; i < 4; ++i) {
        vec2 u = (vec2(i & 1, i >> 1 & 1) - 0.5) * pointSize;
        vec4 v = vertexPos + vec4(0, u, 0);
        gl_Position = u_Transform.projection * v;
        EmitVertex();
    }
    EndPrimitive();

#if 0
    int row = vertexID % 4;
    if (row < 3) {
        float lineScale = 1.0 / 60.0;
        vec4 nextVertexPos = modelView * u_VertexBuffer[vertexID + 1];
        vec4 p0 = vertexPos     + vec4(0, 0, -0.5, 0) * lineScale;
        vec4 p1 = nextVertexPos + vec4(0, 0, -0.5, 0) * lineScale;
        vec4 p2 = nextVertexPos + vec4(0, 0, +0.5, 0) * lineScale;
        vec4 p3 = vertexPos     + vec4(0, 0, +0.5, 0) * lineScale;

        for (int i = 0; i < 4; ++i) {
            vec2 u = vec2(i & 1, i >> 1 & 1);
            vec4 v = mix(mix(p0, p1, u.x), mix(p3, p2, u.x), u.y);

            gl_Position = u_Transform.projection * v;
            EmitVertex();
        }
        EndPrimitive();
    }

    int col = vertexID / 4;
    if (col < 3) {
        float lineScale = 1.0 / 60.0;
        vec4 nextVertexPos = modelView * u_VertexBuffer[vertexID + 4];
        vec4 p0 = vertexPos     + vec4(0, 0, -0.5, 0) * lineScale;
        vec4 p1 = nextVertexPos + vec4(0, 0, -0.5, 0) * lineScale;
        vec4 p2 = nextVertexPos + vec4(0, 0, +0.5, 0) * lineScale;
        vec4 p3 = vertexPos     + vec4(0, 0, +0.5, 0) * lineScale;

        for (int i = 0; i < 4; ++i) {
            vec2 u = vec2(i & 1, i >> 1 & 1);
            vec4 v = mix(mix(p0, p1, u.x), mix(p3, p2, u.x), u.y);

            gl_Position = u_Transform.projection * v;
            EmitVertex();
        }
        EndPrimitive();
    }
#endif
}
#endif


// -----------------------------------------------------------------------------
/**
 * Fragment Shader
 *
 */
#ifdef FRAGMENT_SHADER
layout(location = 0) out vec4 o_FragColor;
void main()
{
    o_FragColor = vec4(1, 1, 0, 1);
}
#endif
