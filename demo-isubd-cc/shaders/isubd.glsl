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
struct ccpatch {
    vec4 v[4];
    vec4 tg[4];
    vec4 bg[4];
    vec2 uv[4];
};

void
subd(
    in uint key,
    in vec4 c_in[16],
    out ccpatch p
) {
    mat4 xfu, xfv; keyToXform(key, xfu, xfv);
    mat4 x_in = mat4(c_in[0].x, c_in[4].x, c_in[ 8].x, c_in[12].x,
                     c_in[1].x, c_in[5].x, c_in[ 9].x, c_in[13].x,
                     c_in[2].x, c_in[6].x, c_in[10].x, c_in[14].x,
                     c_in[3].x, c_in[7].x, c_in[11].x, c_in[15].x);
    mat4 y_in = mat4(c_in[0].y, c_in[4].y, c_in[ 8].y, c_in[12].y,
                     c_in[1].y, c_in[5].y, c_in[ 9].y, c_in[13].y,
                     c_in[2].y, c_in[6].y, c_in[10].y, c_in[14].y,
                     c_in[3].y, c_in[7].y, c_in[11].y, c_in[15].y);
    mat4 z_in = mat4(c_in[0].z, c_in[4].z, c_in[ 8].z, c_in[12].z,
                     c_in[1].z, c_in[5].z, c_in[ 9].z, c_in[13].z,
                     c_in[2].z, c_in[6].z, c_in[10].z, c_in[14].z,
                     c_in[3].z, c_in[7].z, c_in[11].z, c_in[15].z);
    mat4 u_in = mat4(-1, -1, -1, -1,
                      0,  0,  0,  0,
                      1,  1,  1,  1,
                      2,  2,  2,  2);
    mat4 v_in = mat4(-1, 0, 1, 2,
                     -1, 0, 1, 2,
                     -1, 0, 1, 2,
                     -1, 0, 1, 2);
    mat4 x_out = xfv * transpose(xfu * x_in);
    mat4 y_out = xfv * transpose(xfu * y_in);
    mat4 z_out = xfv * transpose(xfu * z_in);
    mat4 u_out = xfv * transpose(xfu * u_in);
    mat4 v_out = xfv * transpose(xfu * v_in);

#define v00 vec4(x_out[0][0], y_out[0][0], z_out[0][0], 1)
#define v01 vec4(x_out[1][0], y_out[1][0], z_out[1][0], 1)
#define v02 vec4(x_out[2][0], y_out[2][0], z_out[2][0], 1)
#define v03 vec4(x_out[3][0], y_out[3][0], z_out[3][0], 1)
#define v10 vec4(x_out[0][1], y_out[0][1], z_out[0][1], 1)
#define v11 vec4(x_out[1][1], y_out[1][1], z_out[1][1], 1)
#define v12 vec4(x_out[2][1], y_out[2][1], z_out[2][1], 1)
#define v13 vec4(x_out[3][1], y_out[3][1], z_out[3][1], 1)
#define v20 vec4(x_out[0][2], y_out[0][2], z_out[0][2], 1)
#define v21 vec4(x_out[1][2], y_out[1][2], z_out[1][2], 1)
#define v22 vec4(x_out[2][2], y_out[2][2], z_out[2][2], 1)
#define v23 vec4(x_out[3][2], y_out[3][2], z_out[3][2], 1)
#define v30 vec4(x_out[0][3], y_out[0][3], z_out[0][3], 1)
#define v31 vec4(x_out[1][3], y_out[1][3], z_out[1][3], 1)
#define v32 vec4(x_out[2][3], y_out[2][3], z_out[2][3], 1)
#define v33 vec4(x_out[3][3], y_out[3][3], z_out[3][3], 1)

#define uv01 vec2(u_out[1][0], v_out[1][0])
#define uv00 vec2(u_out[0][0], v_out[0][0])
#define uv02 vec2(u_out[2][0], v_out[2][0])
#define uv03 vec2(u_out[3][0], v_out[3][0])
#define uv10 vec2(u_out[0][1], v_out[0][1])
#define uv11 vec2(u_out[1][1], v_out[1][1])
#define uv12 vec2(u_out[2][1], v_out[2][1])
#define uv13 vec2(u_out[3][1], v_out[3][1])
#define uv20 vec2(u_out[0][2], v_out[0][2])
#define uv21 vec2(u_out[1][2], v_out[1][2])
#define uv22 vec2(u_out[2][2], v_out[2][2])
#define uv23 vec2(u_out[3][2], v_out[3][2])
#define uv30 vec2(u_out[0][3], v_out[0][3])
#define uv31 vec2(u_out[1][3], v_out[1][3])
#define uv32 vec2(u_out[2][3], v_out[2][3])
#define uv33 vec2(u_out[3][3], v_out[3][3])

    // vertex positions
    p.v[0] = v11;
    p.v[1] = v12;
    p.v[2] = v22;
    p.v[3] = v21;

    // parametric coords
    p.uv[0] = uv11;
    p.uv[1] = uv12;
    p.uv[2] = uv22;
    p.uv[3] = uv21;

    // C1 tangents
    p.tg[0] = (v21 - v01) / (uv21.x - uv01.x);
    p.tg[1] = (v22 - v02) / (uv22.x - uv02.x);
    p.tg[2] = (v32 - v12) / (uv32.x - uv12.x);
    p.tg[3] = (v31 - v11) / (uv31.x - uv11.x);

    // C1 bitangents
    p.bg[0] = (v12 - v10) / (uv12.y - uv01.y);
    p.bg[1] = (v13 - v11) / (uv13.y - uv11.y);
    p.bg[2] = (v23 - v21) / (uv23.y - uv21.y);
    p.bg[3] = (v22 - v20) / (uv22.y - uv20.y);

#undef uv00
#undef uv01
#undef uv02
#undef uv03
#undef uv10
#undef uv11
#undef uv12
#undef uv13
#undef uv20
#undef uv21
#undef uv22
#undef uv23
#undef uv30
#undef uv31
#undef uv32
#undef uv33
#undef v00
#undef v01
#undef v02
#undef v03
#undef v10
#undef v11
#undef v12
#undef v13
#undef v20
#undef v21
#undef v22
#undef v23
#undef v30
#undef v31
#undef v32
#undef v33
}
