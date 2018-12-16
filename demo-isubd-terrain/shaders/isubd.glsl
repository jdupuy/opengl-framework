/* isubd.glsl - public domain implicit subdivision on the GPU
    (created by Jonathan Dupuy)
*/
uint parentKey(in uint key)
{
    return (key >> 1u);
}

void childrenKeys(in uint key, out uint children[2])
{
    children[0] = (key << 1u) | 0u;
    children[1] = (key << 1u) | 1u;
}

bool isRootKey(in uint key)
{
    return (key == 1u);
}

bool isLeafKey(in uint key)
{
    return findMSB(key) == 31;
}

bool isChildZeroKey(in uint key)
{
    return ((key & 1u) == 0u);
}

// barycentric interpolation
vec3 berp(in vec3 v[3], in vec2 u)
{
    return v[0] + u.x * (v[1] - v[0]) + u.y * (v[2] - v[0]);
}
vec4 berp(in vec4 v[3], in vec2 u)
{
    return v[0] + u.x * (v[1] - v[0]) + u.y * (v[2] - v[0]);
}

// get xform from bit value
mat3 bitToXform(in uint bit)
{
    float b = float(bit);
    float c = 1.0f - b;
    vec3 c1 = vec3(0.0f, c   , b   );
    vec3 c2 = vec3(0.5f, b   , 0.0f);
    vec3 c3 = vec3(0.5f, 0.0f, c   );

    return mat3(c1, c2, c3);
}

// get xform from key
mat3 keyToXform(in uint key)
{
    mat3 xf = mat3(1.0f);

    while (key > 1u) {
        xf*= bitToXform(key & 1u);
        key = key >> 1u;
    }

    return xf;
}

// get xform from key as well as xform from parent key
mat3 keyToXform(in uint key, out mat3 xfp)
{
    // TODO: optimize
    xfp = keyToXform(parentKey(key));
    return keyToXform(key);
}

// subdivision routine (vertex position only)
void subd(in uint key, in vec4 v_in[3], out vec4 v_out[3])
{
    mat3 xf = keyToXform(key);
    mat4x3 v = xf * transpose(mat3x4(v_in[0], v_in[1], v_in[2]));

    v_out[0] = vec4(v[0][0], v[1][0], v[2][0], v[3][0]);
    v_out[1] = vec4(v[0][1], v[1][1], v[2][1], v[3][1]);
    v_out[2] = vec4(v[0][2], v[1][2], v[2][2], v[3][2]);
}

// subdivision routine (vertex position only)
// also computes parent position
void subd(in uint key, in vec4 v_in[3], out vec4 v_out[3], out vec4 v_out_p[3])
{
    mat3 xfp; mat3 xf = keyToXform(key, xfp);
    mat4x3 v = xf * transpose(mat3x4(v_in[0], v_in[1], v_in[2]));
    mat4x3 vp = xfp * transpose(mat3x4(v_in[0], v_in[1], v_in[2]));

    v_out[0] = vec4(v[0][0], v[1][0], v[2][0], v[3][0]);
    v_out[1] = vec4(v[0][1], v[1][1], v[2][1], v[3][1]);
    v_out[2] = vec4(v[0][2], v[1][2], v[2][2], v[3][2]);

    v_out_p[0] = vec4(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
    v_out_p[1] = vec4(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
    v_out_p[2] = vec4(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
}
