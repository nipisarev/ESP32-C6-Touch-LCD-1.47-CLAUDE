#include "acts.h"

#include <math.h>
#include <string.h>

#include "ball.h"
#include "gfx.h"
#include "mascot_pose.h"
#include "room.h"
#include "scenes.h"

/* ---------- pixel sprites ---------- */

static uint32_t pal(char c)
{
    switch (c) {
    case 'K': return 0x5C5C6E; /* phone body */
    case 'g': return 0x9BD35A; /* lit LCD */
    case 'w': return 0xF2F2F2;
    case 'u': return 0x3D74B8;
    case 'o': return 0x6F4E37; /* coffee */
    case 'r': return 0xC0503A; /* sausage */
    case 'm': return 0xF0D040; /* mustard */
    case 'n': return 0xE0B070; /* bun */
    case 'p': return 0xE58A9A; /* straw */
    case 'l': return 0xB8C0C8; /* cup edge */
    case 'G': return 0x5DBB3A; /* smoothie */
    case 'x': return 0x2A2A30; /* empty cup */
    default: return 0;
    }
}

static void sprite(uint16_t *fb, const char *const *rows, int n, float x, float y, float cell, int max_cols)
{
    for (int r = 0; r < n; r++)
        for (int c = 0; rows[r][c] && c < max_cols; c++)
            if (rows[r][c] != '.') gfx_fill_box(fb, x + c * cell, y + r * cell, x + (c + 1) * cell, y + (r + 1) * cell, gfx_color(pal(rows[r][c])), 0);
}

static const char *const k_mug[] = {"oooooo..", "wwwwwwww", "wuuuuw.w", "wwwwwwww", "wwwwww..", ".wwww..."};
static const char *const k_phone[] = {".K.....", ".K.....", ".K.....", ".K.....", "KKKKKKK", "KgggggK", "KgggggK", "KgggggK",
                                      "KKKKKKK", "KwKwKwK", "KKKKKKK", "KwKwKwK", "KKKKKKK", "KwKwKwK", "KKKKKKK", "KKKKKKK"};
static const char *const k_note[] = {"..ww", "..w.", "..w.", "www.", "ww.."};
static const char *const k_hotdog[] = {"..rrrrrrrrrr..", "nnrmrmrmrmrmnn", "nnnnnnnnnnnnnn", ".nnnnnnnnnnnn."};

static float blink(float t)
{
    float p = fmodf(t + 1.3f, 3.4f);
    return p < 0.14f ? fabsf(1 - p / 0.07f) : 1;
}

/* Piecewise helpers: 0..1 progress of t inside [a, a + d]. */
static float seg(float t, float a, float d) { return clampf((t - a) / d, 0, 1); }

#define OX (cx - 53.5f)
#define OY (ground - MASCOT_H)

/* ---------- coffee ---------- */

static void steam(uint16_t *fb, float x, float y, float t)
{
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 4; k++) {
            float ph = fmodf(t * 1.4f + i * 0.33f + k * 0.25f, 1.0f);
            if (ph > 0.85f) continue;
            float sx = x + 4 + i * 9 + 3 * sinf(ph * 6.28f + i);
            gfx_fill_box(fb, sx, y - 4 - ph * 28, sx + 3, y - 1 - ph * 28, gfx_color(0xD0D0D8), 0);
        }
}

static void coffee(uint16_t *fb, float t, float cx, float ground)
{
    mascot_pose_t p = MASCOT_POSE_REST;
    float lift = 0; /* 0 hold, 1 at the face */
    const float sips[2][2] = {{2.4f, 3.0f}, {6.4f, 7.0f}}; /* raise start, sip start */
    bool sipping = false;
    for (int k = 0; k < 2; k++) {
        float a = sips[k][0];
        if (t >= a && t < a + 2.0f) {
            lift = t < a + 0.6f ? ease_inout2(seg(t, a, 0.6f)) : t < a + 1.4f ? 1 : 1 - ease_inout2(seg(t, a + 1.4f, 0.6f));
            sipping = t >= a + 0.6f && t < a + 1.4f;
        }
    }
    float down = t < 0.6f ? 1 - ease_out2(t / 0.6f) : t > 8.4f ? ease_in2(seg(t, 8.4f, 0.6f)) : 0;
    p.hand_dx[1] = -30; /* hold the mug in front of the body: steam rises between the eyes */
    p.hand_dy[1] = 10 + 14 * down - 22 * lift;
    p.eye_open = blink(t);
    p.happy = sipping || (t > 5.0f && t < 5.8f);
    p.eye_dx = sipping ? 0 : 2;
    mascot_pose_draw_body(fb, OX, OY, 1, &p);
    float x0, y0, x1, y1;
    mascot_hand_rect(OX, OY, 1, &p, 1, &x0, &y0, &x1, &y1);
    float mx = x0 - 24, my = y0 + 2;
    sprite(fb, k_mug, 6, mx, my, 4, 99);
    if (!sipping && lift < 0.2f && down < 0.5f) steam(fb, mx, my, t);
    mascot_pose_draw_hands(fb, OX, OY, 1, &p);
}

/* ---------- phone ---------- */

static void phone(uint16_t *fb, float t, float cx, float ground)
{
    mascot_pose_t p = MASCOT_POSE_REST;
    const bool ringing = t > 0.5f && t < 2.0f, talking = t >= 2.6f && t < 7.6f;
    float hand = t < 0.5f ? lerpf(14, 4, ease_out2(t / 0.5f))
               : t < 2.0f ? 4
               : t < 2.6f ? lerpf(4, -14, ease_inout2(seg(t, 2.0f, 0.6f)))
               : t < 7.6f ? -14
               : t < 8.2f ? lerpf(-14, 4, ease_inout2(seg(t, 7.6f, 0.6f)))
                          : lerpf(4, 16, ease_in2(seg(t, 8.2f, 0.8f)));
    p.hand_dy[1] = hand;
    p.eye_open = blink(t);
    if (ringing) {
        p.eye_dx = 3;
        p.eye_dy = 3;
    }
    if (talking) {
        p.eye_dx = 3 * sinf(t * 1.7f);
        p.hand_dy[0] = -8 * fmaxf(0, sinf(t * 4.2f)) * (sinf(t * 0.9f) > 0 ? 1 : 0.3f);
        if (t > 5.0f && t < 5.7f) { /* laughs */
            p.happy = true;
            p.body_dy = fmodf(t, 0.2f) < 0.1f ? -1 : 0;
        }
    }
    mascot_pose_draw(fb, OX, OY, 1, &p);
    float x0, y0, x1, y1;
    mascot_hand_rect(OX, OY, 1, &p, 1, &x0, &y0, &x1, &y1);
    float px = x0 + 2 + (ringing && fmodf(t, 0.1f) < 0.05f ? 1 : 0), py = y0 - 20;
    sprite(fb, k_phone, 16, px, py, 3, 99);
    if (ringing)
        for (int k = 0; k < 2; k++) {
            float ph = fmodf(t * 1.4f + k * 0.5f, 1.0f);
            sprite(fb, k_note, 5, px + (k ? 24 : -16), py + 10 - ph * 26, 3, 99);
        }
    if (talking && !p.happy) { /* chatter bubble on the free side */
        int dots = (int)(t / 0.3f) % 4;
        for (int i = 0; i < dots; i++) gfx_fill_box(fb, OX + 4 + i * 9, OY - 14, OX + 9 + i * 9, OY - 9, gfx_color(0xFFFFFF), 0);
    }
}

/* ---------- hot dog and smoothie ---------- */

static void snack(uint16_t *fb, float t, float cx, float ground)
{
    static const float bites[4] = {1.0f, 3.8f, 6.2f, 7.8f}, sips[3] = {2.4f, 5.2f, 8.8f};
    mascot_pose_t p = MASCOT_POSE_REST;
    float down = t < 0.6f ? 1 - ease_out2(t / 0.6f) : t > 9.7f ? ease_in2(seg(t, 9.7f, 0.3f)) : 0;
    p.hand_dy[0] = p.hand_dy[1] = 14 * down;
    int bitten = 0, sipped = 0;
    bool chewing = false, drinking = false;
    for (int k = 0; k < 4; k++) {
        float a = bites[k];
        if (t >= a && t < a + 0.8f) p.hand_dy[0] -= 12 * (t < a + 0.3f ? ease_out2(seg(t, a, 0.3f)) : t < a + 0.5f ? 1 : 1 - ease_inout2(seg(t, a + 0.5f, 0.3f)));
        if (t >= a + 0.3f) bitten = k + 1;
        if (t >= a + 0.5f && t < a + 1.3f) chewing = true;
    }
    float level = 3;
    for (int k = 0; k < 3; k++) {
        float a = sips[k];
        if (t >= a && t < a + 0.9f) {
            p.hand_dy[1] -= 14 * (t < a + 0.3f ? ease_out2(seg(t, a, 0.3f)) : t < a + 0.6f ? 1 : 1 - ease_inout2(seg(t, a + 0.6f, 0.3f)));
            drinking = t >= a + 0.3f && t < a + 0.6f;
        }
        level -= seg(t, a + 0.3f, 0.3f);
        if (t >= a + 0.6f) sipped = k + 1;
    }
    (void)sipped;
    p.eye_open = blink(t);
    p.happy = chewing || drinking || t > 9.4f;
    if (chewing) p.body_dy = fmodf(t, 0.24f) < 0.12f ? -1 : 0;
    mascot_pose_draw(fb, OX, OY, 1, &p);

    float x0, y0, x1, y1;
    mascot_hand_rect(OX, OY, 1, &p, 0, &x0, &y0, &x1, &y1);
    int cols = bitten >= 4 ? 0 : 14 - 3 * bitten;
    sprite(fb, k_hotdog, 4, x1 - 6, y0 + 6, 3, cols);
    if (chewing) /* crumbs */
        for (int k = 0; k < 2; k++) {
            float ph = fmodf(t * 2.5f + k * 0.5f, 1.0f);
            gfx_fill_box(fb, x1 - 6 + cols * 3 + k * 4, y0 + 16 + ph * 30, x1 - 3 + cols * 3 + k * 4, y0 + 19 + ph * 30, gfx_color(0xE0B070), 0);
        }

    mascot_hand_rect(OX, OY, 1, &p, 1, &x0, &y0, &x1, &y1);
    char rows[9][7] = {"....p.", "...p..", "..p...", ".wwww.", "wwwwww", ".lxxl.", ".lxxl.", ".lxxl.", "..ll.."};
    for (int r = 5; r < 8; r++)
        if (r >= 8 - (int)ceilf(level - 0.01f)) rows[r][2] = rows[r][3] = 'G';
    const char *rp[9];
    for (int r = 0; r < 9; r++) rp[r] = rows[r];
    sprite(fb, rp, 9, x0 - 8, y0 - 4, 4, 99);
}

/* ---------- notebook ---------- */

#define BOOK_X0 12.0f
#define BOOK_X1 95.0f
#define SPINE 53.5f
#define LINE_Y(k) (38.0f + 5.0f * (k))

static void pencil(uint16_t *fb, float tip_x, float tip_y, bool eraser_down)
{
    static const uint32_t k_cols[11] = {0x333338, 0xE8C89A, 0xE8C89A, 0xF2C230, 0xF2C230, 0xF2C230, 0xF2C230, 0xF2C230, 0xF2C230, 0xB0B0B8, 0xE58A9A};
    for (int i = 10; i >= 0; i--) {
        uint32_t c = k_cols[eraser_down ? 10 - i : i];
        gfx_fill_box(fb, tip_x + 2 * i, tip_y - 2 * i - 3, tip_x + 2 * i + 3, tip_y - 2 * i, gfx_color(c), 0);
    }
}

static void notebook(uint16_t *fb, float t, float cx, float ground)
{
    static const float k_left[5] = {30, 26, 32, 18, 28}, k_right[3] = {30, 24, 28};
    mascot_pose_t p = MASCOT_POSE_REST;
    float lift = t < 0.6f ? 22 * (1 - ease_out2(t / 0.6f)) : t > 11.4f ? 22 * ease_in2(seg(t, 11.4f, 0.6f)) : 0;
    float close = t > 10.8f ? ease_inout2(seg(t, 10.8f, 0.6f)) : 0; /* 1 = closed */
    p.hands_dy = 8 + lift;
    p.hand_dx[0] = 10 * (1 - close) + 30 * close;
    p.hand_dx[1] = -10 * (1 - close) - 30 * close;
    p.eye_open = blink(t);
    p.eye_dy = 5;
    if (t > 0.6f && t < 4.6f) p.eye_dx = -3 + 6 * fmodf(t, 0.8f) / 0.8f; /* reading along the lines */

    /* writing / erasing schedule on the right page */
    float written[3] = {0, 0, 0};
    bool has_pencil = t > 4.6f && t < 10.8f, eraser = t >= 8.6f && t < 9.8f;
    float tip_x = OX + SPINE + 4, tip_y = OY + LINE_Y(0) + 8;
    for (int k = 0; k < 3; k++) {
        float a = 4.6f + 1.3f * k, f = seg(t, a, 1.1f);
        written[k] = k_right[k] * f;
        if (t >= a && t < a + 1.3f) {
            tip_x = OX + SPINE + 4 + written[k];
            tip_y = OY + LINE_Y(k) + 8 + lift;
        }
    }
    if (t >= 8.6f) { /* rub out the last line, then write a shorter one */
        float e = seg(t, 8.6f, 1.2f);
        written[2] = t < 9.8f ? k_right[2] * (1 - e) : 20 * seg(t, 9.8f, 1.0f);
        tip_x = OX + SPINE + 4 + written[2] + (eraser ? 3 * sinf(t * 25) : 0);
        tip_y = OY + LINE_Y(2) + 8 + lift;
    }
    if (has_pencil) {
        p.hand_dx[1] = tip_x + 12 - (OX + 85);
        p.hand_dy[1] = tip_y - 34 - (OY + 21 + p.hands_dy);
        p.eye_dx = clampf((tip_x - cx) / 8, -4, 4);
    }
    mascot_pose_draw_body(fb, OX, OY, 1, &p);

    /* the book (cover, pages, ruled lines, text); it folds towards the spine when closing */
    float w = 1 - close, by = OY + lift;
    float bx0 = OX + SPINE - (SPINE - BOOK_X0) * w - 2 * close, bx1 = OX + SPINE + (BOOK_X1 - SPINE) * w + 2 * close;
    gfx_fill_box(fb, bx0, by + 33, bx1, by + 63, gfx_color(0xB04040), 0);
    if (w > 0.05f) {
        float l0 = OX + SPINE - 38.5f * w, r1 = OX + SPINE + 38.5f * w;
        gfx_fill_box(fb, l0, by + 34, OX + SPINE - 0.5f, by + 61, gfx_color(0xF2EBD9), 0);
        gfx_fill_box(fb, OX + SPINE + 0.5f, by + 34, r1, by + 61, gfx_color(0xF2EBD9), 0);
        gfx_fill_box(fb, OX + SPINE - 0.5f, by + 34, OX + SPINE + 0.5f, by + 61, gfx_color(0x8A6A4A), 0);
        for (int k = 0; k < 5; k++) {
            gfx_fill_box(fb, l0 + 3, by + LINE_Y(k) + 3, OX + SPINE - 3, by + LINE_Y(k) + 4, gfx_color(0xC0C8D8), 0);
            gfx_fill_box(fb, OX + SPINE + 3, by + LINE_Y(k) + 3, r1 - 3, by + LINE_Y(k) + 4, gfx_color(0xC0C8D8), 0);
            if (w > 0.9f) gfx_fill_box(fb, l0 + 4, by + LINE_Y(k) + 1, l0 + 4 + k_left[k], by + LINE_Y(k) + 3, gfx_color(0x6A6A7A), 0);
        }
        for (int k = 0; k < 3 && w > 0.9f; k++)
            if (written[k] > 0.5f) gfx_fill_box(fb, OX + SPINE + 4, by + LINE_Y(k) + 1, OX + SPINE + 4 + written[k], by + LINE_Y(k) + 3, gfx_color(0x3A3A4A), 0);
        if (t > 3.0f && t < 3.6f) { /* page flip */
            float pw = 38 * cosf((float)M_PI * seg(t, 3.0f, 0.6f));
            float a = OX + SPINE, b = a + pw;
            gfx_fill_box(fb, fminf(a, b), by + 32, fmaxf(a, b), by + 60, gfx_color(0xE6DEC8), 0);
        }
    }
    if (eraser) /* eraser crumbs */
        for (int k = 0; k < 3; k++) {
            float ph = fmodf(t * 3 + k * 0.33f, 1.0f);
            gfx_fill_box(fb, tip_x - 4 + k * 4, tip_y + ph * 6, tip_x - 2 + k * 4, tip_y + 2 + ph * 6, gfx_color(0xE58A9A), 0);
        }
    mascot_pose_draw_hands(fb, OX, OY, 1, &p);
    if (has_pencil) pencil(fb, tip_x, tip_y, eraser);
}

/* ---------- watering the plant ---------- */

static void water(uint16_t *fb, float t, float cx, float ground)
{
    mascot_pose_t p = MASCOT_POSE_REST;
    float down = t < 0.6f ? 1 - ease_out2(t / 0.6f) : t > 4.8f ? ease_in2(seg(t, 4.8f, 0.8f)) : 0;
    float up = t < 0.6f ? 0 : t < PACT_WATER_POUR_T0 ? ease_inout2(seg(t, 0.6f, 0.6f)) : t < PACT_WATER_POUR_T1 ? 1 : 1 - ease_inout2(seg(t, PACT_WATER_POUR_T1, 0.6f));
    p.body_dy = -22 * up; /* stretches up on its legs to reach above the flower */
    p.hand_dy[1] = 14 * down - 18 * up;
    p.eye_open = blink(t);
    p.eye_dx = 4;
    p.eye_dy = 2 * up;
    p.happy = t > 3.6f && t < 5.2f;
    mascot_pose_draw(fb, OX, OY, 1, &p);

    float x0, y0, x1, y1;
    mascot_hand_rect(OX, OY, 1, &p, 1, &x0, &y0, &x1, &y1);
    const float gx = x1 - 4, gy = (y0 + y1) / 2, ang = 0.6f * up; /* grip point; tilt clockwise to pour */
    const uint16_t can = gfx_color(0x43A047), dark = gfx_color(0x2E7D32);
    gfx_fill_rot_box(fb, gx, gy, ang, 0, -6, 18, 10, can);          /* body */
    gfx_fill_rot_box(fb, gx, gy, ang, 2, -9, 16, -6, dark);         /* rim */
    gfx_fill_rot_box(fb, gx, gy, ang, 17, -4, 22, -1, dark);        /* spout base */
    gfx_fill_rot_box(fb, gx, gy, ang, 21, -8, 30, -5, can);         /* spout */
    gfx_fill_rot_box(fb, gx, gy, ang, 28, -10, 32, -4, dark);       /* rose */
    if (t > PACT_WATER_POUR_T0 && t < PACT_WATER_POUR_T1) {
        const float cs = cosf(ang), sn = sinf(ang);
        float tx = gx + 31 * cs + 7 * sn, ty = gy + 31 * sn - 7 * cs; /* spout tip */
        for (int k = 0; k < 6; k++) {
            float ph = fmodf(t * 2.2f + k / 6.0f, 1.0f);
            float dx = tx + (PLANT_X - 5 + (k % 3) * 3 - tx) * ph, dy = ty + (POT_TOP + 2 - ty) * ph * ph; /* into the soil */
            gfx_fill_box(fb, dx, dy, dx + 2, dy + 3, gfx_color(0x6EC6FF), 0);
        }
    }
    (void)ground;
}

/* ---------- walk in the park ---------- */
/* Home: through the door. Park: out of his house, walk left to right round the block until his house
 * comes back from the right, into the house. Home: out of the door. */

#define PARK_V 53.25f
#define GRASS_TOP 160.0f
#define MARK_X 130.0f   /* where he walks while the park scrolls */
#define HOUSE_DOOR_X 55.0f
#define WALK_S 16.0f
#define HOUSE2_SCREEN_X 175.0f /* where his house stops when he arrives back */
#define HOME_X 60.0f

/* phase boundaries (seconds) */
#define K_OPEN 0.4f
#define K_INTO (K_OPEN + 0.9f)
#define K_PARK (K_INTO + 0.3f)          /* cut to the park */
#define K_OUT_OPEN (K_PARK + 0.3f)
#define K_EMERGE (K_OUT_OPEN + 0.8f)
#define K_TO_MARK (K_EMERGE + (MARK_X - 90) / PARK_V)
#define K_WALK_OUT (K_TO_MARK + WALK_S)
#define DOOR2_X (HOUSE2_SCREEN_X + 55)
#define K_TO_DOOR (K_WALK_OUT + (DOOR2_X - 35 - MARK_X) / PARK_V)
#define K_ENTER (K_TO_DOOR + 0.8f)
#define WALK_W (PARK_V * WALK_S)              /* total scroll */
#define HOUSE2_WORLD (WALK_W + HOUSE2_SCREEN_X) /* the same house, reached from the other side */
#define K_HOME (K_ENTER + 0.3f)         /* cut back home */
#define K_HOME_EMERGE (K_HOME + 0.3f + 0.9f)
#define PARK_S (K_HOME_EMERGE + 0.3f)

static float hash(int i, int salt)
{
    uint32_t h = (uint32_t)i * 2654435761u ^ (uint32_t)salt * 40503u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (h & 0xFFFF) / 65535.0f;
}

static void stride(mascot_pose_t *p, float t, float amount)
{
    float a = fmodf(t, 0.2f), b = fmodf(t + 0.1f, 0.2f);
    p->leg_lift[0] = p->leg_lift[2] = amount * (a < 0.1f ? ease_out2(a / 0.1f) : 1 - ease_in2((a - 0.1f) / 0.1f));
    p->leg_lift[1] = p->leg_lift[3] = amount * (b < 0.1f ? ease_out2(b / 0.1f) : 1 - ease_in2((b - 0.1f) / 0.1f));
}

/* Mascot at body centre cx with feet at fy, scaled (shrinking = going away through a door). */
static void mascot_at(uint16_t *fb, float cx, float fy, float s, const mascot_pose_t *p)
{
    mascot_pose_draw(fb, cx - 53.5f * s, fy - MASCOT_H * s, s, p);
}

static void far_house(uint16_t *fb, float x, int i, bool day)
{
    static const uint32_t night_walls[3] = {0x3E4C6E, 0x5E3E54, 0x4E5E3E}, day_walls[3] = {0x8AA0C8, 0xC89AA8, 0x9AB88A};
    float w = 44 + 16 * hash(i, 1), h = 34 + 18 * hash(i, 2), y0 = GRASS_TOP - h;
    gfx_fill_box(fb, x, y0, x + w, GRASS_TOP, gfx_color((day ? day_walls : night_walls)[i % 3]), 0);
    for (float rw = w + 6, ry = y0; rw > 4; rw -= 8, ry -= 4)
        gfx_fill_box(fb, x + (w - rw) / 2, ry - 4, x + (w + rw) / 2, ry, gfx_color(day ? 0xB24A3A : 0x8A3A3A), 0);
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 2; c++) {
            float wx = x + 7 + c * (w - 22), wy = y0 + 6 + r * 13;
            if (wy + 8 > GRASS_TOP - 12) continue;
            bool lit = hash(i * 4 + r * 2 + c, 3) > 0.35f;
            gfx_fill_box(fb, wx, wy, wx + 8, wy + 8, gfx_color(day ? 0xCFE8F8 : lit ? 0xF2D060 : 0x22222E), 0);
        }
    gfx_fill_box(fb, x + w / 2 - 4, GRASS_TOP - 12, x + w / 2 + 4, GRASS_TOP, gfx_color(0x3A2A20), 0);
}

static void my_house(uint16_t *fb, float x, float door_open, bool day)
{
    gfx_fill_box(fb, x, GRASS_TOP - 70, x + 100, GRASS_TOP, gfx_color(day ? 0xE8C8A0 : 0x7A6450), 0);
    for (float rw = 112, ry = GRASS_TOP - 70; rw > 6; rw -= 10, ry -= 5)
        gfx_fill_box(fb, x + (100 - rw) / 2, ry - 5, x + (100 + rw) / 2, ry, gfx_color(day ? 0xC0503A : 0x8A3A3A), 0);
    gfx_fill_box(fb, x + 72, GRASS_TOP - 58, x + 90, GRASS_TOP - 40, gfx_color(day ? 0xCFE8F8 : 0xF2D060), 0);
    door_draw(fb, x + 40, GRASS_TOP - 50, x + 70, GRASS_TOP, door_open, day ? 0x3A2A20 : 0xF2D060);
}

static void tree(uint16_t *fb, float x, bool day)
{
    const uint16_t leaf = gfx_color(day ? 0x43A047 : 0x2E7D32), hi = gfx_color(day ? 0x7BCB5A : 0x43A047);
    gfx_fill_box(fb, x + 18, GRASS_TOP - 26, x + 24, GRASS_TOP, gfx_color(0x6B4A3A), 0);
    gfx_fill_box(fb, x + 1, GRASS_TOP - 38, x + 41, GRASS_TOP - 24, leaf, 0);
    gfx_fill_box(fb, x - 2, GRASS_TOP - 50, x + 44, GRASS_TOP - 36, leaf, 0);
    gfx_fill_box(fb, x + 5, GRASS_TOP - 60, x + 37, GRASS_TOP - 48, leaf, 0);
    gfx_fill_box(fb, x + 9, GRASS_TOP - 57, x + 19, GRASS_TOP - 51, hi, 0);
}

static void bench(uint16_t *fb, float x)
{
    const uint16_t wood = gfx_color(0x8A5A3A), iron = gfx_color(0x4A4A55);
    gfx_fill_box(fb, x, 146, x + 40, 149, wood, 0);
    gfx_fill_box(fb, x + 4, 146, x + 7, 154, iron, 0);
    gfx_fill_box(fb, x + 33, 146, x + 36, 154, iron, 0);
    gfx_fill_box(fb, x - 2, 154, x + 42, 158, wood, 0);
    gfx_fill_box(fb, x + 3, 158, x + 6, 168, iron, 0);
    gfx_fill_box(fb, x + 34, 158, x + 37, 168, iron, 0);
}

static void lamp(uint16_t *fb, float x, bool day)
{
    gfx_fill_box(fb, x, 108, x + 3, 168, gfx_color(0x5A5A66), 0);
    gfx_fill_box(fb, x - 4, 102, x + 7, 108, gfx_color(0x5A5A66), 0);
    gfx_fill_box(fb, x - 2, 108, x + 5, 112, gfx_color(day ? 0xA0A0A8 : 0xF2D060), 0);
}

static void cloud(uint16_t *fb, float x, float y, uint16_t c)
{
    gfx_fill_box(fb, x, y + 6, x + 44, y + 14, c, 0);
    gfx_fill_box(fb, x + 8, y, x + 26, y + 8, c, 0);
    gfx_fill_box(fb, x + 22, y + 2, x + 38, y + 8, c, 0);
}

/* Dogs: side view facing right, two walk frames and two sitting (tail) frames. */
static const char *const k_dog_walk[2][8] = {
    {".............KK.", "............KKKK", "K..........KKKEK", ".K........KKKKKN", "..KKKKKKKKKKKK..", "..KKKKKKKKKKK...",
     "..KK.KK..KK.KK..", "..K...K..K...K.."},
    {".............KK.", "............KKKK", "...........KKKEK", "KK........KKKKKN", "..KKKKKKKKKKKK..", "..KKKKKKKKKKK...",
     "...KK.....KK....", "...KK.....KK...."},
};
static const char *const k_dog_sit[2][8] = {
    {"............KK..", "...........KKKK.", "..........KKKEK.", "..........KKKKKN", "K.......KKKKKK..", ".K....KKKKKKK...",
     "..KKKKKKKKKKK...", "..KKKKKK.KK.KK.."},
    {"............KK..", "...........KKKK.", "..........KKKEK.", "..........KKKKKN", "........KKKKKK..", "......KKKKKKK...",
     "KKKKKKKKKKKKK...", "..KKKKKK.KK.KK.."},
};

static void dog(uint16_t *fb, const char *const *rows, float x, float feet, bool face_left, uint32_t fur, uint32_t eye)
{
    const float cell = 2;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 16; c++) {
            char ch = rows[r][c];
            if (ch == '.') continue;
            uint32_t col = ch == 'K' ? fur : ch == 'E' ? eye : 0x202020;
            float cx = face_left ? x + (15 - c) * cell : x + c * cell;
            gfx_fill_box(fb, cx, feet - (8 - r) * cell, cx + cell, feet - (7 - r) * cell, gfx_color(col), 0);
        }
}

static uint32_t mix_rgb(uint32_t a, uint32_t b, float k)
{
    uint32_t c = 0;
    for (int sh = 0; sh <= 16; sh += 8) c |= (uint32_t)lerpf((float)((a >> sh) & 0xFF), (float)((b >> sh) & 0xFF), k) << sh;
    return c;
}

/* Thunderstorm over the park: storm 0..1 darkens the sky, flash 0..1 is a lightning flash, bolt_x < 0 no bolt. */
typedef struct {
    float storm, flash, bolt_x;
} weather_t;

static void bolt(uint16_t *fb, float x, int seed)
{
    const uint16_t c = gfx_color(0xF4F0C0);
    float y = 0;
    for (int k = 0; k < 6; k++) {
        float nx = x + 14 * (hash(seed * 7 + k, 41) - 0.5f), ny = y + 14;
        gfx_fill_rot_box(fb, x, y, atan2f(x - nx, ny - y), -1, 0, 1, hypotf(nx - x, ny - y) + 1, c);
        x = nx;
        y = ny;
    }
}

static void park_scene(uint16_t *fb, float t, float w, float mascot_world, bool day, float house_door, float house2_door, const weather_t *wx)
{
    const float daylight = day ? 1 : 0, storm = wx ? wx->storm : 0, flash = wx ? wx->flash : 0;
    static const uint32_t k_storm_sky[3] = {0x14171E, 0x1B1F28, 0x242934};
    for (int b = 0; b < 3; b++)
        gfx_fill_box(fb, 0, GRASS_TOP * b / 3, GFX_W, GRASS_TOP * (b + 1) / 3,
                     gfx_color(mix_rgb(mix_rgb(sky_color(daylight, b), k_storm_sky[b], storm), 0xC8CEE8, flash)), 0);
    static const int disc[7] = {8, 14, 18, 18, 18, 14, 8};
    if (storm > 0) {
        const uint16_t c = gfx_color(mix_rgb(0x343A48, 0x9AA2BC, flash));
        for (int k = 0; k < 7; k++) cloud(fb, fmodf(k * 64 - 0.2f * w + 18 * t + 4000, 420) - 60, 2 + (k % 3) * 9, c);
        if (wx->bolt_x >= 0) bolt(fb, wx->bolt_x, (int)wx->bolt_x);
    } else if (day) {
        for (int r = 0; r < 7; r++) gfx_fill_rect(fb, 276 - disc[r] / 2, 14 + r * 3, disc[r], 3, gfx_color(0xF7C948));
        for (int k = 0; k < 3; k++) cloud(fb, fmodf(60 + k * 140 - 0.2f * w + 6 * t + 4000, 420) - 60, 10 + k * 14, gfx_color(0xF4F8FC));
    } else {
        for (int i = 0; i < 12; i++)
            if (fmodf(t * 0.7f + hash(i, 9), 1.0f) > 0.15f) gfx_fill_rect(fb, (int)(hash(i, 5) * 310 + 5), (int)(hash(i, 6) * 70 + 4), 2, 2, gfx_color(0xE8E8F0));
        for (int r = 0; r < 7; r++) gfx_fill_rect(fb, 276 - disc[r] / 2, 16 + r * 3, disc[r], 3, gfx_color(0xF2EBC0));
    }
    for (int i = 0; i < 9; i++) {
        float x = 170 + i * 115 + 30 * hash(i, 11) - 0.6f * w;
        if (x > -70 && x < GFX_W) far_house(fb, x, i, day);
    }
    for (int i = 0; i < 14; i++) {
        float x = 150 + i * 90 + 25 * hash(i, 12) - 0.8f * w;
        if (x > -50 && x < GFX_W) tree(fb, x, day);
    }
    gfx_fill_box(fb, 0, GRASS_TOP, GFX_W, GFX_H, gfx_color(day ? 0x4CAF50 : 0x2F6B2A), 0);
    for (int j = 0; j < 40; j++) {
        float x = fmodf(j * 9 - w + 3600, 360) - 20;
        gfx_fill_box(fb, x, GRASS_TOP - 2, x + 2, GRASS_TOP + 2, gfx_color(day ? 0x7BCB5A : 0x4CAF50), 0);
    }
    my_house(fb, 4 - w, house_door, day);
    if (HOUSE2_WORLD - w < GFX_W) my_house(fb, HOUSE2_WORLD - w, house2_door, day);
    for (int i = 0; i < 8; i++) {
        float wx = 230 + i * 170 + 30 * hash(i, 13), x = wx - w;
        if (wx > HOUSE2_WORLD - 60 || x < -50 || x > GFX_W) continue; /* keep the way to the house clear */
        if (i % 2) lamp(fb, x, day);
        else bench(fb, x);
    }
    /* dogs: the ginger one plays around him, the white one walks by, the black one sits by a bench */
    int frame = (int)(t / 0.12f) % 2;
    float gx = mascot_world + 75 + 40 * sinf(t * 0.9f), gv = 36 * cosf(t * 0.9f) + (w > 0 ? 0 : 0);
    dog(fb, k_dog_walk[frame], gx - w, 168, gv < 0, 0xD9822B, 0x202020);
    dog(fb, k_dog_walk[frame], 640 - 22 * t - w, 168, true, 0xF0F0F0, 0x202020);
    dog(fb, k_dog_sit[(int)(t / 0.25f) % 2], 230 + 44 - w, 168, true, 0x2A2A2A, 0xF0F0F0);
}

/* ---------- shaken on the walk: by day a ball game, at night a thunderstorm ---------- */

enum { PARK_PLAIN, PARK_BALL, PARK_STORM };
static struct {
    int ev;
    float te, we; /* when it started (act time) and how far the park had scrolled */
} s_park;

#define SHAKE_FROM K_TO_MARK          /* out in the park, */
#define SHAKE_TO (K_WALK_OUT - 3.0f)  /* before his house comes back into view */

/* ball game: it drops from the sky, bounces, he kicks it up, heads it four times and sends it away */
#define B_DROP 0.75f
#define B_BOUNCE1 (B_DROP + 0.55f)
#define B_BOUNCE2 (B_BOUNCE1 + 0.35f)
#define B_ROLL (B_BOUNCE2 + 0.35f)
#define B_KICK (B_ROLL + 0.6f)
#define B_HEAD_P 0.7f
#define B_HEADS 4
#define B_LAUNCH (B_KICK + B_HEADS * B_HEAD_P)
#define B_GONE (B_LAUNCH + 1.2f)
#define BALL_S (B_GONE + 0.9f)
#define BALL_GROUND (170.0f - BALL_R)
#define BALL_HEAD (170.0f - MASCOT_H - BALL_R + 1)

/* storm: startle, run home along the rest of the block, then dripping and a shiver at home */
#define ST_STARTLE 0.6f
#define RUN_V (3 * PARK_V)
#define H_OUT 0.9f              /* out of the door into the room */
#define H_DRIP (H_OUT + 2.4f)
#define H_SHIVER (H_DRIP + 1.8f)  /* one slow shiver, like a cartoon cat */
#define H_END (H_SHIVER + 0.7f)

static float storm_home_at(float we)
{
    return ST_STARTLE + (WALK_W - we) / RUN_V + (DOOR2_X - 35 - MARK_X) / RUN_V + 0.8f;
}

void pact_begin(pact_t a)
{
    if (a == PACT_PARK) s_park.ev = PARK_PLAIN;
}

float pact_park_shake(float t, float daylight)
{
    if (s_park.ev != PARK_PLAIN || t < SHAKE_FROM || t > SHAKE_TO) return 0; /* once per walk */
    s_park.ev = daylight > 0.5f ? PARK_BALL : PARK_STORM;
    s_park.te = t;
    s_park.we = PARK_V * (t - K_TO_MARK);
    return s_park.ev == PARK_BALL ? PARK_S + BALL_S : t + storm_home_at(s_park.we) + H_END;
}

/* Along a parabola from (x0, y0) to (x1, y1), `apex` px above the straight line halfway. */
static void arc(float k, float x0, float y0, float x1, float y1, float apex, float *x, float *y)
{
    *x = lerpf(x0, x1, k);
    *y = lerpf(y0, y1, k) - 4 * apex * k * (1 - k);
}

static void ball_game(uint16_t *fb, float t, float u, bool day)
{
    const float mx = MARK_X, w = s_park.we;
    park_scene(fb, t, w, mx + w, day, 0, 0, 0);
    float bx, by;
    if (u < B_DROP) {
        bx = mx + 70;
        by = lerpf(-BALL_R - 4, BALL_GROUND, (u / B_DROP) * (u / B_DROP));
    } else if (u < B_BOUNCE1) arc(seg(u, B_DROP, B_BOUNCE1 - B_DROP), mx + 70, BALL_GROUND, mx + 62, BALL_GROUND, 40, &bx, &by);
    else if (u < B_BOUNCE2) arc(seg(u, B_BOUNCE1, B_BOUNCE2 - B_BOUNCE1), mx + 62, BALL_GROUND, mx + 56, BALL_GROUND, 14, &bx, &by);
    else if (u < B_ROLL) {
        bx = lerpf(mx + 56, mx + 46, ease_out2(seg(u, B_BOUNCE2, B_ROLL - B_BOUNCE2)));
        by = BALL_GROUND;
    } else if (u < B_KICK) arc(seg(u, B_ROLL, B_KICK - B_ROLL), mx + 46, BALL_GROUND, mx + 2, BALL_HEAD, 45, &bx, &by);
    else if (u < B_LAUNCH) {
        int n = (int)((u - B_KICK) / B_HEAD_P);
        float side = n % 2 ? -2 : 2;
        arc(seg(u, B_KICK + n * B_HEAD_P, B_HEAD_P), mx + side, BALL_HEAD, mx - side, BALL_HEAD, 46, &bx, &by);
    } else arc(seg(u, B_LAUNCH, B_GONE - B_LAUNCH), mx, BALL_HEAD, mx + 250, -40, 60, &bx, &by);

    /* a little hop at every header; the ball rides on the head at the touch */
    float hop = 0;
    for (int k = 0; k <= B_HEADS; k++) {
        float d = (u - (B_KICK + k * B_HEAD_P)) / 0.07f;
        hop = fmaxf(hop, (k == B_HEADS ? 10 : 7) * expf(-d * d));
    }
    if (u >= B_KICK && u < B_GONE) by -= hop;

    if (by > -BALL_R) { /* shadow on the grass */
        float sw = 24 * (1 - 0.6f * clampf((BALL_GROUND - by) / 120, 0, 1));
        gfx_fill_box_dither(fb, bx - sw / 2, 168, bx + sw / 2, 171, gfx_color(day ? 0x2E7D32 : 0x1E4A1C));
    }
    mascot_pose_t p = MASCOT_POSE_REST;
    p.eye_open = blink(t);
    if (u < B_GONE) {
        p.eye_dx = clampf((bx - mx) / 12, -4, 4);
        p.eye_dy = clampf((by - 110) / 18, -4, 3);
    } else {
        p.happy = true;
        p.hands_dy = -10 * sinf(3.1416f * seg(u, B_GONE, BALL_S - B_GONE));
    }
    if (u > B_ROLL && u < B_ROLL + 0.3f) p.leg_lift[3] = sinf(3.1416f * seg(u, B_ROLL, 0.3f)); /* the kick */
    mascot_at(fb, mx, 170 - hop, 1, &p);
    /* rolls on the grass, spins in the air */
    if (u < B_GONE) ball_draw(fb, bx, by, -(bx - mx - 70) / BALL_R, 7 * (fminf(u, B_BOUNCE2) + fmaxf(0, u - B_ROLL)));
}

/* Lightning: a double flicker every couple of seconds. Returns 0..1, and which strike it is. */
static float storm_flash(float u, int *strike)
{
    static const float k_at[] = {0.1f, 2.1f, 3.8f, 6.3f, 8.9f, 11.6f, 14.0f};
    for (int i = 0; i < (int)(sizeof(k_at) / sizeof(k_at[0])); i++) {
        float d = u - k_at[i];
        if (d >= 0 && d < 0.3f) {
            *strike = i;
            return d < 0.07f ? 1 : d < 0.13f ? 0.2f : d < 0.21f ? 0.8f : 0.3f;
        }
    }
    return 0;
}

static void rain(uint16_t *fb, float t, int n, const gfx_clip_t *clip)
{
    const uint16_t c = gfx_color(0x9AA8C8);
    for (int i = 0; i < n; i++) {
        float y = fmodf(hash(i, 22) * 400 + t * (300 + 90 * hash(i, 21)), 200) - 20;
        float x = fmodf(hash(i, 23) * 360 + 0.3f * y + 4000, 340) - 10; /* slanted by the wind */
        gfx_fill_box(fb, x, y, x + 1, y + 4, c, clip);
        gfx_fill_box(fb, x + 1, y + 4, x + 2, y + 8, c, clip);
    }
}

/* Shiver: a band of rows that runs from the feet up slides sideways. Only the mascot's pixels move:
 * the background under the band is saved before the mascot is drawn (shiver_save) and restored around it. */
#define SHIVER_BAND 18
#define SHIVER_W 128
static uint16_t s_band_bg[2 * SHIVER_BAND + 1][SHIVER_W];
static int s_band_y0, s_band_x0;

static void shiver_save(const uint16_t *fb, float cx, float fy, float front)
{
    s_band_y0 = (int)(fy - front) - SHIVER_BAND;
    s_band_x0 = (int)clampf(cx - SHIVER_W / 2, 0, GFX_W - SHIVER_W);
    for (int i = 0; i <= 2 * SHIVER_BAND; i++)
        if (s_band_y0 + i >= 0 && s_band_y0 + i < GFX_H) memcpy(s_band_bg[i], &fb[(s_band_y0 + i) * GFX_W + s_band_x0], sizeof(s_band_bg[i]));
}

/* An S-shaped ripple centred on the front, and the fur bristling at the sides where it passes. */
static void shiver_apply(uint16_t *fb, float amp)
{
    static uint16_t drawn[SHIVER_W];
    for (int i = 0; i <= 2 * SHIVER_BAND; i++) {
        int y = s_band_y0 + i;
        if (y < 0 || y >= GFX_H) continue;
        float d = (float)(i - SHIVER_BAND), e = d / (SHIVER_BAND / 2.0f), env = expf(-e * e);
        int dx = (int)lroundf(amp * env * sinf(d * 0.39f)); /* wavelength 16 px */
        int fur = (y & 1) && fabsf(d) < 7 ? (int)lroundf(5 * env) : 0;
        if (!dx && !fur) continue;
        uint16_t *r = &fb[y * GFX_W + s_band_x0];
        memcpy(drawn, r, sizeof(drawn));
        memcpy(r, s_band_bg[i], sizeof(drawn));
        int l = SHIVER_W, rr = -1;
        for (int x = 0; x < SHIVER_W; x++)
            if (drawn[x] != s_band_bg[i][x]) {
                if (x + dx >= 0 && x + dx < SHIVER_W) r[x + dx] = drawn[x];
                if (l == SHIVER_W) l = x;
                rr = x;
            }
        for (int k = 1; k <= fur && rr >= 0; k++) { /* bristles */
            if (l + dx - k >= 0) r[l + dx - k] = drawn[l];
            if (rr + dx + k < SHIVER_W) r[rr + dx + k] = drawn[rr];
        }
    }
}

/* A water drop, 6x7 px with a glint. */
static void drop(uint16_t *fb, float x, float y)
{
    const uint16_t c = gfx_color(0x8EC8FF);
    gfx_fill_box(fb, x + 2, y, x + 4, y + 2, c, 0);
    gfx_fill_box(fb, x + 1, y + 2, x + 5, y + 3, c, 0);
    gfx_fill_box(fb, x, y + 3, x + 6, y + 6, c, 0);
    gfx_fill_box(fb, x + 1, y + 6, x + 5, y + 7, c, 0);
    gfx_fill_box(fb, x + 1, y + 3, x + 2, y + 5, gfx_color(0xF0F8FF), 0);
}

static void storm_home(uint16_t *fb, float t, float h, float u, float ground, float daylight)
{
    room_draw(fb, daylight, h < H_OUT ? seg(h, 0, 0.3f) : 1 - seg(h, H_OUT - 0.1f, 0.3f), t);
    /* the storm goes on in the window and dies down */
    int strike = 0;
    const float rain_k = 1 - seg(h, H_DRIP, H_END - H_DRIP), flash = storm_flash(u, &strike) * rain_k;
    /* panes between the 3 px cross bars, rounded like gfx_fill_box rounds the bars */
    const int x0 = (int)WIN_X0, x1 = (int)WIN_X1, y0 = (int)WIN_Y0, y1 = (int)WIN_Y1;
    const int bl = (int)((WIN_X0 + WIN_X1) / 2 - 1.5f + 0.5f), br = bl + 3, bt = (int)((WIN_Y0 + WIN_Y1) / 2 - 1.5f + 0.5f), bb = bt + 3;
    const gfx_clip_t panes[4] = {{x0, y0, bl, bt}, {br, y0, x1, bt}, {x0, bb, bl, y1}, {br, bb, x1, y1}};
    for (int k = 0; k < 4; k++) {
        if (rain_k > 0.3f || flash > 0.5f) /* storm clouds hide the moon until it clears */
            gfx_fill_box(fb, WIN_X0, WIN_Y0, WIN_X1, WIN_Y1, gfx_color(flash > 0.5f ? 0xC8CEE8 : 0x1B1F28), &panes[k]);
        rain(fb, t, (int)(220 * rain_k), &panes[k]);
    }

    const float x = h < H_OUT ? lerpf(DOOR_CX, HOME_X, ease_out2(seg(h, 0.3f, H_OUT - 0.3f))) : HOME_X;
    const float puddle = 90 * seg(h, H_OUT, 1.6f) * (1 - seg(h, H_SHIVER, H_END - H_SHIVER));
    if (puddle > 1) {
        gfx_fill_box_dither(fb, x - puddle / 2, ground - 1, x + puddle / 2, ground + 2, gfx_color(0x6EA8E0));
        gfx_fill_box_dither(fb, x - puddle / 2 + 8, ground - 3, x + puddle / 2 - 8, ground - 1, gfx_color(0x6EA8E0));
    }
    mascot_pose_t p = MASCOT_POSE_REST;
    p.eye_open = blink(t);
    if (h < H_OUT) {
        float k = seg(h, 0.3f, H_OUT - 0.3f);
        if (h < 0.3f) return; /* the door is opening */
        stride(&p, 1.8f * t, 1 - k);
        mascot_at(fb, x, lerpf(FLOOR_Y - 3, ground, k), lerpf(0.35f, 1, ease_out2(k)), &p);
        return;
    }
    if (h < H_DRIP) { /* drenched: droopy eyes, water runs down and drips off */
        p.eye_open = fminf(p.eye_open, 0.55f);
        p.eye_dy = 2;
        mascot_at(fb, x, ground, 1, &p);
        for (int i = 0; i < 7; i++) { /* running down the body */
            float ph = fmodf(h * (0.4f + 0.3f * hash(i, 31)) + hash(i, 32), 1.0f);
            drop(fb, x - 40 + 74 * hash(i, 33), ground - 82 + 50 * ph);
        }
        for (int i = 0; i < 8; i++) { /* dripping off onto the floor */
            float per = 0.5f + 0.3f * hash(i, 34), ph = fmodf(h + hash(i, 35) * per, per) / per;
            drop(fb, x - 46 + 86 * hash(i, 36), ground - 30 + 26 * ph * ph);
        }
        return;
    }
    if (h < H_SHIVER) { /* one slow shiver from the feet up, fur on end, water flung off the sides */
        const float v = h - H_DRIP, front = lerpf(-14, MASCOT_H + 14, ease_inout2(v / (H_SHIVER - H_DRIP)));
        p.eye_open = 0.1f;
        shiver_save(fb, x, ground, front);
        mascot_at(fb, x, ground, 1, &p);
        shiver_apply(fb, 6);
        for (int i = 0; i < 10; i++) {
            float ph = fmodf(v * 1.4f + hash(i, 37), 1.0f), side = i % 2 ? 1 : -1;
            drop(fb, x - 3 + side * (58 + 60 * ph), ground - clampf(front, 8, MASCOT_H) - 10 * hash(i, 38) + 34 * ph * ph);
        }
        return;
    }
    p.happy = h < H_SHIVER + 0.4f; /* dry again */
    mascot_at(fb, x, ground, 1, &p);
}

static void storm(uint16_t *fb, float t, float u, float ground, float daylight)
{
    const float home_at = storm_home_at(s_park.we);
    if (u >= home_at) {
        storm_home(fb, t, u - home_at, u, ground, daylight);
        return;
    }
    const float run_end = ST_STARTLE + (WALK_W - s_park.we) / RUN_V, door_at = home_at - 0.8f;
    float w = s_park.we, mx = MARK_X, fy = 170, sc = 1, door2 = 0;
    mascot_pose_t p = MASCOT_POSE_REST;
    p.eye_open = blink(t);
    if (u < ST_STARTLE) { /* startled: a jump, looking up at the sky */
        fy -= 12 * sinf(3.1416f * u / ST_STARTLE);
        p.eye_dy = -4;
        p.hands_dy = -10 * sinf(3.1416f * u / ST_STARTLE);
    } else if (u < run_end) { /* runs for home, hands over the head */
        w += RUN_V * (u - ST_STARTLE);
        stride(&p, 1.8f * t, 1);
        p.hands_dy = -12;
        p.eye_dx = 4;
    } else if (u < door_at) {
        w = WALK_W;
        mx = lerpf(MARK_X, DOOR2_X - 35, seg(u, run_end, door_at - run_end));
        door2 = seg(u, run_end, 0.2f);
        stride(&p, 1.8f * t, 1);
        p.hands_dy = -12;
        p.eye_dx = 4;
    } else {
        float k = seg(u, door_at, 0.5f);
        w = WALK_W;
        door2 = 1 - seg(u, door_at + 0.5f, 0.3f);
        mx = lerpf(DOOR2_X - 35, DOOR2_X, k);
        fy = lerpf(170, GRASS_TOP, k);
        sc = lerpf(1, 0.35f, ease_in2(k));
        stride(&p, 1.8f * t, 1 - k);
        p.eye_dx = 4;
    }
    int strike = 0;
    const float flash = storm_flash(u, &strike);
    const weather_t wx = {seg(u, 0, 0.4f), flash, flash > 0.5f ? 40 + 240 * hash(strike, 42) : -1};
    park_scene(fb, t, w, mx + w, false, 0, door2, &wx);
    if (u < door_at + 0.5f) mascot_at(fb, mx, fy, sc, &p);
    rain(fb, t, (int)(90 * wx.storm), 0);
    for (int i = 0; i < 12; i++) { /* splashes on the grass */
        float sx = hash(i + 13 * (int)(t * 8), 24) * GFX_W, sy = 162 + 8 * hash(i, 25);
        gfx_fill_box(fb, sx - 1, sy, sx + 2, sy + 1, gfx_color(0x9AA8C8), 0);
    }
}

static void park(uint16_t *fb, float t, float cx, float ground, float daylight)
{
    (void)cx;
    const bool day = daylight > 0.5f;
    float tp = t; /* the plain walk's clock: it waits while he plays ball */
    if (s_park.ev != PARK_PLAIN && t >= s_park.te) {
        float u = t - s_park.te;
        if (s_park.ev == PARK_STORM) {
            storm(fb, t, u, ground, daylight);
            return;
        }
        if (u < BALL_S) {
            ball_game(fb, t, u, day);
            return;
        }
        tp = t - BALL_S;
    }
    mascot_pose_t p = MASCOT_POSE_REST;
    p.eye_open = blink(t);

    if (tp < K_PARK) { /* home: through the door */
        room_draw(fb, daylight, tp < K_INTO ? seg(tp, 0, K_OPEN) : 1 - seg(tp, K_INTO, 0.3f), t);
        if (tp < K_INTO) {
            float k = seg(tp, K_OPEN, K_INTO - K_OPEN);
            stride(&p, t, 1);
            p.eye_dx = -4;
            mascot_at(fb, lerpf(HOME_X, DOOR_CX, k), lerpf(ground, FLOOR_Y - 3, k), lerpf(1, 0.35f, ease_in2(k)), &p);
        }
        return;
    }
    if (tp >= K_HOME) { /* home again: out of the door */
        float k = seg(tp, K_HOME + 0.3f, 0.9f);
        room_draw(fb, daylight, tp < K_HOME_EMERGE ? seg(tp, K_HOME, 0.3f) : 1 - seg(tp, K_HOME_EMERGE, 0.3f), t);
        if (tp >= K_HOME + 0.3f) {
            if (k < 1) stride(&p, t, 1);
            p.eye_dx = 4 * (1 - k);
            mascot_at(fb, lerpf(DOOR_CX, HOME_X, k), lerpf(FLOOR_Y - 3, ground, k), lerpf(0.35f, 1, ease_out2(k)), &p);
        }
        return;
    }

    float w = 0, mx = MARK_X, fy = 170, sc = 1, door = 0, door2 = 0;
    bool walking = true;
    if (tp < K_EMERGE) {
        door = seg(tp, K_PARK, 0.3f);
        float k = seg(tp, K_OUT_OPEN, K_EMERGE - K_OUT_OPEN);
        mx = lerpf(HOUSE_DOOR_X, 90, k);
        fy = lerpf(GRASS_TOP, 170, k);
        sc = lerpf(0.35f, 1, ease_out2(k));
        walking = tp >= K_OUT_OPEN;
    } else if (tp < K_TO_MARK) {
        door = 1 - seg(tp, K_EMERGE, 0.3f);
        mx = 90 + PARK_V * (tp - K_EMERGE);
    } else if (tp < K_WALK_OUT) {
        w = PARK_V * (tp - K_TO_MARK);
        if (tp > K_TO_MARK + 7 && tp < K_TO_MARK + 8.2f) p.eye_dy = -3; /* looks up at the sky */
    } else if (tp < K_TO_DOOR) { /* his house is back: walk up to the door */
        w = WALK_W;
        mx = MARK_X + PARK_V * (tp - K_WALK_OUT);
        door2 = seg(tp, K_TO_DOOR - 0.4f, 0.3f);
    } else {
        float k = seg(tp, K_TO_DOOR, K_ENTER - K_TO_DOOR);
        w = WALK_W;
        door2 = 1 - seg(tp, K_ENTER, 0.3f);
        mx = lerpf(DOOR2_X - 35, DOOR2_X, k);
        fy = lerpf(170, GRASS_TOP, k);
        sc = lerpf(1, 0.35f, ease_in2(k));
        walking = tp < K_ENTER;
    }
    park_scene(fb, t, w, mx + w, day, door, door2, 0);
    if (tp >= K_ENTER) return; /* inside the house */
    if (walking) stride(&p, t, 1);
    p.eye_dx = 4;
    mascot_at(fb, mx, fy, sc, &p);
}

/* ---------- dispatch ---------- */

float pact_duration(pact_t a)
{
    static const float k_dur[PACT_COUNT] = {12.0f, 9.0f, 9.0f, 10.0f, 5.6f, PARK_S};
    return k_dur[a];
}

void pact_draw(uint16_t *fb, pact_t a, float t, float cx, float ground, float daylight)
{
    switch (a) {
    case PACT_NOTEBOOK: notebook(fb, t, cx, ground); break;
    case PACT_PHONE: phone(fb, t, cx, ground); break;
    case PACT_COFFEE: coffee(fb, t, cx, ground); break;
    case PACT_SNACK: snack(fb, t, cx, ground); break;
    case PACT_WATER: water(fb, t, cx, ground); break;
    default: park(fb, t, cx, ground, daylight); break;
    }
}
