#include <math.h>
#include <stdio.h>

#include "font.h"
#include "gfx.h"
#include "mascot_anim.h"
#include "mascot_pose.h"
#include "pet_coffee.h"
#include "room.h"
#include "scenes.h"
#include "toss.h"

#define GROUND 170.0f
#define CX 160.0f
#define OY (GROUND - MASCOT_H)
#define MAX_HEARTS 10
#define MAX_FX 24
#define SAVE_DEBOUNCE_MS 3000
#define GRAB_S 0.4f          /* finger resting this long on Claude picks it up */
#define STROKE_PX 100.0f     /* stroking distance per heart, summed over strokes */
#define HARD_LANDING 330.0f  /* px/s: faster than this and Claude lands dizzy */
#define N_TUFTS 12
#define COMB_REACH 16.0f
#define HOME_S 0.6f

typedef enum { M_PET, M_COMB, M_COFFEE } pet_mode_t; /* also the button order */
typedef enum { R_NONE, R_HOP, R_HAPPY, R_CONFETTI, R_FLAG, R_PAT, R_WAVE, R_FEET, R_LAUGH, R_COFFEE, R_DIZZY, R_COUNT } reaction_t;
#define N_BODY_REACTIONS 4 /* R_HOP..R_FLAG, picked at random for a tap on the body */
static const float k_duration[R_COUNT] = {0, 0.6f, 1.2f, 2.0f, 0.84f + 0.63f * 3, 0.8f, 1.2f, 1.0f, 1.5f, 2.4f, 3.4f};
typedef enum { F_NONE, F_OTHER, F_PENDING, F_STROKE, F_GRAB } finger_t; /* what the current touch is doing */
typedef enum { AIR_NONE, AIR_SHAKE, AIR_THROW } air_t;

typedef struct {
    float x, y, t; /* t < 0: not shown yet */
    bool active;
} heart_t;

enum { FX_SPARK, FX_HA };
typedef struct {
    float x, y, vx, vy, t, life;
    int kind;
    bool active;
} fx_t;

/* Where the fur can stick out: a point on the body edge (viewBox) and the outward direction. */
static const struct {
    float x, y;
    int dx, dy;
} k_tuft_at[N_TUFTS] = {
    {18, 0, 0, -1}, {31, 0, 0, -1}, {44, 0, 0, -1}, {57, 0, 0, -1}, {70, 0, 0, -1}, {84, 0, 0, -1},
    {11, 7, -1, 0}, {11, 15, -1, 0}, {11, 52, -1, 0}, {96, 7, 1, 0}, {96, 15, 1, 0}, {96, 52, 1, 0},
};

static const float k_btn[3][4] = {{6, 56, 36, 86}, {6, 92, 36, 122}, {6, 128, 36, 158}}; /* below the wall clock */

static pet_mode_t s_mode;
static reaction_t s_react;
static int s_wave_hand;
static float s_react_t, s_clock, s_blink_in, s_blink_t, s_glance_in, s_look_t, s_look_dx, s_look_dy, s_ha_in;
static heart_t s_hearts[MAX_HEARTS];
static fx_t s_fx[MAX_FX];
static toss_t s_toss;  /* position while thrown, held or dizzy: body centre x, height above the floor */
static air_t s_air;
static float s_air_peak;
static float s_home_t; /* walking back to the centre */
static finger_t s_finger;
static float s_finger_t, s_prev_x, s_prev_y, s_stroke_acc, s_grab_dx, s_grab_dy, s_vx, s_vy, s_still_t;
static bool s_stroking, s_comb_down, s_comb_on_body;
static float s_comb_x, s_comb_y;
static float s_taps[3]; /* times of the last taps on Claude (tickling) */
static bool s_tuft[N_TUFTS];
static int s_tuft_v[N_TUFTS];
static float s_mess_in;
static int32_t s_count;
static uint32_t s_dirty_at;
static bool s_dirty;

static float body_x(void)
{
    if (s_air != AIR_NONE || s_finger == F_GRAB || s_react == R_DIZZY) return s_toss.x;
    if (s_home_t >= 0) return lerpf(s_toss.x, CX, ease_inout2(s_home_t / HOME_S));
    return CX;
}
static float body_lift(void) { return s_air != AIR_NONE || s_finger == F_GRAB ? s_toss.y : 0; }
static float body_ox(void) { return body_x() - MASCOT_W / 2; }
static float body_oy(void) { return OY + body_lift(); }

static bool on_mascot(float x, float y)
{
    float ox = body_ox(), oy = body_oy();
    return x >= ox - 6 && x <= ox + MASCOT_W + 6 && y >= oy - 6 && y <= oy + MASCOT_H + 6;
}

static int button_at(float x, float y)
{
    for (int i = 0; i < 3; i++)
        if (x >= k_btn[i][0] - 4 && x <= k_btn[i][2] + 4 && y >= k_btn[i][1] - 4 && y <= k_btn[i][3] + 4) return i;
    return -1;
}

static void add_hearts(int n, uint32_t now)
{
    s_count += n;
    s_dirty = true;
    s_dirty_at = now;
    for (int k = 0; k < n; k++)
        for (int i = 0; i < MAX_HEARTS; i++)
            if (!s_hearts[i].active) {
                s_hearts[i] = (heart_t){body_x() + rng_float(-30, 30), body_oy() - 14, -0.18f * k, true};
                break;
            }
}

static void add_fx(int kind, float x, float y, float vx, float vy, float life)
{
    for (int i = 0; i < MAX_FX; i++)
        if (!s_fx[i].active) {
            s_fx[i] = (fx_t){x, y, vx, vy, 0, life, kind, true};
            return;
        }
}

static void sparkles(float x, float y, int n, float spread)
{
    for (int k = 0; k < n; k++) add_fx(FX_SPARK, x + rng_float(-spread, spread), y + rng_float(-spread, spread), rng_float(-40, 40), rng_float(-50, 10), rng_float(0.4f, 0.6f));
}

static void mess_add(int n)
{
    for (int k = 0; k < n; k++)
        for (int tries = 0; tries < N_TUFTS; tries++) {
            int i = (int)(rng_next() % N_TUFTS);
            if (s_tuft[i]) continue;
            s_tuft[i] = true;
            s_tuft_v[i] = (int)(rng_next() % 4);
            break;
        }
}

static void walk_home(void) { s_home_t = fabsf(s_toss.x - CX) > 4 ? 0 : -1; }

static void start_reaction(reaction_t r)
{
    s_react = r;
    s_react_t = 0;
}

static void enter(uint32_t now)
{
    (void)now;
    s_count = app_load("hearts", 0);
    s_mode = M_PET;
    s_react = R_NONE;
    s_air = AIR_NONE;
    s_finger = F_NONE;
    s_home_t = -1;
    s_stroking = s_comb_down = s_comb_on_body = false;
    toss_start(&s_toss, CX);
    s_clock = 0;
    s_blink_in = rng_float(1.5f, 3.5f);
    s_blink_t = -1;
    s_glance_in = rng_float(3, 6);
    s_look_t = 0;
    for (int i = 0; i < MAX_HEARTS; i++) s_hearts[i].active = false;
    for (int i = 0; i < MAX_FX; i++) s_fx[i].active = false;
    for (int i = 0; i < 3; i++) s_taps[i] = -10;
    for (int i = 0; i < N_TUFTS; i++) s_tuft[i] = false;
    mess_add(3 + (int)(rng_next() % 2));
    s_mess_in = rng_float(40, 70);
}

static void set_mode(pet_mode_t m)
{
    if (m == s_mode) return;
    if (m == M_COFFEE) { /* first person: the room and Claude's position reset */
        coffee_start();
        s_air = AIR_NONE;
        s_react = R_NONE;
        s_home_t = -1;
    }
    s_finger = F_NONE;
    s_stroking = s_comb_down = s_comb_on_body = false;
    s_mode = m;
}

static void look_at(float x, float y, float hold)
{
    s_look_dx = clampf((x - body_x()) / 25, -4, 4);
    s_look_dy = clampf((y - (body_oy() + 16)) / 25, -4, 4);
    s_look_t = hold;
}

static void tap_mascot(float x, float y, uint32_t now)
{
    add_hearts(1, now);
    s_taps[0] = s_taps[1];
    s_taps[1] = s_taps[2];
    s_taps[2] = s_clock;
    if (s_taps[2] - s_taps[0] < 1.2f && s_react != R_DIZZY) { /* tickled: keeps laughing while the taps go on */
        if (s_react != R_LAUGH) s_ha_in = 0;
        start_reaction(R_LAUGH);
        return;
    }
    if (s_react != R_NONE) return;
    const float ox = body_ox(), oy = body_oy();
    const mascot_pose_t rest = MASCOT_POSE_REST;
    for (int i = 0; i < 2; i++) {
        float x0, y0, x1, y1;
        mascot_hand_rect(ox, oy, 1, &rest, i, &x0, &y0, &x1, &y1);
        if (x >= x0 - 3 && x <= x1 + 3 && y >= y0 - 3 && y <= y1 + 3) {
            s_wave_hand = i;
            start_reaction(R_WAVE);
            return;
        }
    }
    float vy = y - oy;
    start_reaction(vy < 22 ? R_PAT : vy > 62 ? R_FEET : (reaction_t)(1 + rng_next() % N_BODY_REACTIONS));
}

static void grab_start(const input_t *in)
{
    float x = body_x(), lift = body_lift();
    toss_start(&s_toss, x);
    s_toss.y = lift;
    s_grab_dx = in->x - x;
    s_grab_dy = in->y - (OY + lift);
    s_vx = s_vy = s_still_t = 0;
    s_finger = F_GRAB;
    s_react = R_NONE;
    s_home_t = -1;
    s_stroking = false;
}

static void grab_move(const input_t *in, float dt)
{
    float x = clampf(in->x - s_grab_dx, MASCOT_W / 2, GFX_W - MASCOT_W / 2);
    float y = clampf(in->y - s_grab_dy - OY, MASCOT_H - GROUND, 0);
    if (x == s_toss.x && y == s_toss.y) {
        /* the touch panel repeats positions and the release is debounced: keep the last speed briefly */
        if ((s_still_t += dt) > 0.12f) s_vx = s_vy = 0;
        return;
    }
    s_still_t = 0;
    if (dt > 0) {
        float k = fminf(1, dt / 0.08f);
        s_vx += ((x - s_toss.x) / dt - s_vx) * k;
        s_vy += ((y - s_toss.y) / dt - s_vy) * k;
    }
    s_toss.x = x;
    s_toss.y = y;
}

static void comb_at(float x, float y, uint32_t now)
{
    const float ox = body_ox(), oy = body_oy();
    int left = 0;
    bool removed = false;
    for (int i = 0; i < N_TUFTS; i++) {
        if (!s_tuft[i]) continue;
        float tx = ox + k_tuft_at[i].x + k_tuft_at[i].dx * 5, ty = oy + k_tuft_at[i].y + k_tuft_at[i].dy * 5;
        if (hypotf(tx - x, ty - y) < COMB_REACH) {
            s_tuft[i] = false;
            removed = true;
            sparkles(tx, ty, 4, 3);
        } else {
            left++;
        }
    }
    if (removed && left == 0) { /* all smooth */
        add_hearts(1, now);
        if (s_react == R_NONE) start_reaction(R_HAPPY);
        sparkles(ox + MASCOT_W / 2, oy + 30, 8, 36);
    }
}

static void update(const input_t *in, uint32_t now, float dt)
{
    s_clock += dt;
    for (int i = 0; i < MAX_HEARTS; i++)
        if (s_hearts[i].active && (s_hearts[i].t += dt) > 0.9f) s_hearts[i].active = false;
    for (int i = 0; i < MAX_FX; i++) {
        fx_t *f = &s_fx[i];
        if (!f->active) continue;
        f->x += f->vx * dt;
        f->y += f->vy * dt;
        if ((f->t += dt) > f->life) f->active = false;
    }
    if (s_dirty && now - s_dirty_at > SAVE_DEBOUNCE_MS) { /* limit flash writes */
        app_save("hearts", s_count);
        s_dirty = false;
    }
    if (s_look_t > 0) s_look_t -= dt;
    if ((s_glance_in -= dt) <= 0) {
        s_look_dx = rng_float(-3, 3);
        s_look_dy = rng_float(-2, 2);
        s_look_t = 1.0f;
        s_glance_in = rng_float(3, 6);
    }
    if (s_blink_t >= 0 && (s_blink_t += dt) > 0.14f) s_blink_t = -1;
    if ((s_blink_in -= dt) <= 0) {
        s_blink_t = 0;
        s_blink_in = rng_float(2.5f, 5);
    }

    if (in->tap) {
        int b = button_at(in->start_x, in->start_y);
        if (b >= 0) {
            set_mode((pet_mode_t)b);
            return;
        }
    }
    if (s_mode == M_COFFEE) {
        if (coffee_update(in, dt)) {
            s_mode = M_PET;
            start_reaction(R_COFFEE);
            add_hearts(3, now);
        }
        return;
    }

    if (in->pressed) {
        s_finger = F_OTHER;
        if (s_mode == M_PET && s_air == AIR_NONE && button_at(in->x, in->y) < 0 && on_mascot(in->x, in->y)) {
            s_finger = F_PENDING;
            s_finger_t = 0;
        }
        s_prev_x = in->x;
        s_prev_y = in->y;
    }
    if ((s_mess_in -= dt) <= 0) { /* fur gets ruffled over time */
        mess_add(1);
        s_mess_in = rng_float(40, 70);
    }
    if (in->shake && s_air == AIR_NONE && s_finger != F_GRAB && s_react != R_DIZZY) {
        toss_start(&s_toss, body_x());
        s_air = AIR_SHAKE;
        s_react = R_NONE;
        s_home_t = -1;
        s_stroking = false;
    }
    if (s_air != AIR_NONE) {
        static const input_t calm; /* a throw flies on gravity alone */
        toss_update(&s_toss, s_air == AIR_SHAKE ? in : &calm, dt, MASCOT_W / 2, GFX_W - MASCOT_W / 2, MASCOT_H - GROUND);
        s_air_peak = fmaxf(s_air_peak, hypotf(s_toss.vx, s_toss.vy));
        if (toss_settled(&s_toss) && (s_air == AIR_THROW || in->shake_level < 0.3f)) {
            bool hard = s_air == AIR_SHAKE || s_air_peak > HARD_LANDING;
            s_air = AIR_NONE;
            if (hard) {
                start_reaction(R_DIZZY);
                mess_add(6 + (int)(rng_next() % 4));
            } else {
                walk_home();
            }
        }
        if (!in->down) s_finger = F_NONE;
        return;
    }

    if (s_mode == M_COMB) {
        s_comb_down = in->down;
        s_comb_on_body = false;
        if (in->down) {
            s_comb_x = in->x;
            s_comb_y = in->y;
            comb_at(in->x, in->y - 10, now);
            s_comb_on_body = on_mascot(in->x, in->y - 10);
            look_at(in->x, in->y - 10, 0.3f);
        }
    } else {
        if (s_finger == F_PENDING && in->down) {
            s_finger_t += dt;
            if (in->moved) s_finger = F_STROKE;
            else if (s_finger_t >= GRAB_S) grab_start(in);
        }
        if (s_finger == F_STROKE) {
            s_stroking = in->down && on_mascot(in->x, in->y);
            if (s_stroking && (s_stroke_acc += hypotf(in->x - s_prev_x, in->y - s_prev_y)) >= STROKE_PX) {
                s_stroke_acc -= STROKE_PX;
                add_hearts(1, now);
            }
        }
        if (s_finger == F_GRAB) {
            if (in->down) {
                grab_move(in, dt);
            } else { /* let go: thrown with the finger's speed */
                s_toss.vx = clampf(s_vx, -1200, 1200);
                s_toss.vy = clampf(s_vy, -1200, 1200);
                s_air_peak = hypotf(s_toss.vx, s_toss.vy);
                s_air = AIR_THROW;
                s_finger = F_NONE;
            }
        }
        if (in->tap) {
            if (s_finger == F_PENDING) tap_mascot(in->start_x, in->start_y, now);
            else if (s_finger == F_OTHER) look_at(in->start_x, in->start_y, 1.5f);
        }
    }
    if (!in->down && s_finger != F_GRAB) {
        s_finger = F_NONE;
        s_stroking = false;
    }
    s_prev_x = in->x;
    s_prev_y = in->y;

    if (s_home_t >= 0 && (s_home_t += dt) > HOME_S) s_home_t = -1;
    if (s_react != R_NONE && (s_react_t += dt) >= k_duration[s_react]) {
        if (s_react == R_DIZZY) walk_home();
        s_react = R_NONE;
    }
    if (s_react == R_LAUGH && (s_ha_in -= dt) <= 0) {
        add_fx(FX_HA, body_x() + rng_float(-50, 30), body_oy() - 4, rng_float(-15, 15), -45, 0.9f);
        s_ha_in = 0.35f;
    }
}

static bool menu_locked(void) { return s_mode == M_COMB || s_finger == F_PENDING || s_finger == F_STROKE || s_finger == F_GRAB; }

/* ---------- drawing ---------- */

static uint32_t pal(char c)
{
    switch (c) {
    case 'c': return 0xE0A040; /* comb */
    case 'C': return 0xB87828;
    case 'o': return 0x6F4E37;
    case 'w': return 0xF2F2F2;
    case 'u': return 0x3D74B8;
    default: return 0;
    }
}

static void sprite(uint16_t *fb, const char *const *rows, int n, float x, float y, float cell)
{
    for (int r = 0; r < n; r++)
        for (int c = 0; rows[r][c]; c++)
            if (rows[r][c] != '.') gfx_fill_box(fb, x + c * cell, y + r * cell, x + (c + 1) * cell, y + (r + 1) * cell, gfx_color(pal(rows[r][c])), 0);
}

static const char *const k_comb[] = {"CCCCCCCCCCC", "ccccccccccc", "c.c.c.c.c.c", "c.c.c.c.c.c", "c.c.c.c.c.c"};
static const char *const k_mug_icon[] = {"oooooo..", "wwwwwwww", "wuuuuw.w", "wwwwwwww", "wwwwww..", ".wwww..."};

/* A ruffled tuft: three 3 px strands sticking out of the body edge at (x, y) in direction (dx, dy). */
static void tuft_draw(uint16_t *fb, float x, float y, int dx, int dy, int v)
{
    static const int len[4][3] = {{2, 3, 2}, {3, 2, 1}, {1, 3, 3}, {2, 4, 2}};
    const uint16_t c = gfx_color(MASCOT_RGB);
    for (int k = 0; k < 3; k++) {
        float off = (k - 1) * 4.0f;
        int lean = k - 1 + (v & 1);
        for (int i = 0; i < len[v][k]; i++) {
            float along = off + lean * i * 1.5f, out = 1.5f + i * 3.0f;
            float px = x + (dx ? dx * out : along), py = y + (dy ? dy * out : along);
            gfx_fill_box(fb, px - 1.5f, py - 1.5f, px + 1.5f, py + 1.5f, c, 0);
        }
    }
}

static void steam(uint16_t *fb, float x, float y, float t)
{
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 3; k++) {
            float ph = fmodf(t * 1.3f + i * 0.33f + k * 0.33f, 1.0f);
            if (ph > 0.85f) continue;
            float sx = x + (i - 1) * 12 + 3 * sinf(ph * 6.28f + i);
            gfx_fill_box(fb, sx - 1.5f, y - ph * 26 - 1.5f, sx + 1.5f, y - ph * 26 + 1.5f, gfx_color(0xD0D0D8), 0);
        }
}

static void draw_ui(uint16_t *fb)
{
    char buf[16];
    draw_heart(fb, 6, 8, 2, gfx_color(0xE8506E));
    snprintf(buf, sizeof(buf), "%d", (int)s_count);
    font_draw(fb, 24, 8, 2, gfx_color(0xFFFFFF), buf);
    for (int i = 0; i < 3; i++) {
        const float *b = k_btn[i];
        const bool on = (int)s_mode == i;
        const uint16_t edge = gfx_color(on ? MASCOT_RGB : 0x4A4C58), fill = gfx_color(on ? 0x3A2A24 : 0x1E2026);
        const float e = on ? 2 : 1;
        gfx_fill_box(fb, b[0] + 2, b[1], b[2] - 2, b[3], edge, 0); /* rounded corners */
        gfx_fill_box(fb, b[0], b[1] + 2, b[2], b[3] - 2, edge, 0);
        gfx_fill_box(fb, b[0] + e + 1, b[1] + e, b[2] - e - 1, b[3] - e, fill, 0);
        gfx_fill_box(fb, b[0] + e, b[1] + e + 1, b[2] - e, b[3] - e - 1, fill, 0);
        const float cx = (b[0] + b[2]) / 2, cy = (b[1] + b[3]) / 2;
        if (i == M_PET) draw_heart(fb, cx - 7, cy - 6, 2, gfx_color(0xE8506E));
        else if (i == M_COMB) sprite(fb, k_comb, 5, cx - 11, cy - 5, 2);
        else sprite(fb, k_mug_icon, 6, cx - 10, cy - 7.5f, 2.5f);
    }
}

static void draw(uint16_t *fb, uint32_t now)
{
    if (s_mode == M_COFFEE) {
        coffee_draw(fb, s_clock);
        draw_ui(fb);
        return;
    }
    room_draw(fb, 1, 0, s_clock);
    const float px = body_x(), pox = px - MASCOT_W / 2, poy = body_oy(), t = s_react_t;

    if (s_react == R_CONFETTI) { /* the original animations, aligned so the body stays in place */
        manim_draw(fb, MANIM_CONFETTI, t, px - 64.5f, GROUND - 113, 1.0f);
    } else if (s_react == R_FLAG) {
        manim_draw(fb, MANIM_FLAG, t, px - 63.5f, GROUND - 146, 1.0f);
    } else {
        mascot_pose_t p = MASCOT_POSE_REST;
        float hop = 0, sway = 0;
        if (s_look_t > 0) {
            p.eye_dx = s_look_dx;
            p.eye_dy = s_look_dy;
        }
        if (s_blink_t >= 0) p.eye_open = fabsf(1 - s_blink_t / 0.07f);
        if (s_air != AIR_NONE) { /* flying */
            p.eye_open = 0.45f;
            p.hand_dy[0] = -10 * sinf(now * 0.03f);
            p.hand_dy[1] = 10 * sinf(now * 0.03f);
            if (s_toss.y < -2) p.leg_lift[0] = p.leg_lift[1] = p.leg_lift[2] = p.leg_lift[3] = 0.5f;
        } else if (s_finger == F_GRAB) { /* dangling from the finger */
            p.eye_open = 1;
            p.eye_dx = 0;
            p.eye_dy = 3;
            p.hands_dy = -8 + 2 * sinf(s_clock * 9);
            for (int k = 0; k < 4; k++) p.leg_lift[k] = 0.2f + 0.2f * sinf(s_clock * 7 + k * 1.6f);
        } else {
            switch (s_react) {
            case R_HOP: {
                float k = clampf(t / 0.5f, 0, 1);
                hop = 30 * sinf(k * (float)M_PI);
                p.happy = true;
                p.leg_lift[0] = p.leg_lift[1] = p.leg_lift[2] = p.leg_lift[3] = hop > 2 ? 0.4f : 0;
                break;
            }
            case R_HAPPY:
                p.happy = true;
                p.hands_dy = -10 * fabsf(sinf(t * (float)M_PI * 2.5f));
                p.body_dy = 2 * sinf(t * (float)M_PI * 5);
                break;
            case R_PAT: { /* squashed by the pat, then springs back */
                float sq = sinf(clampf(t / 0.8f, 0, 1) * (float)M_PI);
                p.happy = true;
                p.body_dy = 5 * sq;
                p.hands_dy = 3 * sq;
                break;
            }
            case R_WAVE: {
                float e = clampf(fminf(t / 0.2f, (1.2f - t) / 0.2f), 0, 1);
                p.hand_dy[s_wave_hand] = e * (-18 + 5 * sinf(t * 18));
                p.happy = t > 0.15f && t < 1.05f;
                p.eye_dx = s_wave_hand ? 3 : -3;
                break;
            }
            case R_FEET: /* ticklish feet */
                for (int k = 0; k < 4; k++) p.leg_lift[k] = sinf(t * 26 + k * 1.57f) > 0 ? 0.7f : 0;
                hop = 5 * fabsf(sinf(t * (float)M_PI * 3));
                p.happy = true;
                break;
            case R_LAUGH:
                p.happy = true;
                sway = 3 * sinf(t * 45);
                p.body_dy = 1.5f * sinf(t * 30);
                p.hand_dy[0] = -7 * fabsf(sinf(t * 20));
                p.hand_dy[1] = -7 * fabsf(cosf(t * 20));
                break;
            case R_COFFEE: /* pats its tummy */
                p.happy = true;
                p.hand_dx[0] = 6;
                p.hand_dx[1] = -6;
                p.hands_dy = 3 * fabsf(sinf(t * 8));
                break;
            case R_DIZZY:
                p.spiral = true;
                p.spiral_t = t;
                p.hands_dy = 4;
                sway = 5 * sinf(t * 2 * (float)M_PI / 1.3f);
                break;
            default:
                break;
            }
            if (s_react == R_NONE && (s_stroking || s_comb_on_body)) { /* eyes shut with pleasure */
                p.eye_open = 0.25f;
                p.eye_dx = p.eye_dy = 0;
                if (s_stroking) p.body_dy = 0.8f * sinf(s_clock * 60); /* purring */
            }
            if (s_home_t >= 0) {
                p.leg_lift[0] = p.leg_lift[2] = sinf(s_home_t * 31) > 0 ? 0.8f : 0;
                p.leg_lift[1] = p.leg_lift[3] = 0.8f - p.leg_lift[0];
            }
        }
        const float bx = pox + sway, by = poy - hop;
        mascot_pose_draw_body(fb, bx, by, 1, &p);
        for (int i = 0; i < N_TUFTS; i++)
            if (s_tuft[i]) tuft_draw(fb, bx + k_tuft_at[i].x, by + k_tuft_at[i].y + p.body_dy, k_tuft_at[i].dx, k_tuft_at[i].dy, s_tuft_v[i]);
        mascot_pose_draw_hands(fb, bx, by, 1, &p);
        if (s_react == R_DIZZY) {
            float fade = t < 0.4f ? t / 0.4f : t > 2.8f ? 1 - (t - 2.8f) / 0.6f : 1;
            draw_spark_orbit(fb, px + sway, poy, t, fade);
        }
        if (s_react == R_COFFEE) steam(fb, px, by - 6, t);
        if (s_stroking && fmodf(s_clock, 0.4f) < 0.25f) /* purr marks on both sides */
            for (int side = -1; side <= 1; side += 2)
                for (int k = 0; k < 3; k++) {
                    float x = px + side * (58 + k * 4), y = by + 8 + (k % 2 ? -2 : 2);
                    gfx_fill_box(fb, x - 1.5f, y - 1, x + 1.5f, y + 1, gfx_color(0xF0C0A0), 0);
                }
    }

    for (int i = 0; i < MAX_HEARTS; i++) {
        const heart_t *h = &s_hearts[i];
        if (!h->active || h->t < 0) continue;
        float cell = 3 - 1.5f * (h->t / 0.9f);
        draw_heart(fb, h->x - 3.5f * cell, h->y - h->t * 50, cell, gfx_color(0xE8506E));
    }
    for (int i = 0; i < MAX_FX; i++) {
        const fx_t *f = &s_fx[i];
        if (!f->active) continue;
        float k = 1 - f->t / f->life;
        if (f->kind == FX_HA) {
            font_draw(fb, (int)f->x, (int)f->y, 2, gfx_color(0xFFE070), "HA");
        } else {
            float r = 1 + 2.5f * k;
            uint16_t c = gfx_color(k > 0.5f ? 0xFFFFFF : 0xF7D860);
            gfx_fill_box(fb, f->x - r, f->y - 1, f->x + r, f->y + 1, c, 0);
            gfx_fill_box(fb, f->x - 1, f->y - r, f->x + 1, f->y + r, c, 0);
        }
    }
    if (s_mode == M_COMB && s_comb_down) sprite(fb, k_comb, 5, s_comb_x - 16.5f, s_comb_y - 21, 3); /* teeth just above the finger */
    draw_ui(fb);
}

const scene_t scene_pet = {enter, update, draw, menu_locked};
