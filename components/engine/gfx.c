#include "gfx.h"

#include <math.h>

static uint8_t s_gamma_lut[256];
static float s_saturation = 1.0f;
static int s_sat256 = 256; /* the same saturation in 1/256: gfx_color stays integer (the C6 has no FPU) */
static int s_lut_ready;

static uint16_t to_panel(float r, float g, float b, float saturation)
{
    float luma = 0.299f * r + 0.587f * g + 0.114f * b;
    float c[3] = {r, g, b};
    for (int i = 0; i < 3; i++) {
        c[i] = luma + (c[i] - luma) * saturation;
        c[i] = c[i] < 0 ? 0 : c[i] > 255 ? 255 : c[i];
    }
    uint16_t v = GFX_RGB565((int)(c[0] + 0.5f), (int)(c[1] + 0.5f), (int)(c[2] + 0.5f));
    return GFX_SWAP16(v);
}

void gfx_set_color_correction(float gamma, float saturation)
{
    for (int i = 0; i < 256; i++) s_gamma_lut[i] = (uint8_t)(255.0f * powf(i / 255.0f, gamma) + 0.5f);
    s_saturation = saturation;
    s_sat256 = (int)(saturation * 256 + 0.5f);
    s_lut_ready = 1;
}

uint16_t gfx_color(uint32_t rgb)
{
    if (!s_lut_ready) gfx_set_color_correction(GFX_GAMMA, GFX_SATURATION);
    int c[3] = {s_gamma_lut[(rgb >> 16) & 0xFF], s_gamma_lut[(rgb >> 8) & 0xFF], s_gamma_lut[rgb & 0xFF]};
    const int luma = (77 * c[0] + 150 * c[1] + 29 * c[2] + 128) >> 8; /* 0.299, 0.587, 0.114 */
    for (int i = 0; i < 3; i++) {
        int v = luma + (((c[i] - luma) * s_sat256 + 128) >> 8);
        c[i] = v < 0 ? 0 : v > 255 ? 255 : v;
    }
    return GFX_SWAP16(GFX_RGB565(c[0], c[1], c[2]));
}

uint16_t gfx_color_with(uint32_t rgb, float gamma, float saturation)
{
    float c[3] = {(float)((rgb >> 16) & 0xFF), (float)((rgb >> 8) & 0xFF), (float)(rgb & 0xFF)};
    for (int i = 0; i < 3; i++) c[i] = 255.0f * powf(c[i] / 255.0f, gamma);
    return to_panel(c[0], c[1], c[2], saturation);
}

static inline int iround(float v) { return (int)(v >= 0 ? v + 0.5f : v - 0.5f); }

void gfx_clear(uint16_t *fb, uint16_t color)
{
    for (int i = 0; i < GFX_W * GFX_H; i++) fb[i] = color;
}

void gfx_fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > GFX_W ? GFX_W : x + w;
    int y1 = y + h > GFX_H ? GFX_H : y + h;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = fb + yy * GFX_W;
        for (int xx = x0; xx < x1; xx++) row[xx] = color;
    }
}

void gfx_fill_rectf(uint16_t *fb, float x, float y, float w, float h, uint16_t color)
{
    int x0 = iround(x), y0 = iround(y);
    gfx_fill_rect(fb, x0, y0, iround(x + w) - x0, iround(y + h) - y0, color);
}

static gfx_clip_t clip_or_screen(const gfx_clip_t *clip)
{
    gfx_clip_t c = {0, 0, GFX_W, GFX_H};
    if (clip) {
        if (clip->x0 > c.x0) c.x0 = clip->x0;
        if (clip->y0 > c.y0) c.y0 = clip->y0;
        if (clip->x1 < c.x1) c.x1 = clip->x1;
        if (clip->y1 < c.y1) c.y1 = clip->y1;
    }
    return c;
}

void gfx_fill_box(uint16_t *fb, float x0, float y0, float x1, float y1, uint16_t color, const gfx_clip_t *clip)
{
    gfx_clip_t c = clip_or_screen(clip);
    int ix0 = iround(x0), iy0 = iround(y0), ix1 = iround(x1), iy1 = iround(y1);
    if (ix0 < c.x0) ix0 = c.x0;
    if (iy0 < c.y0) iy0 = c.y0;
    if (ix1 > c.x1) ix1 = c.x1;
    if (iy1 > c.y1) iy1 = c.y1;
    for (int y = iy0; y < iy1; y++) {
        uint16_t *row = fb + y * GFX_W;
        for (int x = ix0; x < ix1; x++) row[x] = color;
    }
}

void gfx_fill_quad(uint16_t *fb, const float xs[4], const float ys[4], uint16_t color, const gfx_clip_t *clip)
{
    gfx_clip_t c = clip_or_screen(clip);
    float miny = ys[0], maxy = ys[0];
    for (int i = 1; i < 4; i++) {
        if (ys[i] < miny) miny = ys[i];
        if (ys[i] > maxy) maxy = ys[i];
    }
    int y0 = iround(miny), y1 = iround(maxy);
    if (y0 < c.y0) y0 = c.y0;
    if (y1 > c.y1) y1 = c.y1;
    for (int y = y0; y < y1; y++) {
        float sy = y + 0.5f, xl = 1e9f, xr = -1e9f;
        for (int i = 0; i < 4; i++) {
            float ax = xs[i], ay = ys[i], bx = xs[(i + 1) & 3], by = ys[(i + 1) & 3];
            if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) {
                float x = ax + (sy - ay) * (bx - ax) / (by - ay);
                if (x < xl) xl = x;
                if (x > xr) xr = x;
            }
        }
        int ix0 = iround(xl), ix1 = iround(xr);
        if (ix0 < c.x0) ix0 = c.x0;
        if (ix1 > c.x1) ix1 = c.x1;
        uint16_t *row = fb + y * GFX_W;
        for (int x = ix0; x < ix1; x++) row[x] = color;
    }
}

void gfx_fill_box_dither(uint16_t *fb, float x0, float y0, float x1, float y1, uint16_t color)
{
    int ix0 = iround(x0), iy0 = iround(y0), ix1 = iround(x1), iy1 = iround(y1);
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > GFX_W) ix1 = GFX_W;
    if (iy1 > GFX_H) iy1 = GFX_H;
    for (int y = iy0; y < iy1; y++) {
        uint16_t *row = fb + y * GFX_W;
        for (int x = ix0 + ((ix0 + y) & 1); x < ix1; x += 2) row[x] = color;
    }
}

void gfx_fill_rot_box(uint16_t *fb, float px, float py, float ang, float x0, float y0, float x1, float y1, uint16_t color)
{
    const float cs = cosf(ang), sn = sinf(ang), lx[4] = {x0, x1, x1, x0}, ly[4] = {y0, y0, y1, y1};
    float xs[4], ys[4];
    for (int k = 0; k < 4; k++) {
        xs[k] = px + lx[k] * cs - ly[k] * sn;
        ys[k] = py + lx[k] * sn + ly[k] * cs;
    }
    gfx_fill_quad(fb, xs, ys, color, 0);
}

void gfx_fill_box_dither_sparse(uint16_t *fb, float x0, float y0, float x1, float y1, uint16_t color)
{
    int ix0 = iround(x0), iy0 = iround(y0), ix1 = iround(x1), iy1 = iround(y1);
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > GFX_W) ix1 = GFX_W;
    if (iy1 > GFX_H) iy1 = GFX_H;
    for (int y = iy0 + (iy0 & 1); y < iy1; y += 2) {
        uint16_t *row = fb + y * GFX_W;
        for (int x = ix0 + ((ix0 + y / 2) & 1); x < ix1; x += 2) row[x] = color;
    }
}
