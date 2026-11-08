#include "mascot_pose.h"

#include <math.h>

#include "gfx.h"

static const float k_leg_x[4] = {11, 32, 64, 85};

static const char *const k_spiral[11] = {
    "###########", "..........#", "#########.#", "#.......#.#", "#.#####.#.#", "#.#...#.#.#",
    "#.#.###.#.#", "#.#.....#.#", "#.#######.#", "#.........#", "###########",
};

/* One 11x11 eye; the spiral turns a quarter every 0.1 s. */
static void spiral_eye(uint16_t *fb, float x, float y, float s, float t, uint16_t c)
{
    int rot = (int)(t / 0.1f) % 4;
    for (int r = 0; r < 11; r++)
        for (int k = 0; k < 11; k++) {
            int rr = r, kk = k;
            if (rot == 1) rr = k, kk = 10 - r;
            if (rot == 2) rr = 10 - r, kk = 10 - k;
            if (rot == 3) rr = 10 - k, kk = r;
            if (k_spiral[rr][kk] == '#') gfx_fill_box(fb, x + k * s, y + r * s, x + (k + 1) * s, y + (r + 1) * s, c, 0);
        }
}

#define BOX(x, y, w, h, c) gfx_fill_box(fb, ox + (x) * s, oy + (y) * s, ox + ((x) + (w)) * s, oy + ((y) + (h)) * s, c, 0)

void mascot_pose_draw_body(uint16_t *fb, float ox, float oy, float s, const mascot_pose_t *p)
{
    const uint16_t body = gfx_color(MASCOT_RGB), eye = gfx_color(0x000000);
    const float by = p->body_dy;
    for (int i = 0; i < 4; i++) {
        float top = 60 + by, bottom = 86 - p->leg_lift[i] * 26 * 0.55f;
        if (bottom > top) BOX(k_leg_x[i], top, 11, bottom - top, body);
    }
    BOX(11, by, 85, 65, body);
    const float ex[2] = {21 + p->eye_dx, 75 + p->eye_dx}, ey = 11 + by + p->eye_dy;
    for (int i = 0; i < 2; i++) {
        if (p->spiral) {
            spiral_eye(fb, ox + ex[i] * s, oy + ey * s, s, p->spiral_t + i * 0.05f, eye);
        } else if (p->happy) { /* the ^ eyes from the flag animation */
            float cx = ex[i] + 5.5f;
            BOX(cx - 5, ey, 10, 6, eye);
            BOX(cx - 11, ey + 6, 6, 6, eye);
            BOX(cx + 5, ey + 6, 6, 6, eye);
        } else {
            float h = 11 * (p->eye_open < 0.15f ? 0.15f : p->eye_open);
            BOX(ex[i], ey + (11 - h) / 2, 11, h, eye);
        }
    }
}

void mascot_hand_rect(float ox, float oy, float s, const mascot_pose_t *p, int i, float *x0, float *y0, float *x1, float *y1)
{
    float hx = (i ? 85 : 0) + p->hand_dx[i], hy = 21 + p->body_dy + p->hands_dy + p->hand_dy[i];
    *x0 = ox + hx * s;
    *y0 = oy + hy * s;
    *x1 = ox + (hx + 22) * s;
    *y1 = oy + (hy + 23) * s;
}

void mascot_pose_draw_hands(uint16_t *fb, float ox, float oy, float s, const mascot_pose_t *p)
{
    const uint16_t body = gfx_color(MASCOT_RGB), edge = gfx_color(0xA8503A);
    for (int i = 0; i < 2; i++) {
        float x0, y0, x1, y1;
        mascot_hand_rect(ox, oy, s, p, i, &x0, &y0, &x1, &y1);
        bool in_front = fabsf(p->hand_dx[i]) > 2; /* a hand over the body needs an outline to read */
        if (in_front) gfx_fill_box(fb, x0 - 1, y0 - 1, x1 + 1, y1 + 1, edge, 0);
        gfx_fill_box(fb, x0, y0, x1, y1, body, 0);
    }
}

void mascot_pose_draw(uint16_t *fb, float ox, float oy, float s, const mascot_pose_t *p)
{
    mascot_pose_draw_body(fb, ox, oy, s, p);
    mascot_pose_draw_hands(fb, ox, oy, s, p);
}
#undef BOX

void draw_spark_orbit(uint16_t *fb, float cx, float head_y, float t, float fade)
{
    static const char *rows[7] = {"...#...", ".#.#.#.", "..###..", "#######", "..###..", ".#.#.#.", "...#..."};
    if (fade <= 0.05f) return;
    for (int k = 0; k < 3; k++) {
        float a = t * 4.5f + k * 2.0944f, front = sinf(a);
        float x = cx + 36 * fade * cosf(a), y = head_y - 10 + 6 * front;
        float cell = front > 0 ? 2.0f : 1.4f; /* nearer sparks are bigger and brighter */
        uint16_t c = gfx_color(front > 0 ? 0xF0956A : 0xB8603F);
        for (int r = 0; r < 7; r++)
            for (int q = 0; q < 7; q++)
                if (rows[r][q] == '#')
                    gfx_fill_box(fb, x + (q - 3.5f) * cell, y + (r - 3.5f) * cell, x + (q - 2.5f) * cell, y + (r - 2.5f) * cell, c, 0);
    }
}

void draw_heart(uint16_t *fb, float x, float y, float cell, uint16_t color)
{
    static const char *rows[] = {".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."};
    for (int r = 0; r < 6; r++)
        for (int c = 0; c < 7; c++)
            if (rows[r][c] == '#') gfx_fill_box(fb, x + c * cell, y + r * cell, x + (c + 1) * cell, y + (r + 1) * cell, color, 0);
}
