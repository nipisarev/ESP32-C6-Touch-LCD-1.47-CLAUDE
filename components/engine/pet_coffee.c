#include "pet_coffee.h"

#include <math.h>
#include <string.h>

#include "font.h"
#include "gfx.h"
#include "mascot_pose.h"
#include "scenes.h"

#define TABLE_Y 72.0f
#define ITEM_BASE 104.0f /* items stand on the table here */
#define CELL 5.0f
#define MUG_X 186.0f
#define MUG_TOP 112.0f
#define POUR_Y (MUG_TOP - 22) /* where the stream leaves the item */
#define ADD_T 2.2f
#define POUR_T0 0.95f
#define POUR_T1 1.45f
#define PAUSE_T 0.4f
#define DRINK_T 2.6f
#define HAND_W 38.0f
#define HAND_H 40.0f

enum { ING_COFFEE = 1, ING_MILK = 2, ING_WATER = 4, ING_ALL = 7 };

static uint32_t pal(char c)
{
    switch (c) {
    case 'B': return 0x4A2E1E; /* sachet edge */
    case 'b': return 0x7A4E30; /* sachet */
    case 'o': return 0x2E1C12; /* coffee bean */
    case 'w': return 0xF2F2F2;
    case 'g': return 0xC8CCD4; /* carton shade */
    case 'u': return 0x3D74B8;
    case 'U': return 0x2A5490;
    case 'r': return 0xC8403A; /* kettle */
    case 'R': return 0x8A2A26;
    case 'k': return 0x2A2A30;
    default: return 0;
    }
}

static uint32_t mix(uint32_t a, uint32_t b, float k)
{
    int r = (int)lerpf((a >> 16) & 0xFF, (b >> 16) & 0xFF, k), g = (int)lerpf((a >> 8) & 0xFF, (b >> 8) & 0xFF, k),
        bl = (int)lerpf(a & 0xFF, b & 0xFF, k);
    return (uint32_t)(r << 16 | g << 8 | bl);
}

static const char *const k_sachet[] = {"BbBbBbBb", "bbbbbbbb", "bbbbbbbb", "wwwwwwww", "wwwoowww", "wwooooww",
                                       "wwwoowww", "wwwwwwww", "bbbbbbbb", "bbbbbbbb", "BBBBBBBB"};
static const char *const k_milk[] = {"...gg...", "..wwgg..", ".wwwwgg.", "wwwwwggg", "uuuuuuuu", "wwwwwwww",
                                     "wwwuwwww", "wwuuuwww", "wwuuuwww", "wwwwwwww", "uuuuuuuu", "UUUUUUUU"};
static const char *const k_kettle[] = {"....RRRRR....", "....R...R....", "...rrrkrrr...", "r.rrwwrrrrr..", "rrrrwrrrrrrr.",
                                       ".rrrrrrrrrrr.", "..rrrrrrrrrr.", "..rrrrrrrrr..", "...RRRRRRR..."};

typedef struct {
    const char *const *rows;
    int nr, ing;
    float x;
    float lx, ly; /* pour point, cells from the sprite's top-left */
    float tilt;   /* pouring angle, radians (negative tips the left side down) */
    float amount; /* how much it fills the mug */
} item_t;

static const item_t k_items[3] = {
    {k_sachet, 11, ING_COFFEE, 100, 0.5f, 0, -1.9f, 0.15f},
    {k_milk, 12, ING_MILK, 186, 0.5f, 3, -1.2f, 0.30f},
    {k_kettle, 9, ING_WATER, 272, 0, 3.5f, -0.8f, 0.55f},
};

static int s_used, s_adding;
static float s_add_t, s_done_t, s_level, s_level_from;
static uint32_t s_col_from, s_col_to;

static uint32_t mix_color(int m)
{
    static const uint32_t k[8] = {0x3A2E2A, 0x7A4A26, 0xF0F0F0, 0x9A6A44, 0xA8C8D8, 0x5A341C, 0xD8E2E8, 0xB88458};
    return k[m & ING_ALL];
}

static float item_w(const item_t *it) { return (float)strlen(it->rows[0]) * CELL; }
static float item_h(const item_t *it) { return it->nr * CELL; }
static float rest_cy(const item_t *it) { return ITEM_BASE - item_h(it) / 2; }

static void rot(float ang, float x, float y, float *ox, float *oy)
{
    float c = cosf(ang), s = sinf(ang);
    *ox = x * c - y * s;
    *oy = x * s + y * c;
}

/* The pour point relative to the item's centre at angle `ang`. */
static void pour_point(const item_t *it, float ang, float *px, float *py)
{
    rot(ang, -item_w(it) / 2 + it->lx * CELL, -item_h(it) / 2 + it->ly * CELL, px, py);
}

void coffee_start(void)
{
    s_used = 0;
    s_adding = -1;
    s_done_t = -1;
    s_level = 0;
}

bool coffee_update(const input_t *in, float dt)
{
    if (s_adding >= 0) {
        const item_t *it = &k_items[s_adding];
        s_add_t += dt;
        s_level = s_level_from + it->amount * clampf((s_add_t - POUR_T0) / (POUR_T1 - POUR_T0), 0, 1);
        if (s_add_t >= ADD_T) {
            s_used |= it->ing;
            s_adding = -1;
            if (s_used == ING_ALL) s_done_t = 0;
        }
        return false;
    }
    if (s_done_t >= 0) return (s_done_t += dt) >= PAUSE_T + DRINK_T;
    if (in->tap)
        for (int i = 0; i < 3; i++) {
            const item_t *it = &k_items[i];
            float hw = item_w(it) / 2 + 6, top = ITEM_BASE - item_h(it) - 8;
            if ((s_used & it->ing) || fabsf(in->start_x - it->x) > hw || in->start_y < top || in->start_y > ITEM_BASE + 8) continue;
            s_adding = i;
            s_add_t = 0;
            s_level_from = s_level;
            s_col_from = mix_color(s_used);
            s_col_to = mix_color(s_used | it->ing);
            break;
        }
    return false;
}

static void item_draw(uint16_t *fb, const item_t *it, float cx, float cy, float ang, bool used)
{
    const float w = item_w(it), h = item_h(it);
    for (int r = 0; r < it->nr; r++)
        for (int c = 0; it->rows[r][c]; c++) {
            char ch = it->rows[r][c];
            if (ch == '.') continue;
            uint32_t rgb = pal(ch);
            if (used) rgb = mix(rgb, 0x3A2A22, 0.55f);
            float x0 = -w / 2 + c * CELL, y0 = -h / 2 + r * CELL;
            if (ang == 0)
                gfx_fill_box(fb, cx + x0, cy + y0, cx + x0 + CELL, cy + y0 + CELL, gfx_color(rgb), 0);
            else
                gfx_fill_rot_box(fb, cx, cy, ang, x0, y0, x0 + CELL + 0.6f, y0 + CELL + 0.6f, gfx_color(rgb));
        }
}

static void puffs(uint16_t *fb, float x, float y, float t, int n, float rise)
{
    for (int i = 0; i < n; i++) {
        float ph = fmodf(t * 1.2f + i / (float)n, 1.0f);
        float sx = x + 3 * sinf(ph * 6.28f + i * 2.1f) + (i % 2 ? 4 : -4);
        gfx_fill_box(fb, sx, y - ph * rise, sx + 3, y - ph * rise + 3, gfx_color(ph < 0.7f ? 0xD0D0D8 : 0x8A8A94), 0);
    }
}

/* Rows of an ellipse inside the box (cx +- rx, y0 .. y0 + h), only from `from` (0 top .. 1 bottom) down. */
static void ellipse_rows(uint16_t *fb, float cx, float y0, float rx, float h, float from, uint16_t col)
{
    for (float y = floorf(y0 + from * h); y < y0 + h; y += 1) {
        float v = (y + 0.5f - (y0 + h / 2)) / (h / 2);
        if (fabsf(v) >= 1) continue;
        float hw = rx * sqrtf(1 - v * v);
        gfx_fill_box(fb, cx - hw, y, cx + hw, y + 1, col, 0);
    }
}

static void mug_draw(uint16_t *fb, float cx, float top, float sc, float level, uint32_t liquid)
{
    const float rx = 30 * sc, rim_h = 16 * sc;
    gfx_fill_box(fb, cx - rx, top + rim_h / 2, cx + rx, GFX_H, gfx_color(0xF2F2F2), 0);
    gfx_fill_box(fb, cx + rx - 8 * sc, top + rim_h / 2, cx + rx, GFX_H, gfx_color(0xD0D0D8), 0);
    gfx_fill_box(fb, cx - rx, top + 26 * sc, cx + rx, top + 32 * sc, gfx_color(0x3D74B8), 0);
    ellipse_rows(fb, cx, top, rx, rim_h, 0, gfx_color(0xE0E2E8));
    ellipse_rows(fb, cx, top + 2 * sc, rx - 4 * sc, rim_h - 4 * sc, 0, gfx_color(0x3A2E2A));
    if (level > 0.01f) ellipse_rows(fb, cx, top + 2 * sc, rx - 4 * sc, rim_h - 4 * sc, 1 - clampf(level, 0, 1), gfx_color(liquid));
}

static void hand_draw(uint16_t *fb, float cx, float cy, float sc)
{
    const float w = HAND_W * sc / 2, h = HAND_H * sc / 2;
    gfx_fill_box(fb, cx - w - 2, cy - h - 2, cx + w + 2, cy + h + 2, gfx_color(0xA8503A), 0);
    gfx_fill_box(fb, cx - w, cy - h, cx + w, cy + h, gfx_color(MASCOT_RGB), 0);
}

static void stream_draw(uint16_t *fb, const item_t *it, float x, float y0, float y1, float t)
{
    if (it->ing == ING_COFFEE) { /* granules */
        for (int k = 0; k < 8; k++) {
            float ph = fmodf(t * 3 + k / 8.0f, 1.0f), y = y0 + (y1 - y0) * ph, dx = 2.5f * sinf(k * 2.3f + t * 9);
            gfx_fill_box(fb, x + dx - 1.5f, y - 1.5f, x + dx + 1.5f, y + 1.5f, gfx_color(0x3A2416), 0);
        }
        return;
    }
    const float w = it->ing == ING_MILK ? 2.0f : 1.5f;
    gfx_fill_box(fb, x - w, y0, x + w, y1, gfx_color(it->ing == ING_MILK ? 0xF2F2F2 : 0xB8D8E8), 0);
    if (it->ing == ING_WATER) {
        gfx_fill_box(fb, x - 0.5f, y0, x + 0.5f, y1, gfx_color(0xE8F4FA), 0);
        puffs(fb, x + 10, y1 - 4, t, 4, 30); /* boiling water steams */
    }
}

void coffee_draw(uint16_t *fb, float t)
{
    /* kitchen wall with tiles, then the table top */
    gfx_fill_box(fb, 0, 0, GFX_W, TABLE_Y, gfx_color(0x2C2630), 0);
    for (int row = 0; row * 18 < TABLE_Y; row++) {
        float y = row * 18.0f;
        gfx_fill_box(fb, 0, y, GFX_W, y + 1, gfx_color(0x3C3442), 0);
        for (float x = (row % 2) * 9.0f; x < GFX_W; x += 18) gfx_fill_box(fb, x, y, x + 1, y + 18, gfx_color(0x3C3442), 0);
    }
    gfx_fill_box(fb, 0, TABLE_Y, GFX_W, GFX_H, gfx_color(0x8A5A3A), 0);
    gfx_fill_box(fb, 0, TABLE_Y, GFX_W, TABLE_Y + 3, gfx_color(0x5E3E26), 0);
    static const float grain[4][3] = {{90, 60, 150}, {118, 210, 300}, {140, 40, 120}, {162, 150, 260}};
    for (int g = 0; g < 4; g++) gfx_fill_box(fb, grain[g][1], grain[g][0], grain[g][2], grain[g][0] + 1, gfx_color(0x7A4E32), 0);

    /* drinking: the mug comes up to the camera and empties in three gulps */
    float dk = s_done_t >= 0 ? s_done_t - PAUSE_T : -1;
    float up = dk > 0 ? ease_inout2(dk / 0.6f) : 0;
    float level = s_level;
    if (dk > 0)
        for (int g = 0; g < 3; g++) level -= clampf((dk - 0.8f - g * 0.5f) / 0.25f, 0, 1) / 3;

    for (int i = 0; i < 3; i++) {
        const item_t *it = &k_items[i];
        float w = item_w(it);
        gfx_fill_box(fb, it->x - w / 2 - 3, ITEM_BASE - 2, it->x + w / 2 + 3, ITEM_BASE + 3, gfx_color(0x6A4630), 0);
        if (i == s_adding) continue;
        bool used = s_used & it->ing;
        float bob = used || s_adding >= 0 || s_done_t >= 0 ? 0 : 3 * fmaxf(0, sinf(t * 3 - i * 2.1f));
        item_draw(fb, it, it->x, rest_cy(it) - bob, 0, used);
        if (it->ing == ING_WATER && !used) puffs(fb, it->x - w / 2 + 2, rest_cy(it) - 6, t, 3, 22);
    }

    const float sc = 1 + 0.7f * up, top = MUG_TOP - 84 * up;
    uint32_t liquid = mix_color(s_used);
    if (s_adding >= 0) liquid = mix(s_col_from, s_col_to, clampf((s_add_t - POUR_T0) / (POUR_T1 - POUR_T0), 0, 1));
    mug_draw(fb, MUG_X, top, sc, level, liquid);
    hand_draw(fb, MUG_X - 35 * sc, top + 46 * sc, sc);

    float rhx = MUG_X + 35 * sc, rhy = top + 46 * sc, rsc = sc;
    if (s_adding >= 0) { /* the right hand fetches the item, pours over the mug and puts it back */
        const item_t *it = &k_items[s_adding];
        const float a = s_add_t, h = item_h(it);
        float lx, ly;
        pour_point(it, it->tilt, &lx, &ly);
        const float px = MUG_X - lx, py = POUR_Y - ly; /* centre that puts the pour point over the mug */
        float move = a < 0.8f ? ease_inout2((a - 0.35f) / 0.45f) : a < 1.6f ? 1 : 1 - ease_inout2((a - 1.6f) / 0.35f);
        float ang = it->tilt * (a < 1.45f ? ease_inout2((a - 0.8f) / 0.15f) : 1 - ease_inout2((a - 1.45f) / 0.15f));
        float cx = lerpf(it->x, px, move), cy = lerpf(rest_cy(it), py, move);
        float gx, gy;
        rot(ang, 6, h / 2 + 8, &gx, &gy); /* holds it from below */
        float grip_x = cx + gx, grip_y = cy + gy;
        float reach = a < 0.35f ? ease_inout2(a / 0.35f) : a < 1.95f ? 1 : 1 - ease_inout2((a - 1.95f) / 0.25f);
        rhx = lerpf(rhx, grip_x, reach);
        rhy = lerpf(rhy, grip_y, reach);
        rsc = lerpf(1, 0.8f, reach); /* the hand looks smaller out on the table */
        if (a > POUR_T0 && a < POUR_T1) {
            float sx, sy;
            pour_point(it, ang, &sx, &sy);
            stream_draw(fb, it, cx + sx, cy + sy, MUG_TOP + 8, t);
        }
        item_draw(fb, it, cx, cy, ang, false);
    }
    hand_draw(fb, rhx, rhy, rsc);

    if (dk > 0) {
        for (int g = 0; g < 3; g++) {
            float k = dk - 0.8f - g * 0.5f;
            if (k > 0 && k < 0.4f) font_draw_centered(fb, (int)(MUG_X + 74), 30 - (int)(k * 20), 2, gfx_color(0xFFFFFF), "GULP");
        }
        float fade = (dk - 2.0f) / 0.6f;
        if (fade > 0.75f) gfx_clear(fb, GFX_HEX(0x000000));
        else if (fade > 0.25f) gfx_fill_box_dither(fb, 0, 0, GFX_W, GFX_H, GFX_HEX(0x000000));
    }
}
