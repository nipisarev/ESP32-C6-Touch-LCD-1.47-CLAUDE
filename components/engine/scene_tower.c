#include <math.h>
#include <stdio.h>

#include "font.h"
#include "gfx.h"
#include "mascot_pose.h"
#include "scenes.h"

#define BASE_Y 166.0f
#define BLOCK_H 12.0f
#define BASE_W 120.0f
#define MAX_BLOCKS 256
#define CLAUDE_S 0.32f
#define PERFECT_PX 3.0f
#define RESTART_DELAY_MS 700

typedef enum { READY, PLAY, OVER } state_t;
typedef struct {
    float x, w;
} block_t;

static const uint32_t k_colors[] = {0xDD8361, 0xC87392, 0x7CA4D0, 0x3D74B8, 0xE8A05A, 0x9DB4D8};

static state_t s_state;
static block_t s_blocks[MAX_BLOCKS];
static int s_n; /* placed blocks, including the base */
static float s_x, s_dir, s_speed, s_cam, s_hop_t, s_perfect_t;
static float s_fall_x, s_fall_w, s_fall_y, s_fall_vy;
static int s_fall_level;
static int32_t s_best;
static uint32_t s_over_at;

static int score(void) { return s_n - 1; }
static uint32_t color_of(int i) { return k_colors[i % (int)(sizeof(k_colors) / sizeof(k_colors[0]))]; }

static void next_block(void)
{
    s_dir = (s_n % 2) ? -1 : 1;
    s_x = s_dir > 0 ? -s_blocks[s_n - 1].w : GFX_W;
    s_speed = fminf(90 + 7 * score(), 260);
}

static void reset(void)
{
    s_n = 1;
    s_blocks[0] = (block_t){(GFX_W - BASE_W) / 2, BASE_W};
    s_cam = 0;
    s_hop_t = s_perfect_t = -1;
    s_fall_w = 0;
    next_block();
}

static void enter(uint32_t now)
{
    (void)now;
    s_best = app_load("tower_best", 0);
    s_state = READY;
    reset();
}

static void drop(uint32_t now)
{
    const block_t *top = &s_blocks[s_n - 1];
    float w = top->w, x0 = fmaxf(s_x, top->x), x1 = fminf(s_x + w, top->x + w);
    if (x1 - x0 <= 0) { /* missed: the whole block falls */
        s_fall_x = s_x;
        s_fall_w = w;
        s_fall_level = s_n;
        s_fall_y = 0;
        s_fall_vy = 0;
        s_state = OVER;
        s_over_at = now;
        if (score() > s_best) {
            s_best = score();
            app_save("tower_best", s_best);
        }
        return;
    }
    if (fabsf(s_x - top->x) <= PERFECT_PX) { /* snap: no cut */
        x0 = top->x;
        x1 = top->x + w;
        s_perfect_t = 0;
    } else { /* the overhang falls away */
        s_fall_x = s_x < top->x ? s_x : x1;
        s_fall_w = w - (x1 - x0);
        s_fall_level = s_n;
        s_fall_y = 0;
        s_fall_vy = 0;
    }
    if (s_n < MAX_BLOCKS) s_blocks[s_n++] = (block_t){x0, x1 - x0};
    s_hop_t = 0;
    next_block();
}

static void update(const input_t *in, uint32_t now, float dt)
{
    if (s_fall_w > 0) {
        s_fall_vy += 600 * dt;
        s_fall_y += s_fall_vy * dt;
        if (s_fall_y > 260) s_fall_w = 0;
    }
    if (s_hop_t >= 0 && (s_hop_t += dt) > 0.35f) s_hop_t = -1;
    if (s_perfect_t >= 0 && (s_perfect_t += dt) > 0.7f) s_perfect_t = -1;
    float cam_target = fmaxf(0, (s_n - 7) * BLOCK_H); /* keep the top of the tower in view */
    s_cam += (cam_target - s_cam) * fminf(1, dt * 6);

    if (s_state == READY) {
        if (in->pressed) s_state = PLAY;
        return;
    }
    if (s_state == OVER) {
        if (in->pressed && now - s_over_at > RESTART_DELAY_MS) {
            reset();
            s_state = PLAY;
        }
        return;
    }
    s_x += s_dir * s_speed * dt;
    float w = s_blocks[s_n - 1].w;
    if (s_x > GFX_W - w * 0.25f) s_dir = -1;
    if (s_x < -w * 0.75f) s_dir = 1;
    if (in->pressed) drop(now);
}

static float level_bottom(int i) { return BASE_Y - i * BLOCK_H + s_cam; }

static void draw_block(uint16_t *fb, float x, float w, float bottom, uint32_t color)
{
    gfx_fill_box(fb, x, bottom - BLOCK_H, x + w, bottom, gfx_color(color), 0);
    gfx_fill_box(fb, x, bottom - BLOCK_H, x + w, bottom - BLOCK_H + 2, gfx_color(0xFFFFFF), 0);
}

static void draw(uint16_t *fb, uint32_t now)
{
    char buf[24];
    const uint16_t white = gfx_color(0xFFFFFF), gray = gfx_color(0x909090);
    gfx_clear(fb, GFX_HEX(0x000000));
    gfx_fill_box(fb, 0, BASE_Y + s_cam, GFX_W, BASE_Y + s_cam + 2, gfx_color(0x505050), 0);
    for (int i = 0; i < s_n; i++) {
        float b = level_bottom(i);
        if (b < 0 || b - BLOCK_H > GFX_H) continue;
        draw_block(fb, s_blocks[i].x, s_blocks[i].w, b, color_of(i));
    }
    if (s_fall_w > 0) draw_block(fb, s_fall_x, s_fall_w, level_bottom(s_fall_level) + s_fall_y, color_of(s_fall_level));
    if (s_state != OVER) draw_block(fb, s_x, s_blocks[s_n - 1].w, level_bottom(s_n), color_of(s_n));

    /* Claude rides the top block and hops onto each new one. */
    const block_t *top = &s_blocks[s_n - 1];
    mascot_pose_t p = MASCOT_POSE_REST;
    float hop = s_hop_t >= 0 ? 14 * sinf(s_hop_t / 0.35f * (float)M_PI) : 0;
    float feet = level_bottom(s_n - 1) - BLOCK_H - hop, ox = top->x + top->w / 2 - 53.5f * CLAUDE_S;
    if (s_state == PLAY) p.eye_dx = clampf((s_x + top->w / 2 - (top->x + top->w / 2)) / 20, -4, 4);
    if (s_hop_t >= 0 || s_perfect_t >= 0) p.happy = true;
    if (s_state == OVER) {
        p.eye_open = 0.15f;
        p.hands_dy = 8;
    }
    mascot_pose_draw(fb, ox, feet - MASCOT_H * CLAUDE_S, CLAUDE_S, &p);

    snprintf(buf, sizeof(buf), "%d", score());
    font_draw(fb, GFX_W - 6 - font_width(buf, 3), 6, 3, white, buf);
    snprintf(buf, sizeof(buf), "BEST %d", (int)s_best);
    font_draw(fb, 6, 6, 2, gray, buf);
    if (s_perfect_t >= 0) font_draw_centered(fb, GFX_W / 2, 30, 2, gfx_color(0xE8A05A), "PERFECT!");
    if (s_state == READY && (now / 500) % 2 == 0) font_draw_centered(fb, GFX_W / 2, 40, 3, white, "TAP TO DROP");
    if (s_state == OVER) {
        font_draw_centered(fb, GFX_W / 2, 30, 4, white, "GAME OVER");
        snprintf(buf, sizeof(buf), "TOWER %d", score());
        font_draw_centered(fb, GFX_W / 2, 60, 2, white, buf);
        if (now - s_over_at > RESTART_DELAY_MS) font_draw_centered(fb, GFX_W / 2, 80, 2, gray, "TAP TO PLAY");
    }
}

const scene_t scene_tower = {enter, update, draw, 0};
