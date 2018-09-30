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
#if 1
    const mat4 mcc = mat4(4, 1, 0, 0,
                          4, 6, 4, 1,
                          0, 1, 4, 6,
                          0, 0, 0, 1) / 8.0f;
#else
    const mat4 mcc = mat4( 0.686887 , 0.0833333, -0.0202201,  0.0226732,
                           0.3333333, 0.833333 ,  0.3333333, -0.106006,
                          -0.0202201, 0.0833333,  0.686887 ,  0.72978,
                           0        , 0        ,  0        , 0.353553);
//mcc = mcc*mcc;
#endif
    float b = float(bit);
    float c = 1.0f - b;
    mat4 m = mat4(c, 0, 0, b,
                  0, c, b, 0,
                  0, b, c, 0,
                  b, 0, 0, c);

    return mcc * m;
}

// get xform from key
void keyToXform(in uint key, out mat4 xfu, out mat4 xfv)
{
    xfu = xfv = mat4(1.0f);

    while (key > 1u) {
        xfu = bitToXform(key & 1u) * xfu;
        key = key >> 1u;
        xfv = bitToXform(key & 1u) * xfv;
        key = key >> 1u;
    }
}

// get xform from key
void
keyToXform(
    in uint key,
    out mat4 xfu, out mat4 xfv,
    out mat4 xfup, out mat4 xfvp
) {
    keyToXform(parentKey(key), xfup, xfvp);
    keyToXform(key, xfu, xfv);
}

// subdivision routine (vertex position only) with parents
void
subd(
    in uint key,
    in vec4 v_in[16],
    out vec4 v_out[4],
    out vec4 v_out_p[4],
    out vec4 v_tg[4],
    out vec4 v_bg[4]
) {
    mat4 xfu, xfv, xfup, xfvp; keyToXform(key, xfu, xfv, xfup, xfvp);
    mat4 x_in = mat4(v_in[0].x, v_in[4].x, v_in[ 8].x, v_in[12].x,
                     v_in[1].x, v_in[5].x, v_in[ 9].x, v_in[13].x,
                     v_in[2].x, v_in[6].x, v_in[10].x, v_in[14].x,
                     v_in[3].x, v_in[7].x, v_in[11].x, v_in[15].x);
    mat4 y_in = mat4(v_in[0].y, v_in[4].y, v_in[ 8].y, v_in[12].y,
                     v_in[1].y, v_in[5].y, v_in[ 9].y, v_in[13].y,
                     v_in[2].y, v_in[6].y, v_in[10].y, v_in[14].y,
                     v_in[3].y, v_in[7].y, v_in[11].y, v_in[15].y);
    mat4 z_in = mat4(v_in[0].z, v_in[4].z, v_in[ 8].z, v_in[12].z,
                     v_in[1].z, v_in[5].z, v_in[ 9].z, v_in[13].z,
                     v_in[2].z, v_in[6].z, v_in[10].z, v_in[14].z,
                     v_in[3].z, v_in[7].z, v_in[11].z, v_in[15].z);
    mat4 x_out = xfv * transpose(xfu * x_in);
    mat4 y_out = xfv * transpose(xfu * y_in);
    mat4 z_out = xfv * transpose(xfu * z_in);
    mat4 x_out_p = xfvp * transpose(xfup * x_in);
    mat4 y_out_p = xfvp * transpose(xfup * y_in);
    mat4 z_out_p = xfvp * transpose(xfup * z_in);

    v_out[0] = vec4(x_out[1][1], y_out[1][1], z_out[1][1], 1);
    v_out[1] = vec4(x_out[2][1], y_out[2][1], z_out[2][1], 1);
    v_out[2] = vec4(x_out[2][2], y_out[2][2], z_out[2][2], 1);
    v_out[3] = vec4(x_out[1][2], y_out[1][2], z_out[1][2], 1);

    v_out_p[0] = vec4(x_out_p[1][1], y_out_p[1][1], z_out_p[1][1], 1);
    v_out_p[1] = vec4(x_out_p[2][1], y_out_p[2][1], z_out_p[2][1], 1);
    v_out_p[2] = vec4(x_out_p[2][2], y_out_p[2][2], z_out_p[2][2], 1);
    v_out_p[3] = vec4(x_out_p[1][2], y_out_p[1][2], z_out_p[1][2], 1);

    // tangents
    v_tg[0] = (vec4(x_out[2][1], y_out[2][1], z_out[2][1], 1)
            - vec4(x_out[0][1], y_out[0][1], z_out[0][1], 1)) / 2.0;
    v_tg[1] = (vec4(x_out[3][1], y_out[3][1], z_out[3][1], 1)
            - vec4(x_out[1][1], y_out[1][1], z_out[1][1], 1)) / 2.0;
    v_tg[2] = (vec4(x_out[3][2], y_out[3][2], z_out[3][2], 1)
            - vec4(x_out[1][2], y_out[1][2], z_out[1][2], 1)) / 2.0;
    v_tg[3] = (vec4(x_out[2][2], y_out[2][2], z_out[2][2], 1)
            - vec4(x_out[0][2], y_out[0][2], z_out[0][2], 1)) / 2.0;

    // bitangents
    float s2 = sign(determinant(xfv));
    v_bg[0] =(vec4(x_out[1][2], y_out[1][2], z_out[1][2], 1)
            - vec4(x_out[1][0], y_out[1][0], z_out[1][0], 1)) / 2.0;
    v_bg[1] =(vec4(x_out[2][2], y_out[2][2], z_out[2][2], 1)
            - vec4(x_out[2][0], y_out[2][0], z_out[2][0], 1)) / 2.0;
    v_bg[2] =(vec4(x_out[2][3], y_out[2][3], z_out[2][3], 1)
            - vec4(x_out[2][1], y_out[2][1], z_out[2][1], 1)) / 2.0;
    v_bg[3] =(vec4(x_out[1][3], y_out[1][3], z_out[1][3], 1)
            - vec4(x_out[1][1], y_out[1][1], z_out[1][1], 1)) / 2.0;
}
