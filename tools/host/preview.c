/* Host preview: renders contact sheets of the engine output (same C code as the firmware).
 *   ./preview <out_dir> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "mascot_anim.h"

#define COLS 4
#define ROWS 4
#define GAP 4
#define SHEET_W (COLS * GFX_W + (COLS + 1) * GAP)
#define SHEET_H (ROWS * GFX_H + (ROWS + 1) * GAP)

static uint8_t sheet[SHEET_H][SHEET_W][3];
static uint16_t fb[GFX_W * GFX_H];

static void blit(int col, int row)
{
    for (int y = 0; y < GFX_H; y++)
        for (int x = 0; x < GFX_W; x++) {
            uint16_t c = GFX_SWAP16(fb[y * GFX_W + x]);
            uint8_t *px = sheet[GAP + row * (GFX_H + GAP) + y][GAP + col * (GFX_W + GAP) + x];
            px[0] = ((c >> 11) & 0x1F) * 255 / 31;
            px[1] = ((c >> 5) & 0x3F) * 255 / 63;
            px[2] = (c & 0x1F) * 255 / 31;
        }
}

static void write_bmp(const char *path)
{
    FILE *f = fopen(path, "wb");
    int stride = (SHEET_W * 3 + 3) & ~3, size = 54 + stride * SHEET_H;
    uint8_t h[54] = {'B', 'M'};
    memcpy(h + 2, &size, 4);
    h[10] = 54;
    h[14] = 40;
    int w = SHEET_W, hh = SHEET_H;
    memcpy(h + 18, &w, 4);
    memcpy(h + 22, &hh, 4);
    h[26] = 1;
    h[28] = 24;
    fwrite(h, 1, 54, f);
    uint8_t row[SHEET_W * 3 + 4] = {0};
    for (int y = SHEET_H - 1; y >= 0; y--) {
        for (int x = 0; x < SHEET_W; x++) {
            row[x * 3] = sheet[y][x][2];
            row[x * 3 + 1] = sheet[y][x][1];
            row[x * 3 + 2] = sheet[y][x][0];
        }
        fwrite(row, 1, stride, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    /* Diagnostics: PREVIEW_SCALE / PREVIEW_GROUND / PREVIEW_BG override the firmware layout. */
    const float s = getenv("PREVIEW_SCALE") ? (float)atof(getenv("PREVIEW_SCALE")) : mascot_data_scale;
    const float ground = getenv("PREVIEW_GROUND") ? (float)atof(getenv("PREVIEW_GROUND")) : 170.0f;
    const uint32_t bg = getenv("PREVIEW_BG") ? (uint32_t)strtoul(getenv("PREVIEW_BG"), NULL, 16) : 0x000000;
    for (int a = 0; a < MANIM_COUNT; a++) {
        const manim_anim_t *an = &mascot_anims[a];
        float period = 0;
        for (int k = 0; k < an->tl_count; k++) {
            const manim_tl_t *tl = &mascot_tls[an->tl_start + k];
            if (tl->delay + tl->period > period) period = tl->delay + tl->period;
        }
        const char *env = getenv("PREVIEW_T0");
        float t0 = env ? (float)atof(env) : 0, span = getenv("PREVIEW_SPAN") ? (float)atof(getenv("PREVIEW_SPAN")) : period;
        memset(sheet, 40, sizeof(sheet));
        float ox = a == MANIM_WALKING ? 0 : (GFX_W - an->vb_w * s) / 2, oy = ground - an->vb_h * s;
        for (int i = 0; i < COLS * ROWS; i++) {
            float t = t0 + span * i / (COLS * ROWS);
            gfx_clear(fb, gfx_color(bg));
            manim_draw(fb, a, t, ox, oy, s);
            blit(i % COLS, i / COLS);
        }
        char path[512];
        snprintf(path, sizeof(path), "%s/%s.bmp", out, an->name);
        write_bmp(path);
        printf("%s: period %.3f s, sheet %s (t=%.2f..%.2f)\n", an->name, period, path, t0, t0 + span);
    }
    return 0;
}
