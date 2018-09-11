#line 1

uint parentKey(in uint key)
{
    return (key >> 2u);
}

void childrenKeys(in uint key, out uint children[4])
{
    children[0] = (key << 2u) | 0u;
    children[1] = (key << 2u) | 1u;
    children[2] = (key << 2u) | 2u;
    children[3] = (key << 2u) | 3u;
}

bool isRootKey(in uint key)
{
    return (key == 1u);
}

bool isLeafKey(in uint key)
{
    return findMSB(key) == 13;
}

bool isChildZeroKey(in uint key)
{
    return ((key & 3u) == 0u);
}

// get xform from bit value
mat4 bitToXform(in uint bit)
{
    const mat4 mcc = mat4(4, 1, 0, 0,
                          4, 6, 4, 1,
                          0, 1, 4, 6,
                          0, 0, 0, 1) / 8.0f;
    float b = float(bit);
    float c = 1.0f - b;
    mat4 m = mat4(c, 0, 0, b,
                  0, c, b, 0,
                  0, b, c, 0,
                  b, 0, 0, c);

    return mcc * m;
}

// get xform from key
mat4 keyToXform(in uint key)
{
    mat4 xf = mat4(1.0f);

    while (key > 1u) {
        xf = bitToXform(key & 1u) * xf;
        key = key >> 1u;
    }

    return xf;
}

// get xform from key
mat4 keyToXform(in uint key, out mat4 xfp)
{
    xfp = keyToXform(parentKey(key));
    return keyToXform(key);
}

// subdivision routine (vertex position only)
void subd(in uint key, in vec4 v_in[16], out vec4 v_out[4])
{
    mat4 xf = keyToXform(key);
    vec4 x_in = vec4(v_in[0].x, v_in[1].x, v_in[2].x, v_in[3].x);
    vec4 y_in = vec4(v_in[0].y, v_in[1].y, v_in[2].y, v_in[3].y);
    vec4 z_in = vec4(v_in[0].z, v_in[1].z, v_in[2].z, v_in[3].z);
    vec4 x_out = xf * x_in;
    vec4 y_out = xf * y_in;
    vec4 z_out = xf * z_in;

    v_out[0] = vec4(x_out[0], y_out[0], z_out[0], 1);
    v_out[1] = vec4(x_out[1], y_out[1], z_out[1], 1);
    v_out[2] = vec4(x_out[2], y_out[2], z_out[2], 1);
    v_out[3] = vec4(x_out[3], y_out[3], z_out[3], 1);
}

// subdivision routine (vertex position only) with parents
void
subd(in uint key, in vec4 v_in[4], out vec4 v_out[4], out vec4 v_out_p[4])
{
    mat4 xfp; mat4 xf = keyToXform(key, xfp);
    vec4 x_in = vec4(v_in[0].x, v_in[1].x, v_in[2].x, v_in[3].x);
    vec4 y_in = vec4(v_in[0].y, v_in[1].y, v_in[2].y, v_in[3].y);
    vec4 z_in = vec4(v_in[0].z, v_in[1].z, v_in[2].z, v_in[3].z);
    vec4 x_out = xf * x_in;
    vec4 y_out = xf * y_in;
    vec4 z_out = xf * z_in;
    vec4 x_out_p = xfp * x_in;
    vec4 y_out_p = xfp * y_in;
    vec4 z_out_p = xfp * z_in;

    v_out[0] = vec4(x_out[0], y_out[0], z_out[0], 1);
    v_out[1] = vec4(x_out[1], y_out[1], z_out[1], 1);
    v_out[2] = vec4(x_out[2], y_out[2], z_out[2], 1);
    v_out[3] = vec4(x_out[3], y_out[3], z_out[3], 1);

    v_out_p[0] = vec4(x_out_p[0], y_out_p[0], z_out_p[0], 1);
    v_out_p[1] = vec4(x_out_p[1], y_out_p[1], z_out_p[1], 1);
    v_out_p[2] = vec4(x_out_p[2], y_out_p[2], z_out_p[2], 1);
    v_out_p[3] = vec4(x_out_p[3], y_out_p[3], z_out_p[3], 1);
}
