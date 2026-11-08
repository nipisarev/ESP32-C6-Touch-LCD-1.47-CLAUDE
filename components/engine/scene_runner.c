#include <math.h>
#include <stdio.h>

#include "font.h"
#include "gfx.h"
#include "mascot_pose.h"
#include "scenes.h"

#define GROUND 150.0f
#define SCALE 0.5f
#define MX 30.0f          /* mascot viewBox origin x */
#define JUMP_PX 62.0f
#define MAX_OBS 6
#define RESTART_DELAY_MS 700

typedef enum { READY, RUN, OVER } state_t;
typedef struct {
    float x, w, h;
    bool active;
} obstacle_t;

static state_t s_state;
static float s_t, s_dist, s_speed, s_jump_t, s_land_t, s_gap;
static obstacle_t s_obs[MAX_OBS];
static int32_t s_best;
static uint32_t s_over_at;

/* The jump keeps the original curve: sine.out up over 0.42 s, a short hang, power3.in down. */
static float jump_height(float t)
{
    if (t < 0) return 0;
    if (t < 0.42f) return JUMP_PX * sinf(t / 0.42f * (float)M_PI / 2);
    if (t < 0.5f) return JUMP_PX;
    if (t < 0.7f) {
        float p = (t - 0.5f) / 0.2f;
        return JUMP_PX * (1 - p * p * p * p);
    }
    return 0;
}

static int score(void) { return (int)(s_dist / 10); }

static void reset(void)
{
    s_t = s_dist = 0;
    s_speed = 130;
    s_jump_t = -1;
    s_land_t = -1;
    s_gap = 260;
    for (int i = 0; i < MAX_OBS; i++) s_obs[i].active = false;
}

static void enter(uint32_t now)
{
    (void)now;
    s_best = app_load("run_best", 0);
    s_state = READY;
    reset();
}

static void jump(void)
{
    if (s_jump_t < 0) s_jump_t = 0;
}

static void spawn(void)
{
    for (int i = 0; i < MAX_OBS; i++)
        if (!s_obs[i].active) {
            s_obs[i] = (obstacle_t){GFX_W + 4, rng_float(12, 24), rng_float(14, 30), true};
            float lo = s_speed * 0.95f < 160 ? 160 : s_speed * 0.95f;
            s_gap = rng_float(lo, s_speed * 1.6f + 40);
            return;
        }
}

static void update(const input_t *in, uint32_t now, float dt)
{
    if (s_state == READY) {
        if (in->pressed) {
            s_state = RUN;
            jump();
        }
        return;
    }
    if (s_state == OVER) {
        if (in->pressed && now - s_over_at > RESTART_DELAY_MS) {
            reset();
            s_state = RUN;
        }
        return;
    }
    if (in->pressed) jump();
    s_t += dt;
    s_speed = fminf(130 + 7 * s_t, 300);
    s_dist += s_speed * dt;
    if (s_jump_t >= 0) {
        s_jump_t += dt;
        if (s_jump_t >= 0.7f) {
            s_jump_t = -1;
            s_land_t = s_t;
        }
    }
    float rightmost = -1e9f;
    for (int i = 0; i < MAX_OBS; i++)
        if (s_obs[i].active) {
            s_obs[i].x -= s_speed * dt;
            if (s_obs[i].x + s_obs[i].w < 0) s_obs[i].active = false;
            else if (s_obs[i].x > rightmost) rightmost = s_obs[i].x;
        }
    if (rightmost < GFX_W - s_gap) spawn();

    /* Forgiving hitbox: body and legs, inset by 3 px. */
    float h = jump_height(s_jump_t), top = GROUND - MASCOT_H * SCALE - h;
    float x0 = MX + 11 * SCALE + 3, x1 = MX + 96 * SCALE - 3, y0 = top + 3, y1 = GROUND - h - 1;
    for (int i = 0; i < MAX_OBS; i++) {
        const obstacle_t *o = &s_obs[i];
        if (o->active && x1 > o->x && x0 < o->x + o->w && y1 > GROUND - o->h && y0 < GROUND) {
            s_state = OVER;
            s_over_at = now;
            if (score() > s_best) {
                s_best = score();
                app_save("run_best", s_best);
            }
        }
    }
}

static void draw(uint16_t *fb, uint32_t now)
{
    char buf[24];
    gfx_clear(fb, GFX_HEX(0x000000));
    gfx_fill_rect(fb, 0, (int)GROUND, GFX_W, 2, gfx_color(0x505050));
    for (int i = 0; i < 8; i++) { /* pebbles scroll with the ground */
        float x = fmodf(i * 47.0f - s_dist + 1000 * GFX_W, GFX_W + 20) - 10;
        gfx_fill_rect(fb, (int)x, (int)GROUND + 6 + (i % 3) * 5, 3, 2, gfx_color(0x404040));
    }
    for (int i = 0; i < MAX_OBS; i++) {
        const obstacle_t *o = &s_obs[i];
        if (!o->active) continue;
        gfx_fill_box(fb, o->x, GROUND - o->h, o->x + o->w, GROUND, gfx_color(0x3D74B8), 0);
        gfx_fill_box(fb, o->x, GROUND - o->h, o->x + o->w, GROUND - o->h + 3, gfx_color(0x7CA4D0), 0);
    }

    mascot_pose_t p = MASCOT_POSE_REST;
    float h = jump_height(s_jump_t);
    if (s_state == RUN && s_jump_t < 0) { /* stride: legs 1+3 and 2+4 alternate every 0.1 s */
        float ph = fmodf(s_t, 0.2f) / 0.1f;
        int pair = ph < 1 ? 0 : 1;
        float lift = sinf((ph - pair) * (float)M_PI);
        p.leg_lift[pair] = p.leg_lift[pair + 2] = lift;
    } else if (s_jump_t >= 0) {
        p.leg_lift[0] = p.leg_lift[1] = p.leg_lift[2] = p.leg_lift[3] = 0.35f;
        p.eye_dy = -2;
    }
    if (s_land_t >= 0 && s_t - s_land_t < 0.13f) p.hands_dy = 6; /* landing bounce */
    if (s_state == OVER) {
        p.eye_open = 0.15f;
        p.hands_dy = 8;
    }
    mascot_pose_draw(fb, MX, GROUND - MASCOT_H * SCALE - h, SCALE, &p);

    const uint16_t white = gfx_color(0xFFFFFF), gray = gfx_color(0x909090);
    snprintf(buf, sizeof(buf), "%d", score());
    font_draw(fb, GFX_W - 6 - font_width(buf, 3), 6, 3, white, buf);
    snprintf(buf, sizeof(buf), "BEST %d", (int)s_best);
    font_draw(fb, 6, 6, 2, gray, buf);
    if (s_state == READY && (now / 500) % 2 == 0) font_draw_centered(fb, GFX_W / 2, 50, 3, white, "TAP TO JUMP");
    if (s_state == OVER) {
        font_draw_centered(fb, GFX_W / 2, 34, 4, white, "GAME OVER");
        snprintf(buf, sizeof(buf), "SCORE %d", score());
        font_draw_centered(fb, GFX_W / 2, 64, 2, white, buf);
        if (now - s_over_at > RESTART_DELAY_MS) font_draw_centered(fb, GFX_W / 2, 84, 2, gray, "TAP TO PLAY");
    }
}

const scene_t scene_runner = {enter, update, draw, 0};
