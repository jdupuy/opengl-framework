// --------------------------------------------------
// Uniforms
// --------------------------------------------------
uniform vec3 u_ClearColor;
uniform float u_Fovy;
uniform sampler2D u_EnvmapSampler;

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

vec3 evalEnvmap(vec3 dir)
{
	float pi = 3.14159265359;
	float u1 = atan(dir.x, dir.y) / pi * 0.5 + 0.5;
	float u2 = 1.0 - acos(dir.z) / pi;
	return texture(u_EnvmapSampler, vec2(u1, u2)).rgb;
}

vec3 inv(vec3 x)
{
    return x / dot(x, x);
}

vec3 proj(vec3 x)
{
    const vec3 e0 = vec3(1, 0, 0);
    return 2.0 * inv(x + e0) - e0;
}

// --------------------------------------------------
// Vertex shader
// --------------------------------------------------
#ifdef VERTEX_SHADER
layout(location = 0) out vec3 o_TexCoord;
void main() {
	// draw a full screen quad in modelview space
	vec2 p = vec2(gl_VertexID & 1, gl_VertexID >> 1 & 1) * 2.0 - 1.0;
	vec3 e = vec3(-900.0, p * 1e5);

	gl_Position = u_Transform.projection * vec4(e, 1);
	gl_Position.z = 0.99999 * gl_Position.w; // make sure the cubemap is visible
	o_TexCoord = vec3(u_Transform.viewInv * vec4(normalize(e), 0));
}
#endif

// --------------------------------------------------
// Fragment shader
// --------------------------------------------------
#ifdef FRAGMENT_SHADER
layout(location = 0) in vec3 i_TexCoord;
layout(location = 0) out vec4 o_FragColor;

void main() {
    vec2 texCoord = gl_FragCoord.xy / vec2(1680, 1050);
    //texCoord = floor(texCoord * 256.0f) / 256.0f;
    texCoord = 2.0 * texCoord - 1.0;
    texCoord*= tan(u_Fovy / 2.0) * vec2(1680.0/1050.0, 1.0);

    vec3 w;
#if FLAG_FISHEYE
    w = proj(vec3(texCoord, 0.0).zxy);
#else
    w = normalize(vec3(texCoord * 2.0, 1).zxy);
#endif

    // to world
    w = normalize((u_Transform.viewInv * vec4(w, 0)).xyz);

    vec3 emap = evalEnvmap(w);
    o_FragColor = vec4(emap, 1);
}
#endif


