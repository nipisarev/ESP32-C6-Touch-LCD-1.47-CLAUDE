#include <math.h>

#include "font.h"
#include "gfx.h"
#include "mascot_pose.h"
#include "scenes.h"

#define TIMEOUT_MS 20000

typedef struct {
    const char *label;
    const scene_t *scene;
} tile_t;

/* Two pages of four big tiles; swipe (or drag) sideways between them. */
static const tile_t k_tiles[2][4] = {
    {{"SHOW", &scene_idle}, {"RUN", &scene_runner}, {"CATCH", &scene_catch}, {"PET", &scene_pet}},
    {{"TOWER", &scene_tower}, {"PONG", &scene_pong}, {"POOL", &scene_pool}, {"RACE", &scene_race}},
};
#define TILE_W 157
#define TILE_H 76
#define DOTS_Y 162

static uint32_t s_last_input;
static int s_page, s_pressed = -1;
static float s_offset; /* animated page offset in px: page p is drawn at x = p*GFX_W - s_offset */

static int tile_at(int x, int y)
{
    int col = x < 2 + TILE_W + 1 ? 0 : 1, row = y < 2 + TILE_H + 2 ? 0 : 1;
    if (y > 2 + 2 * TILE_H + 4) return -1;
    return row * 2 + col;
}

static void enter(uint32_t now)
{
    s_last_input = now;
    s_pressed = -1;
    s_offset = s_page * GFX_W;
}

static void update(const input_t *in, uint32_t now, float dt)
{
    if (in->down || in->tap) s_last_input = now;
    if (in->swipe) s_page = clampf(s_page - in->swipe, 0, 1);
    float target = s_page * GFX_W;
    if (in->down && in->moved) /* the page follows the finger */
        s_offset = clampf(target - (in->x - in->start_x), -40, GFX_W + 40);
    else
        s_offset += (target - s_offset) * fminf(1, dt * 14);
    bool settled = fabsf(s_offset - target) < 2;
    s_pressed = in->down && !in->moved && settled ? tile_at(in->start_x, in->start_y) : -1;
    if (in->tap && settled) {
        int i = tile_at(in->start_x, in->start_y);
        if (i >= 0) app_switch(k_tiles[s_page][i].scene, now);
        return;
    }
    if (now - s_last_input > TIMEOUT_MS) app_switch(&scene_idle, now);
}

static void icon(uint16_t *fb, int i, int cx, int cy, uint32_t now)
{
    mascot_pose_t p = MASCOT_POSE_REST;
    const float s = 0.32f, w = MASCOT_W * s, h = MASCOT_H * s;
    switch (i) {
    case 0: /* show: mascot with confetti */
        p.happy = true;
        mascot_pose_draw(fb, cx - w / 2, cy + 18 - h, s, &p);
        gfx_fill_rect(fb, cx - 26, cy - 18, 4, 4, gfx_color(0x7CA4D0));
        gfx_fill_rect(fb, cx + 20, cy - 22, 4, 4, gfx_color(0xC87392));
        gfx_fill_rect(fb, cx + 26, cy - 8, 4, 4, gfx_color(0xDD8361));
        gfx_fill_rect(fb, cx - 14, cy - 26, 4, 4, gfx_color(0xDD8361));
        break;
    case 1: /* run: mascot jumping over a block */
        p.leg_lift[0] = p.leg_lift[2] = 0.6f;
        mascot_pose_draw(fb, cx - 32, cy + 8 - h, 0.3f, &p);
        gfx_fill_rect(fb, cx + 10, cy + 6, 12, 12, gfx_color(0x3D74B8));
        gfx_fill_rect(fb, cx - 34, cy + 18, 68, 2, gfx_color(0x505050));
        break;
    case 2: /* catch: falling squares */
        p.eye_dy = -3;
        p.hands_dy = -10;
        mascot_pose_draw(fb, cx - w / 2, cy + 20 - h, s, &p);
        gfx_fill_rect(fb, cx - 4, cy - 28 + (int)(now / 60) % 12, 6, 6, gfx_color(0x7CA4D0));
        gfx_fill_rect(fb, cx - 26, cy - 22, 6, 6, gfx_color(0xC87392));
        gfx_fill_rect(fb, cx + 20, cy - 26, 6, 6, gfx_color(0xDD8361));
        break;
    case 3: /* pet: heart */
        draw_heart(fb, cx - 14, cy - 14, 4, gfx_color(0xE8506E));
        break;
    case 4: /* tower: stacked blocks with Claude on top */
        gfx_fill_rect(fb, cx - 24, cy + 10, 48, 8, gfx_color(0xDD8361));
        gfx_fill_rect(fb, cx - 20, cy + 2, 44, 8, gfx_color(0xC87392));
        gfx_fill_rect(fb, cx - 22, cy - 6, 40, 8, gfx_color(0x7CA4D0));
        mascot_pose_draw(fb, cx - 2 - 53.5f * 0.24f, cy - 6 - MASCOT_H * 0.24f, 0.24f, &p);
        break;
    case 5: /* pong: paddle, ball, Claude */
        gfx_fill_rect(fb, cx - 40, cy - 12, 4, 22, gfx_color(0xFFFFFF));
        gfx_fill_rect(fb, cx - 8, cy - 4, 5, 5, gfx_color(0xFFFFFF));
        p.eye_dx = -4;
        mascot_pose_draw(fb, cx + 8, cy - 14, 0.3f, &p);
        break;
    case 7: { /* race: a small wheel held by two paws, on a road */
        const uint16_t road = gfx_color(0x555560), wheel = gfx_color(0x2A2A30), paw = gfx_color(MASCOT_RGB);
        float xs[4] = {cx - 6, cx + 6, cx + 40, cx - 40}, ys[4] = {cy - 22, cy - 22, cy + 18, cy + 18};
        gfx_fill_quad(fb, xs, ys, road, 0);
        gfx_fill_box(fb, cx - 1, cy - 14, cx + 1, cy - 8, gfx_color(0xFFFFFF), 0);
        gfx_fill_box(fb, cx - 1, cy - 2, cx + 1, cy + 6, gfx_color(0xFFFFFF), 0);
        float ang = 0.35f * sinf(now * 0.003f);
        for (int k = 0; k < 16; k++) {
            float a = k * 0.3927f + ang;
            gfx_fill_rot_box(fb, cx, cy + 22, a, -3, -18, 3, -13, wheel);
        }
        gfx_fill_rot_box(fb, cx, cy + 22, ang, -2, -14, 2, 0, wheel);
        gfx_fill_rot_box(fb, cx, cy + 22, ang - 0.9f, -4, -20, 4, -12, paw);
        gfx_fill_rot_box(fb, cx, cy + 22, ang + 0.9f, -4, -20, 4, -12, paw);
        break;
    }
    default: { /* pool: Claude bobbing in a wave */
        float bob = 2 * sinf(now * 0.004f);
        p.happy = true;
        mascot_pose_draw(fb, cx - w / 2, cy - 14 + bob, s, &p);
        for (int x = -40; x < 40; x += 4) {
            float y = cy + 4 + 2 * sinf((x + now * 0.05f) * 0.15f);
            gfx_fill_box_dither(fb, cx + x, y, cx + x + 4, cy + 20, gfx_color(0x3A9AE0));
            gfx_fill_box(fb, cx + x, y - 1, cx + x + 4, y + 1, gfx_color(0xBEE8FF), 0);
        }
        break;
    }
    }
}

static void draw(uint16_t *fb, uint32_t now)
{
    gfx_clear(fb, GFX_HEX(0x000000));
    for (int pg = 0; pg < 2; pg++) {
        int px = (int)(pg * GFX_W - s_offset);
        if (px <= -GFX_W || px >= GFX_W) continue;
        for (int i = 0; i < 4; i++) {
            int x = px + 2 + (i % 2) * (TILE_W + 2), y = 2 + (i / 2) * (TILE_H + 2);
            bool pressed = pg == s_page && i == s_pressed;
            gfx_fill_rect(fb, x, y, TILE_W, TILE_H, gfx_color(pressed ? 0x3A3A3A : 0x1C1C1C));
            icon(fb, pg * 4 + i, x + TILE_W / 2, y + 30, now);
            font_draw_centered(fb, x + TILE_W / 2, y + TILE_H - 14, 2, gfx_color(0xFFFFFF), k_tiles[pg][i].label);
        }
    }
    float pos = s_offset / GFX_W; /* page dots */
    for (int pg = 0; pg < 2; pg++) {
        bool on = fabsf(pos - pg) < 0.5f;
        gfx_fill_rect(fb, GFX_W / 2 - 14 + pg * 20, DOTS_Y + (on ? 0 : 1), on ? 8 : 6, on ? 8 : 6, gfx_color(on ? 0xFFFFFF : 0x606060));
    }
    if (s_page == 0) font_draw(fb, GFX_W - 60, DOTS_Y, 2, gfx_color(0x707070), "SWIPE >");
    else font_draw(fb, 6, DOTS_Y, 2, gfx_color(0x707070), "< SWIPE");
}

const scene_t scene_menu = {enter, update, draw, 0};
