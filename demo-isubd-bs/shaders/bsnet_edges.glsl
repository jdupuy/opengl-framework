layout (std430, binding = BUFFER_BINDING_PATCH)
readonly buffer VertexBuffer {
    vec4 u_VertexBuffer[];
};

uniform vec2 u_MousePos;

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
layout(line_strip, max_vertices = 2) out;

void main()
{
    int primID = gl_PrimitiveIDIn;
    if (primID < 3) {
        gl_Position = u_VertexBuffer[primID];
        EmitVertex();
        gl_Position = u_VertexBuffer[primID + 1];
        EmitVertex();
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
layout(location = 0) out vec4 o_FragColor;

void main()
{
    vec3 myColor = vec3(0.00,0.20,0.70);

    o_FragColor = vec4(myColor, 1.0);
}
#endif
