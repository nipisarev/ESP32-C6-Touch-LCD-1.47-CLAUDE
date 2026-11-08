#include <math.h>
#include <stdio.h>

#include "font.h"
#include "gfx.h"
#include "mascot_pose.h"
#include "scenes.h"

#define GROUND 170.0f
#define SCALE 0.6f
#define ROUND_S 45.0f
#define PIECE 8.0f
#define MAX_PIECES 24
#define MAX_POPS 6
#define RESTART_DELAY_MS 700

typedef enum { READY, PLAY, OVER } state_t;
typedef struct {
    float x, y, vx, vy;
    uint32_t color;
    bool active;
} piece_t;
typedef struct {
    float x, y, t;
    bool active;
} pop_t;

static const uint32_t k_colors[] = {0x7CA4D0, 0xDD8361, 0xC87392, 0x3D74B8, 0xFFFFFF};

static state_t s_state;
static float s_t, s_cx, s_spawn_in, s_react_t, s_walk_t, s_vx;
static piece_t s_pieces[MAX_PIECES];
static pop_t s_pops[MAX_POPS];
static int s_score;
static int32_t s_best;
static uint32_t s_over_at;

static void reset(void)
{
    s_t = 0;
    s_score = 0;
    s_spawn_in = 0.3f;
    s_react_t = -1;
    for (int i = 0; i < MAX_PIECES; i++) s_pieces[i].active = false;
    for (int i = 0; i < MAX_POPS; i++) s_pops[i].active = false;
}

static void enter(uint32_t now)
{
    (void)now;
    s_best = app_load("catch_best", 0);
    s_cx = GFX_W / 2;
    s_state = READY;
    reset();
}

static void spawn(void)
{
    for (int i = 0; i < MAX_PIECES; i++)
        if (!s_pieces[i].active) {
            s_pieces[i] = (piece_t){rng_float(4, GFX_W - 4 - PIECE), -PIECE, rng_float(-12, 12), rng_float(70, 100) + s_t * 2.2f,
                                    k_colors[rng_next() % (sizeof(k_colors) / sizeof(k_colors[0]))], true};
            return;
        }
}

static void pop(float x, float y)
{
    for (int i = 0; i < MAX_POPS; i++)
        if (!s_pops[i].active) {
            s_pops[i] = (pop_t){x, y, 0, true};
            return;
        }
}

static void update(const input_t *in, uint32_t now, float dt)
{
    const float half = MASCOT_W * SCALE / 2;
    if (in->down) { /* follow the finger smoothly: filters the jittery panel */
        float target = clampf((float)in->x, half, GFX_W - half);
        float prev = s_cx;
        s_cx += (target - s_cx) * fminf(1, dt * 10);
        s_vx = dt > 0 ? (s_cx - prev) / dt : 0;
    } else s_vx = 0;
    if (fabsf(s_vx) > 25) s_walk_t += dt;

    for (int i = 0; i < MAX_POPS; i++)
        if (s_pops[i].active && (s_pops[i].t += dt) > 0.6f) s_pops[i].active = false;

    if (s_state == READY) {
        if (in->tap) s_state = PLAY;
        return;
    }
    if (s_state == OVER) {
        if (in->tap && now - s_over_at > RESTART_DELAY_MS) {
            reset();
            s_state = PLAY;
        }
        return;
    }

    s_t += dt;
    if (s_react_t >= 0 && (s_react_t += dt) > 0.35f) s_react_t = -1;
    if ((s_spawn_in -= dt) <= 0) {
        spawn();
        s_spawn_in = 0.75f - 0.43f * (s_t / ROUND_S);
    }
    const float top = GROUND - MASCOT_H * SCALE;
    for (int i = 0; i < MAX_PIECES; i++) {
        piece_t *pc = &s_pieces[i];
        if (!pc->active) continue;
        pc->x += pc->vx * dt;
        pc->y += pc->vy * dt;
        if (pc->y + PIECE >= top - 4 && pc->y <= top + 14 && pc->x + PIECE >= s_cx - half && pc->x <= s_cx + half) {
            pc->active = false;
            s_score++;
            s_react_t = 0;
            pop(pc->x, top - 14);
        } else if (pc->y > GFX_H) pc->active = false;
    }
    if (s_t >= ROUND_S) {
        s_state = OVER;
        s_over_at = now;
        for (int i = 0; i < MAX_PIECES; i++) s_pieces[i].active = false;
        if (s_score > s_best) {
            s_best = s_score;
            app_save("catch_best", s_best);
        }
    }
}

static void draw(uint16_t *fb, uint32_t now)
{
    char buf[24];
    const uint16_t white = gfx_color(0xFFFFFF), gray = gfx_color(0x909090);
    gfx_clear(fb, GFX_HEX(0x000000));

    float nearest_y = -1, nearest_x = s_cx;
    for (int i = 0; i < MAX_PIECES; i++) {
        const piece_t *pc = &s_pieces[i];
        if (!pc->active) continue;
        gfx_fill_box(fb, pc->x, pc->y, pc->x + PIECE, pc->y + PIECE, gfx_color(pc->color), 0);
        if (pc->y > nearest_y) {
            nearest_y = pc->y;
            nearest_x = pc->x + PIECE / 2;
        }
    }

    mascot_pose_t p = MASCOT_POSE_REST;
    p.eye_dx = clampf((nearest_x - s_cx) / 25, -4, 4); /* watch the lowest piece */
    p.eye_dy = s_state == PLAY ? -3 : 0;
    if (s_react_t >= 0) {
        p.happy = true;
        p.hands_dy = -14;
    }
    if (fabsf(s_vx) > 25) {
        float ph = fmodf(s_walk_t, 0.2f) / 0.1f;
        int pair = ph < 1 ? 0 : 1;
        p.leg_lift[pair] = p.leg_lift[pair + 2] = sinf((ph - pair) * (float)M_PI);
    }
    mascot_pose_draw(fb, s_cx - MASCOT_W * SCALE / 2, GROUND - MASCOT_H * SCALE, SCALE, &p);

    for (int i = 0; i < MAX_POPS; i++)
        if (s_pops[i].active) font_draw_centered(fb, (int)s_pops[i].x, (int)(s_pops[i].y - s_pops[i].t * 30), 2, white, "+1");

    snprintf(buf, sizeof(buf), "%d", s_score);
    font_draw(fb, 6, 8, 3, white, buf);
    if (s_state == PLAY) {
        gfx_fill_box(fb, 0, 0, GFX_W * (1 - s_t / ROUND_S), 3, gfx_color(0xDD8361), 0);
        snprintf(buf, sizeof(buf), "%d", (int)ceilf(ROUND_S - s_t));
        font_draw(fb, GFX_W - 6 - font_width(buf, 2), 8, 2, gray, buf);
    }
    if (s_state == READY) {
        font_draw_centered(fb, GFX_W / 2, 36, 3, white, "CATCH CONFETTI");
        if ((now / 500) % 2 == 0) font_draw_centered(fb, GFX_W / 2, 66, 2, gray, "DRAG TO MOVE. TAP TO START");
    }
    if (s_state == OVER) {
        font_draw_centered(fb, GFX_W / 2, 30, 4, white, "TIME!");
        snprintf(buf, sizeof(buf), "SCORE %d  BEST %d", s_score, (int)s_best);
        font_draw_centered(fb, GFX_W / 2, 60, 2, white, buf);
        if (now - s_over_at > RESTART_DELAY_MS) font_draw_centered(fb, GFX_W / 2, 80, 2, gray, "TAP TO PLAY");
    }
}

static bool menu_locked(void) { return s_state == PLAY; }

const scene_t scene_catch = {enter, update, draw, menu_locked};
