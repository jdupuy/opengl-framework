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
layout(triangle_strip, max_vertices = 12) out;

layout(location = 0) out vec2 o_TexCoord;

void main()
{
    vec2 screenRes = vec2(SCREEN_XRES, SCREEN_YRES);
    float pointSize = SCREEN_XRES / 32.0;
    int vertexID = gl_PrimitiveIDIn;
    vec4 vertexPos = u_VertexBuffer[vertexID];

    for (int i = 0; i < 4; ++i) {
        vec2 uv = vec2(i & 1, i >> 1 & 1);
        vec2 offset = (uv - 0.5) * pointSize / screenRes;
        vec4 pos = vertexPos + vec4(offset, 0, 0);

        o_TexCoord = uv;
        gl_Position = pos;
        EmitVertex();
    }
    EndPrimitive();
}
#endif


// -----------------------------------------------------------------------------
/**
 * Fragment Shader
 *
 */
#ifdef FRAGMENT_SHADER
layout(location = 0) in vec2 i_TexCoord;
layout(location = 0) out vec4 o_FragColor;

float sqr(float x) {return x*x;}

void main()
{
    vec2 uv = (i_TexCoord - 0.5);
    float rTest = dot(uv, uv) - sqr(0.35);
    float alpha = 1.0 - sqr(smoothstep(0.00, 0.07, rTest));
    float inner = sqr(smoothstep(0.00, 0.07, -rTest));
    vec3 myColor = vec3(0.00, 0.20, 0.70);
    vec3 myInnerColor = vec3(220./255.);
    vec3 color = mix(myColor, myColor, inner);

    o_FragColor = vec4(color, alpha);
}
#endif
