/* POOL: half the screen is water that follows real gravity from the accelerometer. Tilt the board and
 * the surface leans and sloshes; jolts make waves; Claude and a rubber duck float, drift downhill and
 * splash. Tilt Claude into the stars to collect them; tap to make it jump. */
#include <math.h>
#include <stdio.h>

#include "font.h"
#include "gfx.h"
#include "mascot_pose.h"
#include "scenes.h"

#define COLS 64
#define COL_W (GFX_W / (float)COLS)
#define LEVEL 86.0f        /* mean surface: half the screen */
#define SLOSH_K 40.0f      /* (2*pi*1 Hz)^2: the surface swings about once per second */
#define SLOSH_D 2.0f
#define MAX_SLOPE 1.1f
#define G_PX 600.0f        /* px/s^2 per g */
#define S 0.55f            /* mascot scale */
#define MW (MASCOT_W * S)
#define MH (MASCOT_H * S)
#define MAX_DROPS 24

typedef struct {
    float x, y, vx, vy;    /* centre */
    float w, h, buoy, drag;
} floater_t;

typedef struct {
    float x, y, vx, vy, life;
} drop_t;

static float s_slope, s_slope_v, s_off, s_r[COLS], s_rv[COLS], s_t, s_hint, s_star_x, s_star_t, s_happy;
static floater_t s_claude, s_duck;
static drop_t s_drops[MAX_DROPS];
static int s_stars;

static float surface_at(float x)
{
    int i = (int)clampf(x / COL_W, 0, COLS - 1);
    return clampf(LEVEL + s_off + s_slope * (x - GFX_W / 2) + s_r[i], 2, GFX_H - 2);
}

/* Keep the volume: shift the tilted surface so that exactly half the screen stays under water. */
static void conserve_volume(void)
{
    const float want = GFX_W * (GFX_H - LEVEL);
    float lo = -400, hi = 400;
    for (int it = 0; it < 18; it++) {
        float off = (lo + hi) / 2, area = 0;
        for (int i = 0; i < COLS; i++) {
            float y = LEVEL + off + s_slope * ((i + 0.5f) * COL_W - GFX_W / 2);
            area += (GFX_H - clampf(y, 0, GFX_H)) * COL_W;
        }
        if (area > want) lo = off; /* too much water: move the surface down */
        else hi = off;
    }
    s_off = (lo + hi) / 2;
}

static void splash(float x, float strength)
{
    int c = (int)clampf(x / COL_W, 0, COLS - 1);
    for (int i = -2; i <= 2; i++)
        if (c + i >= 0 && c + i < COLS) s_rv[c + i] += strength * (i ? 0.5f : 1.0f);
    for (int k = 0, n = 0; k < MAX_DROPS && n < 6; k++)
        if (s_drops[k].life <= 0) {
            s_drops[k] = (drop_t){x + rng_float(-10, 10), surface_at(x) - 2, rng_float(-70, 70), -rng_float(80, 180) * fminf(1, strength / 6), 0.8f};
            n++;
        }
}

static void new_star(void)
{
    float x;
    do x = rng_float(30, GFX_W - 30);
    while (fabsf(x - s_claude.x) < 70);
    s_star_x = x;
    s_star_t = 0;
}

static void enter(uint32_t now)
{
    (void)now;
    s_slope = s_slope_v = s_off = s_t = s_happy = 0;
    s_hint = 3;
    for (int i = 0; i < COLS; i++) s_r[i] = s_rv[i] = 0;
    for (int k = 0; k < MAX_DROPS; k++) s_drops[k].life = 0;
    s_claude = (floater_t){GFX_W / 2, LEVEL, 0, 0, MW, MH, 2.2f, 2.5f};
    s_duck = (floater_t){70, LEVEL - 4, 0, 0, 20, 16, 3.2f, 2.0f};
    s_stars = 0;
    new_star();
}

/* gravity, buoyancy (opposite to gravity, by submerged depth), drag, sloshing current, walls */
static void float_step(floater_t *f, const input_t *in, float dt)
{
    float gx = in->gix, gy = in->giy;
    float sub = clampf((f->y + f->h / 2 - surface_at(f->x)) / f->h, 0, 1); /* submerged fraction */
    float b = f->buoy * sub;
    f->vx += (gx - gx * b) * G_PX * dt;
    f->vy += (gy - gy * b) * G_PX * dt;
    if (sub > 0) {
        f->vx += -s_slope_v * 260 * dt; /* the sloshing water carries it along */
        float d = expf(-f->drag * dt);
        f->vx *= d;
        f->vy *= d;
    }
    f->vx += -in->jx * 900 * dt; /* jolts push it like in a shaken box */
    f->vy += -in->jy * 900 * dt;
    bool was_out = f->y + f->h / 2 < surface_at(f->x) + 2;
    f->x += f->vx * dt;
    f->y += f->vy * dt;
    if (f->x < f->w / 2) { f->x = f->w / 2; f->vx = fabsf(f->vx) * 0.5f; }
    if (f->x > GFX_W - f->w / 2) { f->x = GFX_W - f->w / 2; f->vx = -fabsf(f->vx) * 0.5f; }
    if (f->y < f->h / 2) { f->y = f->h / 2; f->vy = fabsf(f->vy) * 0.4f; }
    if (f->y > GFX_H - f->h / 2) { f->y = GFX_H - f->h / 2; f->vy = -fabsf(f->vy) * 0.4f; }
    if (was_out && f->y + f->h / 2 >= surface_at(f->x) + 2 && f->vy > 90) splash(f->x, f->vy / 40); /* belly flop */
}

static void update(const input_t *in, uint32_t now, float dt)
{
    (void)now;
    s_t += dt;
    if (s_hint > 0) s_hint -= dt;
    if (s_happy > 0) s_happy -= dt;
    /* the surface leans perpendicular to the (instantaneous) gravity and swings towards it */
    float gy = fabsf(in->giy) < 0.15f ? copysignf(0.15f, in->giy ? in->giy : 1) : in->giy;
    float target = clampf(-in->gix / gy, -MAX_SLOPE, MAX_SLOPE);
    if (in->giy < 0) target = -target; /* upside down: keep it sane */
    s_slope_v += (SLOSH_K * (target - s_slope) - SLOSH_D * s_slope_v) * dt;
    s_slope += s_slope_v * dt;
    conserve_volume();
    /* ripples: springs per column plus spreading, a few passes per frame */
    float jolt = sqrtf(in->jx * in->jx + in->jy * in->jy + in->jz * in->jz);
    if (jolt > 0.8f) s_rv[rng_next() % COLS] += rng_float(-6, 6) * jolt;
    for (int i = 0; i < COLS; i++) {
        s_rv[i] += (-0.6f * s_r[i]) - 0.05f * s_rv[i];
        s_r[i] += s_rv[i] * dt * 10;
    }
    for (int pass = 0; pass < 3; pass++)
        for (int i = 0; i < COLS; i++) {
            float l = i > 0 ? s_r[i - 1] : s_r[i], r = i < COLS - 1 ? s_r[i + 1] : s_r[i];
            s_rv[i] += 0.12f * (l + r - 2 * s_r[i]);
        }
    for (int i = 0; i < COLS; i++) s_r[i] = clampf(s_r[i], -24, 24);

    if (in->tap && s_claude.y + s_claude.h / 2 > surface_at(s_claude.x) - 4) { /* jump out of the water */
        s_claude.vy -= 260 * fmaxf(0.4f, in->giy);
        s_claude.vx -= 260 * in->gix;
        splash(s_claude.x, 4);
        for (int k = 0, n = 0; k < MAX_DROPS && n < 3; k++) /* the snorkel blows out a spout */
            if (s_drops[k].life <= 0) {
                float tx = s_claude.x - MW / 2 + 96 * S, ty = s_claude.y - MH / 2 - 30 * S;
                s_drops[k] = (drop_t){tx, ty, rng_float(-30, 30), -rng_float(120, 200), 0.7f};
                n++;
            }
    }
    float_step(&s_claude, in, dt);
    float_step(&s_duck, in, dt);

    for (int k = 0; k < MAX_DROPS; k++) {
        drop_t *d = &s_drops[k];
        if (d->life <= 0) continue;
        d->life -= dt;
        d->vy += in->giy * G_PX * dt;
        d->vx += in->gix * G_PX * dt;
        d->x += d->vx * dt;
        d->y += d->vy * dt;
        if (d->vy > 0 && d->y > surface_at(d->x)) d->life = 0;
    }
    s_star_t += dt;
    float sy = LEVEL - 16;
    if (fabsf(s_claude.x - s_star_x) < MW / 2 + 6 && fabsf(s_claude.y - MH / 2 - sy) < MH) {
        s_stars++;
        s_happy = 0.8f;
        new_star();
    }
}

/* Diving mask over the eyes and a J-shaped snorkel on the right (mascot units, scale S). */
static void snorkel_gear(uint16_t *fb, float ox, float oy, float sc)
{
#define B(x0, y0, x1, y1, c) gfx_fill_box(fb, ox + (x0) * sc, oy + (y0) * sc, ox + (x1) * sc, oy + (y1) * sc, c, 0)
    const uint16_t frame = gfx_color(0x2A4E8A), strap = gfx_color(0x1E2E48), tube = gfx_color(0xF2C230), tip = gfx_color(0xF08A2A);
    B(11, 13, 17, 19, strap); /* strap round the head */
    B(90, 13, 96, 19, strap);
    B(16, 6, 91, 9, frame);   /* mask frame */
    B(16, 24, 91, 27, frame);
    B(16, 6, 19, 27, frame);
    B(88, 6, 91, 27, frame);
    B(52, 9, 55, 24, frame);  /* nose bridge between the two lenses */
    gfx_fill_box_dither(fb, ox + 19 * sc, oy + 9 * sc, ox + 52 * sc, oy + 24 * sc, gfx_color(0x9FDDF5)); /* glass */
    gfx_fill_box_dither(fb, ox + 55 * sc, oy + 9 * sc, ox + 88 * sc, oy + 24 * sc, gfx_color(0x9FDDF5));
    B(92, -26, 99, 26, tube);  /* snorkel tube */
    B(84, 22, 99, 28, tube);   /* mouthpiece */
    B(91, -30, 100, -24, tip); /* top */
#undef B
}

static void star(uint16_t *fb, float cx, float cy, float cell, uint16_t c)
{
    static const char *rows[7] = {"...#...", "..###..", "#######", ".#####.", "..###..", ".##.##.", "##...##"};
    for (int r = 0; r < 7; r++)
        for (int k = 0; k < 7; k++)
            if (rows[r][k] == '#') gfx_fill_box(fb, cx + (k - 3.5f) * cell, cy + (r - 3.5f) * cell, cx + (k - 2.5f) * cell, cy + (r - 2.5f) * cell, c, 0);
}

static void duck(uint16_t *fb, float x, float y, bool face_left)
{
    static const char *rows[8] = {"....yy....", "...yyyy...", "...yykyo..", "...yyyyoo.", "y..yyyy...", "yyyyyyyyy.", "yyyyyyyyy.", ".yyyyyyy.."};
    for (int r = 0; r < 8; r++)
        for (int k = 0; k < 10; k++) {
            char c = rows[r][k];
            if (c == '.') continue;
            uint32_t col = c == 'y' ? 0xF7D23C : c == 'o' ? 0xF08A2A : 0x202020;
            float px = face_left ? x + (9 - k) * 2 : x + k * 2;
            gfx_fill_box(fb, px, y + r * 2, px + 2, y + r * 2 + 2, gfx_color(col), 0);
        }
}

static void draw(uint16_t *fb, uint32_t now)
{
    (void)now;
    char buf[16];
    gfx_clear(fb, gfx_color(0x0A1420));
    for (int x = 0; x < GFX_W; x += 16) gfx_fill_rect(fb, x, 0, 1, GFX_H, gfx_color(0x16263A)); /* pool tiles */
    for (int y = 0; y < GFX_H; y += 16) gfx_fill_rect(fb, 0, y, GFX_W, 1, gfx_color(0x16263A));

    float pulse = 1 + 0.15f * sinf(s_star_t * 6);
    star(fb, s_star_x, LEVEL - 16 + 3 * sinf(s_star_t * 2.5f), 2 * pulse, gfx_color(0xF7D23C));

    /* Claude: paddles when in the water, tucks its legs in the air */
    mascot_pose_t p = MASCOT_POSE_REST;
    const floater_t *c = &s_claude;
    bool in_water = c->y + c->h / 2 > surface_at(c->x) + 2;
    if (in_water) {
        float k = sinf(s_t * 9);
        p.leg_lift[0] = p.leg_lift[2] = k > 0 ? k : 0;
        p.leg_lift[1] = p.leg_lift[3] = k < 0 ? -k : 0;
        p.hand_dy[0] = 6 * sinf(s_t * 6);
        p.hand_dy[1] = -6 * sinf(s_t * 6);
    } else {
        p.leg_lift[0] = p.leg_lift[1] = p.leg_lift[2] = p.leg_lift[3] = 0.6f;
        p.hands_dy = -8;
    }
    p.eye_dx = clampf(c->vx / 30, -4, 4);
    p.eye_dy = clampf(c->vy / 40, -3, 3);
    p.happy = s_happy > 0;
    mascot_pose_draw(fb, c->x - MW / 2, c->y - MH / 2, S, &p);
    snorkel_gear(fb, c->x - MW / 2, c->y - MH / 2, S);
    duck(fb, s_duck.x - 10, s_duck.y - 8, s_duck.vx < 0);

    /* water: see-through dither below the surface, a bright surface line */
    const uint16_t water = gfx_color(0x3A9AE0), foam = gfx_color(0xBEE8FF);
    for (int i = 0; i < COLS; i++) {
        float x0 = i * COL_W, y = surface_at(x0 + COL_W / 2);
        gfx_fill_box_dither(fb, x0, y, x0 + COL_W, GFX_H, water);
        gfx_fill_box(fb, x0, y - 1, x0 + COL_W, y + 1, foam, 0);
    }
    for (int k = 0; k < MAX_DROPS; k++)
        if (s_drops[k].life > 0) gfx_fill_box(fb, s_drops[k].x, s_drops[k].y, s_drops[k].x + 3, s_drops[k].y + 3, foam, 0);

    star(fb, 12, 12, 1.4f, gfx_color(0xF7D23C));
    snprintf(buf, sizeof(buf), "%d", s_stars);
    font_draw(fb, 22, 6, 2, gfx_color(0xFFFFFF), buf);
    if (s_hint > 0) font_draw_centered(fb, GFX_W / 2, 30, 2, gfx_color(0xFFFFFF), "TILT ME. TAP TO JUMP");
}

const scene_t scene_pool = {enter, update, draw, 0};
