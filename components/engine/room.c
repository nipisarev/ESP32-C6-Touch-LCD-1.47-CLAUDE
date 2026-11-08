#include "room.h"

#include <math.h>

#include "gfx.h"
#include "scenes.h"


static uint32_t mix(uint32_t a, uint32_t b, float k)
{
    int r = (int)lerpf((a >> 16) & 0xFF, (b >> 16) & 0xFF, k), g = (int)lerpf((a >> 8) & 0xFF, (b >> 8) & 0xFF, k),
        bl = (int)lerpf(a & 0xFF, b & 0xFF, k);
    return (uint32_t)(r << 16 | g << 8 | bl);
}

uint32_t sky_color(float daylight, int band)
{
    static const uint32_t night[3] = {0x0A1030, 0x101A44, 0x1A2656}, day[3] = {0x4FA3E0, 0x7EC4F0, 0xB8E2F8};
    return mix(night[band], day[band], clampf(daylight, 0, 1));
}

void door_draw(uint16_t *fb, float x0, float y0, float x1, float y1, float open, uint32_t outside)
{
    const uint16_t frame = gfx_color(0x5A3E2E), leaf = gfx_color(0x7A5642), panel = gfx_color(0x6B4A3A);
    gfx_fill_box(fb, x0 - 3, y0 - 3, x1 + 3, y1, frame, 0);
    gfx_fill_box(fb, x0, y0, x1, y1, gfx_color(outside), 0);
    float w = (x1 - x0) * (1 - 0.85f * clampf(open, 0, 1)); /* the leaf swings towards the hinge */
    gfx_fill_box(fb, x0, y0, x0 + w, y1, leaf, 0);
    if (w > 16) {
        gfx_fill_box(fb, x0 + 5, y0 + 6, x0 + w - 5, y0 + (y1 - y0) * 0.45f, panel, 0);
        gfx_fill_box(fb, x0 + 5, y0 + (y1 - y0) * 0.55f, x0 + w - 5, y1 - 6, panel, 0);
        gfx_fill_box(fb, x0 + w - 8, y0 + (y1 - y0) * 0.5f - 2, x0 + w - 4, y0 + (y1 - y0) * 0.5f + 2, gfx_color(0xE0C050), 0);
    }
}

static void window_view(uint16_t *fb, float daylight, float t)
{
    const gfx_clip_t clip = {(int)WIN_X0, (int)WIN_Y0, (int)WIN_X1, (int)WIN_Y1};
    const float h = WIN_Y1 - WIN_Y0;
    for (int b = 0; b < 3; b++)
        gfx_fill_box(fb, WIN_X0, WIN_Y0 + h * b / 3, WIN_X1, WIN_Y0 + h * (b + 1) / 3, gfx_color(sky_color(daylight, b)), &clip);
    if (daylight < 0.6f) /* stars fade at dawn */
        for (int i = 0; i < 6; i++)
            if (fmodf(t * 0.6f + i * 0.37f, 1.0f) > 0.2f)
                gfx_fill_box(fb, WIN_X0 + 6 + i * 11, WIN_Y0 + 6 + (i * 7) % 20, WIN_X0 + 8 + i * 11, WIN_Y0 + 8 + (i * 7) % 20,
                             gfx_color(0xE8E8F0), &clip);
    /* the moon sets and the sun rises as daylight goes 0 -> 1 */
    static const int disc[7] = {8, 14, 18, 18, 18, 14, 8};
    float moon_y = WIN_Y0 + 8 + 60 * daylight, sun_y = WIN_Y0 + 10 + 60 * (1 - daylight);
    for (int r = 0; r < 7; r++) {
        gfx_fill_box(fb, 252 - disc[r] / 2.0f, moon_y + r * 3, 252 + disc[r] / 2.0f, moon_y + r * 3 + 3, gfx_color(0xF2EBC0), &clip);
        gfx_fill_box(fb, 264 - disc[r] / 2.0f, sun_y + r * 3, 264 + disc[r] / 2.0f, sun_y + r * 3 + 3, gfx_color(0xF7C948), &clip);
    }
    /* frame and cross bars */
    const uint16_t wood = gfx_color(0x7A5642);
    gfx_fill_box(fb, WIN_X0 - 4, WIN_Y0 - 4, WIN_X1 + 4, WIN_Y0, wood, 0);
    gfx_fill_box(fb, WIN_X0 - 4, WIN_Y1, WIN_X1 + 4, WIN_Y1 + 5, wood, 0);
    gfx_fill_box(fb, WIN_X0 - 4, WIN_Y0, WIN_X0, WIN_Y1, wood, 0);
    gfx_fill_box(fb, WIN_X1, WIN_Y0, WIN_X1 + 4, WIN_Y1, wood, 0);
    gfx_fill_box(fb, (WIN_X0 + WIN_X1) / 2 - 1.5f, WIN_Y0, (WIN_X0 + WIN_X1) / 2 + 1.5f, WIN_Y1, wood, 0);
    gfx_fill_box(fb, WIN_X0, (WIN_Y0 + WIN_Y1) / 2 - 1.5f, WIN_X1, (WIN_Y0 + WIN_Y1) / 2 + 1.5f, wood, 0);
}

static float s_thirst;

void room_set_plant(float thirst) { s_thirst = clampf(thirst, 0, 1); }

static void picture(uint16_t *fb)
{
    const float x0 = 130, y0 = 20, x1 = 168, y1 = 50;
    gfx_fill_box(fb, x0 - 3, y0 - 3, x1 + 3, y1 + 3, gfx_color(0xC8A040), 0); /* gilded frame */
    gfx_fill_box(fb, x0, y0, x1, y1, gfx_color(0x8FD0F0), 0);
    gfx_fill_box(fb, x0 + 26, y0 + 4, x0 + 32, y0 + 10, gfx_color(0xF7C948), 0); /* sun */
    for (int r = 0; r < 5; r++) { /* two mountains */
        float h = r * 3;
        gfx_fill_box(fb, x0 + 10 - h, y0 + 10 + r * 3, x0 + 12 + h, y0 + 13 + r * 3, gfx_color(r ? 0x7A8898 : 0xF2F2F2), 0);
        gfx_fill_box(fb, x0 + 24 - h * 0.8f, y0 + 14 + r * 2, x0 + 26 + h * 0.8f, y0 + 16 + r * 2, gfx_color(0x6A7888), 0);
    }
    gfx_fill_box(fb, x0, y0 + 24, x1, y1, gfx_color(0x5DAA48), 0); /* hill */
}

static void cabinet_and_plant(uint16_t *fb)
{
    const float cx = PLANT_X, top = 112, w = s_thirst;
    gfx_fill_box(fb, cx - 28, top, cx + 28, FLOOR_Y, gfx_color(0x6B4A3A), 0);
    gfx_fill_box(fb, cx - 30, top - 3, cx + 30, top, gfx_color(0x8A5E44), 0);
    for (int d = 0; d < 2; d++) { /* two drawers */
        float y = top + 4 + d * 17;
        gfx_fill_box(fb, cx - 24, y, cx + 24, y + 13, gfx_color(0x7A5642), 0);
        gfx_fill_box(fb, cx - 3, y + 5, cx + 3, y + 8, gfx_color(0xE0C050), 0);
    }
    /* pot */
    gfx_fill_box(fb, cx - 12, POT_TOP, cx + 12, POT_TOP + 4, gfx_color(0xD0704A), 0);
    gfx_fill_box(fb, cx - 10, POT_TOP + 4, cx + 10, top - 3, gfx_color(0xB85A38), 0);
    /* stem bends and the head droops as the plant gets thirsty */
    const float hx = cx + 12 * w, hy = 74 + 16 * w;
    for (int k = 0; k < 6; k++) {
        float a = k / 6.0f, b = (k + 1) / 6.0f;
        float xa = cx + (hx - cx) * a * a, ya = POT_TOP - (POT_TOP - hy) * a, yb = POT_TOP - (POT_TOP - hy) * b;
        gfx_fill_box(fb, xa - 1, yb, xa + 1, ya, gfx_color(0x3E8E3A), 0);
    }
    const uint16_t leaf = gfx_color(0x4CAF50);
    gfx_fill_box(fb, cx - 9, POT_TOP - 12 + 4 * w, cx - 1, POT_TOP - 9 + 4 * w, leaf, 0);
    gfx_fill_box(fb, cx + 1 + 2 * w, POT_TOP - 18 + 5 * w, cx + 9 + 2 * w, POT_TOP - 15 + 5 * w, leaf, 0);
    const float r = 6 - 3 * w; /* petals close up when wilted */
    const uint16_t petal = gfx_color(w > 0.7f ? 0xB86A80 : 0xE8507A);
    gfx_fill_box(fb, hx - r - 2, hy - 2, hx + r + 2, hy + 2, petal, 0);
    gfx_fill_box(fb, hx - 2, hy - r - 2, hx + 2, hy + r + 2, petal, 0);
    gfx_fill_box(fb, hx - 2, hy - 2, hx + 2, hy + 2, gfx_color(0xF7C948), 0);
}

/* Tiden logo, 28x28 px: generated by tools/logo_clock.py from tiden_logo.svg */
const char *const tiden_logo[TIDEN_N] = {
    "..........bbbbbbbb..........",
    "........bbbbbbbbbbbb........",
    "......bbbbbbbbbbbbbbbb......",
    ".....bbbbbbbbbbbbbbbbbb.....",
    "....bbbbbbbbbbbbbbbbbbbb....",
    "...bbbbbbbbbbbbbbbbbbbbbb...",
    "..bbbbbbbbbbbbbbbbbbbbbbbb..",
    "..bbbbbbbbbbbbbbbbbbbbbbbb..",
    ".bbbbbbbbbbbbbbbbbbbbbbbbbb.",
    ".bbbbbbbbbbbbbbbbbbbbbbbbbb.",
    "bbbbbbbggggbbbbbbbbbbbbbbbbb",
    "bbbbbgggggggbbbbbbbbbbbbbbbb",
    "bbbbgggggggggbbbbbbbbbbbbbbb",
    "bbbbggggggggggbbbbbbbbbbbbbb",
    "bbbbggggggggggbbbbbbbbbbbbbb",
    "bbbbgggggggggggbbbbbbbbbgbbb",
    "bbbbggggggggggggbbbbbbbgbbbb",
    "bbbbgggggggggggggbbbbgggbbbb",
    ".bbbggggggggggggggggggggbbb.",
    ".bbbbggggggggggggggggggbbbb.",
    "..bbbggggggggggggggggggbbb..",
    "..bbbbggggggggggggggggbbbb..",
    "...bbbbggggggggggggggbbbb...",
    "....bbbbbggggggggggbbbbb....",
    ".....bbbbbbbggggbbbbbbb.....",
    "......bbbbbbbbbbbbbbbb......",
    "........bbbbbbbbbbbb........",
    "..........bbbbbbbb..........",
};

static void disc(uint16_t *fb, float cx, float cy, float r, uint16_t c)
{
    for (int dy = -(int)ceilf(r); dy < (int)ceilf(r); dy++) {
        float v = r * r - (dy + 0.5f) * (dy + 0.5f);
        if (v > 0) gfx_fill_box(fb, cx - sqrtf(v), cy + dy, cx + sqrtf(v), cy + dy + 1, c, 0);
    }
}

/* Wall clock over the door: wooden rim (the door colors), the Tiden logo as the dial (blue, the wave green), hands on the uptime from 10:10. */
static void wall_clock(uint16_t *fb)
{
    const float cx = DOOR_CX, cy = CLOCK_Y, r = TIDEN_N / 2.0f;
    const uint16_t blue = gfx_color(0x3636BB), green = gfx_color(0x36D3B3), hand = gfx_color(0xF2F2F2);
    disc(fb, cx, cy, r + 3.8f, gfx_color(0x5A3E2E)); /* edge, as the door frame */
    disc(fb, cx, cy, r + 3, gfx_color(0x7A5642));    /* rim, as the leaf */
    disc(fb, cx, cy, r + 1, gfx_color(0x6B4A3A));    /* inner groove, as the panels */
    for (int row = 0; row < TIDEN_N; row++)
        for (int col = 0; col < TIDEN_N; col++) {
            char ch = tiden_logo[row][col];
            if (ch != '.') gfx_fill_box(fb, cx - r + col, cy - r + row, cx - r + col + 1, cy - r + row + 1, ch == 'g' ? green : blue, 0);
        }
    for (int k = 0; k < 4; k++) /* 12, 3, 6, 9 on the rim */
        gfx_fill_rot_box(fb, cx, cy, k * 1.5708f, -0.8f, -r - 2.6f, 0.8f, -r - 0.4f, gfx_color(0xE0C050)); /* brass, as the handle */
    uint32_t s = 10 * 3600 + 10 * 60 + app_uptime_ms() / 1000;
    float sec = (float)(s % 60), min = (float)(s / 60 % 60) + sec / 60, hr = (float)(s / 3600 % 12) + min / 60;
    gfx_fill_rot_box(fb, cx, cy, hr / 12 * 6.2832f, -1.5f, -r * 0.55f, 1.5f, 2, hand);
    gfx_fill_rot_box(fb, cx, cy, min / 60 * 6.2832f, -1, -r * 0.85f, 1, 2, hand);
    gfx_fill_rot_box(fb, cx, cy, sec / 60 * 6.2832f, -0.5f, -r * 0.9f, 0.5f, 3, gfx_color(0xDD775B));
    gfx_fill_box(fb, cx - 2, cy - 2, cx + 2, cy + 2, hand, 0);
}

void room_draw(uint16_t *fb, float daylight, float door_open, float t)
{
    gfx_clear(fb, GFX_HEX(0x000000));
    /* wooden floor: staggered planks */
    static const uint32_t planks[3] = {0x8A5A3A, 0x7E5234, 0x94643F};
    for (int row = 0; FLOOR_Y + row * 6 < GFX_H; row++) {
        float y = FLOOR_Y + row * 6;
        gfx_fill_box(fb, 0, y, GFX_W, y + 6, gfx_color(planks[row % 3]), 0);
        gfx_fill_box(fb, 0, y + 5, GFX_W, y + 6, gfx_color(0x5E3E26), 0);
        for (float x = (row % 2) * 40 + 20; x < GFX_W; x += 80) gfx_fill_box(fb, x, y, x + 1, y + 5, gfx_color(0x5E3E26), 0);
    }
    gfx_fill_box(fb, 0, FLOOR_Y - 3, GFX_W, FLOOR_Y, gfx_color(0x4A3424), 0); /* baseboard */
    window_view(fb, daylight, t);
    picture(fb);
    cabinet_and_plant(fb);
    door_draw(fb, DOOR_X0, DOOR_Y0, DOOR_X1, FLOOR_Y - 3, door_open, sky_color(daylight, 2));
    wall_clock(fb);
}
