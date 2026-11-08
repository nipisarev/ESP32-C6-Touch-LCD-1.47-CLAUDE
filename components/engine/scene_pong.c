#include <math.h>
#include <stdio.h>

#include "font.h"
#include "gfx.h"
#include "mascot_pose.h"
#include "scenes.h"

/* The playing field is inset from the screen edges; everything is laid out relative to it. */
#define FX0 30.0f
#define FX1 314.0f
#define FY0 24.0f
#define FY1 168.0f
#define FCX ((FX0 + FX1) / 2)
#define PAD_X (FX0 + 10)
#define PAD_W 10.0f
#define PAD_H 44.0f
#define DRAG_GAIN 1.5f /* relative control: drag anywhere, the finger never covers the paddle */
#define BALL 6.0f
#define CLAUDE_S 0.42f
#define CLAUDE_X (FX1 - MASCOT_W * CLAUDE_S) /* the mascot is the right paddle */
#define WIN_SCORE 5
#define SERVE_DELAY 0.8f
#define RESTART_DELAY_MS 700

typedef enum { READY, PLAY, POINT, OVER } state_t;

static state_t s_state;
static float s_pad_y, s_cl_y, s_bx, s_by, s_vx, s_vy, s_speed, s_wait, s_aim, s_mood_t;
static int s_player, s_claude, s_serve_dir;
static float s_grab_finger, s_grab_pad;
static bool s_claude_scored;
static uint32_t s_over_at;

static const float CL_W = MASCOT_W * CLAUDE_S, CL_H = MASCOT_H * CLAUDE_S;

static void serve(void)
{
    s_bx = FCX - BALL / 2;
    s_by = (FY0 + FY1) / 2 - BALL / 2;
    s_speed = 150;
    float a = rng_float(-0.5f, 0.5f);
    s_vx = s_serve_dir * s_speed * cosf(a);
    s_vy = s_speed * sinf(a);
    s_aim = rng_float(-10, 10); /* Claude is beatable: it aims a little off */
    s_state = PLAY;
}

static void reset(void)
{
    s_player = s_claude = 0;
    s_serve_dir = -1;
    s_pad_y = s_cl_y = (FY0 + FY1) / 2;
    s_mood_t = -1;
    serve();
}

static void enter(uint32_t now)
{
    (void)now;
    reset();
    s_state = READY;
}

static void point(bool claude, uint32_t now)
{
    if (claude) s_claude++;
    else s_player++;
    s_claude_scored = claude;
    s_mood_t = 0;
    s_serve_dir = claude ? -1 : 1;
    if (s_claude >= WIN_SCORE || s_player >= WIN_SCORE) {
        s_state = OVER;
        s_over_at = now;
    } else {
        s_state = POINT;
        s_wait = SERVE_DELAY;
    }
}

static void bounce_off(float center_y, float half_h)
{
    float off = clampf((s_by + BALL / 2 - center_y) / half_h, -1, 1);
    s_speed = fminf(s_speed * 1.06f, 320);
    float dir = s_vx > 0 ? -1 : 1;
    s_vx = dir * s_speed * cosf(off * 0.8f);
    s_vy = s_speed * sinf(off * 0.8f);
}

static void update(const input_t *in, uint32_t now, float dt)
{
    if (in->pressed) {
        s_grab_finger = (float)in->y;
        s_grab_pad = s_pad_y;
    }
    if (in->down) {
        float target = clampf(s_grab_pad + (in->y - s_grab_finger) * DRAG_GAIN, FY0 + PAD_H / 2, FY1 - PAD_H / 2);
        s_pad_y += (target - s_pad_y) * fminf(1, dt * 16);
        if (target <= FY0 + PAD_H / 2 || target >= FY1 - PAD_H / 2) { /* re-anchor at the walls so reversing is instant */
            s_grab_finger = (float)in->y;
            s_grab_pad = target;
        }
    }
    if (s_mood_t >= 0 && (s_mood_t += dt) > 0.9f) s_mood_t = -1;

    /* Claude follows the ball when it comes its way, otherwise drifts to the middle. */
    float target = s_vx > 0 && s_state == PLAY ? s_by + BALL / 2 + s_aim : (FY0 + FY1) / 2;
    float step = clampf(target - s_cl_y, -135 * dt, 135 * dt);
    s_cl_y = clampf(s_cl_y + step, FY0 + CL_H / 2, FY1 - CL_H / 2);

    if (s_state == READY) {
        if (in->tap) s_state = PLAY;
        return;
    }
    if (s_state == OVER) {
        if (in->tap && now - s_over_at > RESTART_DELAY_MS) reset();
        return;
    }
    if (s_state == POINT) {
        if ((s_wait -= dt) <= 0) serve();
        return;
    }
    s_bx += s_vx * dt;
    s_by += s_vy * dt;
    if (s_by < FY0) { s_by = FY0; s_vy = fabsf(s_vy); }
    if (s_by > FY1 - BALL) { s_by = FY1 - BALL; s_vy = -fabsf(s_vy); }
    if (s_vx < 0 && s_bx <= PAD_X + PAD_W && s_bx + BALL >= PAD_X && fabsf(s_by + BALL / 2 - s_pad_y) <= PAD_H / 2 + BALL / 2) {
        s_bx = PAD_X + PAD_W;
        bounce_off(s_pad_y, PAD_H / 2);
    }
    if (s_vx > 0 && s_bx + BALL >= CLAUDE_X && s_bx <= CLAUDE_X + CL_W && fabsf(s_by + BALL / 2 - s_cl_y) <= CL_H / 2 + BALL / 2) {
        s_bx = CLAUDE_X - BALL;
        bounce_off(s_cl_y, CL_H / 2);
    }
    if (s_bx + BALL < FX0) point(true, now);
    if (s_bx > FX1) point(false, now);
}

static void draw(uint16_t *fb, uint32_t now)
{
    char buf[24];
    const uint16_t white = gfx_color(0xFFFFFF), gray = gfx_color(0x606060);
    gfx_clear(fb, GFX_HEX(0x000000));
    const uint16_t frame = gfx_color(0x404040);
    gfx_fill_box(fb, FX0 - 2, FY0 - 2, FX1 + 2, FY0, frame, 0); /* field border */
    gfx_fill_box(fb, FX0 - 2, FY1, FX1 + 2, FY1 + 2, frame, 0);
    gfx_fill_box(fb, FX0 - 2, FY0, FX0, FY1, frame, 0);
    gfx_fill_box(fb, FX1, FY0, FX1 + 2, FY1, frame, 0);
    for (float y = FY0 + 4; y < FY1 - 4; y += 14) gfx_fill_box(fb, FCX - 1, y, FCX + 1, fminf(y + 7, FY1 - 4), gray, 0);
    snprintf(buf, sizeof(buf), "%d   %d", s_player, s_claude);
    font_draw_centered(fb, (int)FCX, 2, 3, white, buf);

    { /* bevelled paddle */
        float y0 = s_pad_y - PAD_H / 2, y1 = s_pad_y + PAD_H / 2;
        gfx_fill_box(fb, PAD_X, y0, PAD_X + PAD_W, y1, gfx_color(0xD8D8D8), 0);
        gfx_fill_box(fb, PAD_X, y0, PAD_X + PAD_W, y0 + 2, white, 0);
        gfx_fill_box(fb, PAD_X, y0, PAD_X + 2, y1, white, 0);
        gfx_fill_box(fb, PAD_X + PAD_W - 2, y0 + 2, PAD_X + PAD_W, y1, gfx_color(0x808080), 0);
        gfx_fill_box(fb, PAD_X + 2, y1 - 2, PAD_X + PAD_W, y1, gfx_color(0x808080), 0);
    }
    if (s_state == PLAY) gfx_fill_box(fb, s_bx, s_by, s_bx + BALL, s_by + BALL, white, 0);

    mascot_pose_t p = MASCOT_POSE_REST;
    float mx = CLAUDE_X + CL_W / 2;
    p.eye_dx = clampf((s_bx - mx) / 30, -4, 4);
    p.eye_dy = clampf((s_by - s_cl_y) / 20, -4, 4);
    if (s_mood_t >= 0) {
        if (s_claude_scored) {
            p.happy = true;
            p.hands_dy = -12;
        } else {
            p.eye_open = 0.15f;
            p.hands_dy = 6;
        }
    }
    mascot_pose_draw(fb, CLAUDE_X, s_cl_y - CL_H / 2, CLAUDE_S, &p);

    if (s_state == READY) {
        font_draw_centered(fb, (int)FCX, 60, 3, white, "PONG");
        if ((now / 500) % 2 == 0) font_draw_centered(fb, (int)FCX, 92, 2, white, "DRAG ANYWHERE. TAP TO START");
    }
    if (s_state == OVER) {
        font_draw_centered(fb, (int)FCX - 12, 56, 4, white, s_player > s_claude ? "YOU WIN!" : "CLAUDE WINS!");
        if (now - s_over_at > RESTART_DELAY_MS) font_draw_centered(fb, (int)FCX, 92, 2, gfx_color(0x909090), "TAP TO PLAY");
    }
}

static bool menu_locked(void) { return s_state == PLAY || s_state == POINT; }

const scene_t scene_pong = {enter, update, draw, menu_locked};
