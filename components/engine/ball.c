#include "ball.h"

#include <math.h>
#include <stdbool.h>

#include "gfx.h"
#include "room.h"

#define D (2 * BALL_R)

/* Per pixel of the disc, computed once (no FPU on the C6: the per-frame path is integer only). */
static struct {
    int8_t nx, ny, nz; /* sphere normal, Q7 */
    uint8_t alpha;     /* front skin opacity, /16: thicker towards the rim */
    uint8_t spec;      /* highlight, /16 */
    uint8_t shade;     /* diffuse level 0..3 */
} s_px[D][D];
static bool s_ready;
static uint16_t s_pal[2][4], s_white, s_rim; /* [blue, green][shade] */

static uint32_t scale_rgb(uint32_t c, float k)
{
    return (uint32_t)(((c >> 16) & 0xFF) * k) << 16 | (uint32_t)(((c >> 8) & 0xFF) * k) << 8 | (uint32_t)((c & 0xFF) * k);
}

static void init(void)
{
    const float lx = -0.45f, ly = -0.6f, lz = 0.66f; /* light from the upper left */
    const float hn = sqrtf(lx * lx + ly * ly + (lz + 1) * (lz + 1)), hx = lx / hn, hy = ly / hn, hz = (lz + 1) / hn;
    for (int i = 0; i < D; i++)
        for (int j = 0; j < D; j++) {
            float x = (j + 0.5f - BALL_R) / BALL_R, y = (i + 0.5f - BALL_R) / BALL_R, d2 = x * x + y * y;
            if (d2 >= 1) {
                s_px[i][j].alpha = 0;
                continue;
            }
            float z = sqrtf(1 - d2), diff = fmaxf(0, x * lx + y * ly + z * lz), spec = powf(fmaxf(0, x * hx + y * hy + z * hz), 28);
            s_px[i][j].nx = (int8_t)(x * 127);
            s_px[i][j].ny = (int8_t)(y * 127);
            s_px[i][j].nz = (int8_t)(z * 127);
            s_px[i][j].alpha = (uint8_t)(7 + 7 * (1 - z));
            s_px[i][j].spec = (uint8_t)(15 * spec);
            s_px[i][j].shade = (uint8_t)fminf(3, 4 * (0.25f + 0.75f * diff));
        }
    static const uint32_t k_rgb[2] = {0x3636BB, 0x36D3B3};
    for (int c = 0; c < 2; c++)
        for (int k = 0; k < 4; k++) s_pal[c][k] = gfx_color(scale_rgb(k_rgb[c], 0.62f + 0.13f * k));
    s_white = gfx_color(0xFFFFFF);
    s_rim = gfx_color(0x24247A);
    s_ready = true;
}

/* a -> b by k/16, on byte-swapped RGB565 */
static uint16_t mix(uint16_t a, uint16_t b, int k)
{
    a = GFX_SWAP16(a);
    b = GFX_SWAP16(b);
    int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
    int r = ar + ((((b >> 11) - ar) * k) >> 4), g = ag + (((((b >> 5) & 63) - ag) * k) >> 4), bl = ab + ((((b & 31) - ab) * k) >> 4);
    return GFX_SWAP16((uint16_t)(r << 11 | g << 5 | bl));
}

/* 1 where the texture point (u, v, w) on the unit sphere (Q7) is the green wave; the far side is mirrored. */
static int green_at(int u, int v, int w)
{
    if (w < 0) u = -u;
    int col = (u + 128) * TIDEN_N >> 8, row = (v + 128) * TIDEN_N >> 8;
    return tiden_logo[row][col] == 'g';
}

void ball_draw(uint16_t *fb, float cx, float cy, float roll, float tumble)
{
    if (!s_ready) init();
    /* texture = Rx(tumble) * Rz(roll) * screen, Q12 */
    const float cr = cosf(roll), sr = sinf(roll), ct = cosf(tumble), st = sinf(tumble);
    const int m00 = (int)(4096 * cr), m01 = (int)(4096 * sr), m10 = (int)(-4096 * ct * sr), m11 = (int)(4096 * ct * cr),
              m12 = (int)(4096 * st), m20 = (int)(4096 * st * sr), m21 = (int)(-4096 * st * cr), m22 = (int)(4096 * ct);
    const int x0 = (int)floorf(cx) - BALL_R, y0 = (int)floorf(cy) - BALL_R;
    for (int i = 0; i < D; i++) {
        int y = y0 + i;
        if (y < 0 || y >= GFX_H) continue;
        for (int j = 0; j < D; j++) {
            int x = x0 + j;
            if (x < 0 || x >= GFX_W || !s_px[i][j].alpha) continue;
            const int nx = s_px[i][j].nx, ny = s_px[i][j].ny, nz = s_px[i][j].nz;
            /* the far skin, seen through the near one: same normal with z flipped */
            int bu = (m00 * nx + m01 * ny) >> 12, bv = (m10 * nx + m11 * ny - m12 * nz) >> 12, bw = (m20 * nx + m21 * ny - m22 * nz) >> 12;
            int fu = (m00 * nx + m01 * ny) >> 12, fv = (m10 * nx + m11 * ny + m12 * nz) >> 12, fw = (m20 * nx + m21 * ny + m22 * nz) >> 12;
            uint16_t *px = &fb[y * GFX_W + x];
            uint16_t c = mix(*px, s_pal[green_at(bu, bv, bw)][0], 5);
            c = mix(c, s_pal[green_at(fu, fv, fw)][s_px[i][j].shade], s_px[i][j].alpha);
            if (s_px[i][j].alpha >= 13) c = mix(c, s_rim, 6); /* seam of the rim */
            if (s_px[i][j].spec) c = mix(c, s_white, s_px[i][j].spec);
            *px = c;
        }
    }
}
