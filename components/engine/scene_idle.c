/* "SHOW" mode: a director that strings ambient acts (breathe, look around, walk, think,
 * tap foot, dance, sleep) between the four original showpieces. The mascot's position is
 * continuous: every act starts and ends in the base pose at the current x, and the mascot
 * walks to wherever a showpiece has to start. */
#include <math.h>

#include "acts.h"
#include "room.h"
#include "font.h"
#include "gfx.h"
#include "mascot_anim.h"
#include "mascot_pose.h"
#include "scenes.h"
#include "toss.h"

#define GROUND 170.0f
#define POSE_OY (GROUND - MASCOT_H)
#define BODY_CX 53.5f      /* body centre in the base pose / Walking viewBox */
#define CENTER_X 160.0f
#define WALK_SPEED 53.25f  /* the original walk: 117 units in 2.2 s */
#define HOP_S 0.33f
#define LOOK_ORIG_END 3.7f /* Walking timeline: lean left, lean right, look down */
#define WALKJUMP_END 10.51f
#define WALKJUMP_TRAVEL 213.0f
#define FLAG_LOOPS 6
/* Sleep: yawn, bed slides in, hop into bed, blanket up, eyes close | loop | eyes open, blanket off,
 * hop out, bed slides away, big stretch. */
#define SL_YAWN 1.4f
#define SL_BED_IN 0.7f
#define SL_HOP_IN 0.5f
#define SL_BLANKET 0.6f
#define SL_EYES 0.8f
#define SL_WAKE_EYES 1.4f
#define SL_BLANKET_OFF 0.5f
#define SL_HOP_OUT 0.5f
#define SL_BED_OUT 0.6f
#define SL_STRETCH 1.8f
#define SL_BEFORE_LOOP (SL_YAWN + SL_BED_IN + SL_HOP_IN + SL_BLANKET + SL_EYES)
#define SL_AFTER_LOOP (SL_WAKE_EYES + SL_BLANKET_OFF + SL_HOP_OUT + SL_BED_OUT + SL_STRETCH)
/* Dizzy after a shake: (startled out of bed) | jiggle while shaken | spiral eyes + sparks | recover */
#define DZ_STARTLE 0.6f
#define DZ_SPIN 3.0f
#define DZ_RECOVER 0.9f
#define FLAG_LOWER_S 0.21f
#define FLOWER_WILTING 0.25f /* thirst from which a tap on the flower sends the mascot to water it */

typedef enum {
    ACT_IDLE, ACT_LOOK, ACT_WALK, ACT_THINK, ACT_TAP, ACT_DANCE, ACT_SLEEP, ACT_DIZZY,
    ACT_NOTEBOOK, ACT_PHONE, ACT_COFFEE, ACT_SNACK, ACT_WATER, ACT_PARK, /* activities with props (acts.c) */
    ACT_SHOW_WALKJUMP, ACT_SHOW_GYM, ACT_SHOW_FLAG, ACT_SHOW_CONFETTI,
} act_t;

static struct {
    act_t act, last_act, last_prop; /* last_prop: the last act with props, not repeated even across ambient acts */
    float t, dur;
    float cx, walk_from, walk_to, sleep_loop;
    int queued;      /* showpiece act to start after walking, or -1 */
    int last_show;
    float since_show, show_every, since_sleep;
    float blink_in, blink_t, glance_in, glance_t, glance_dx, glance_dy, happy_t;
    float clock, daylight, day_target, day_in, since_park, thirst;
    float dizzy_end, jx, jy, level; /* dizzy: time the shaking stopped (-1 while shaken), live jolt */
    bool startled;
    bool want_water; /* the wilting flower was tapped: water it after the current act */
    toss_t toss;
} D;

/* ---------- acts ---------- */

static float act_duration(act_t a)
{
    switch (a) {
    case ACT_IDLE: return rng_float(2.5f, 5.0f);
    case ACT_LOOK: return LOOK_ORIG_END + 0.25f;
    case ACT_WALK: return HOP_S + fabsf(D.walk_to - D.walk_from) / WALK_SPEED + 0.2f;
    case ACT_THINK: return 5.0f;
    case ACT_TAP: return 3.6f;
    case ACT_DANCE: return 4.0f;
    case ACT_SLEEP: return SL_BEFORE_LOOP + D.sleep_loop + SL_AFTER_LOOP;
    case ACT_DIZZY: return 60; /* open-ended while shaken; set when the shaking stops */
    case ACT_NOTEBOOK: case ACT_PHONE: case ACT_COFFEE: case ACT_SNACK: case ACT_WATER: case ACT_PARK:
        return pact_duration((pact_t)(a - ACT_NOTEBOOK));
    case ACT_SHOW_WALKJUMP: return WALKJUMP_END;
    case ACT_SHOW_GYM: return 7.125f;
    case ACT_SHOW_FLAG: return 0.84f + 0.63f * FLAG_LOOPS + FLAG_LOWER_S;
    default: return 5.0f; /* confetti: ends on the rest frame */
    }
}

#ifdef HOST_DEBUG
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static float s_clock;
static int forced_done;
#endif

static void start(act_t a)
{
#ifdef HOST_DEBUG
    static const char *names[] = {"idle", "look", "walk", "think", "tap", "dance", "sleep", "dizzy", "notebook", "phone", "coffee", "snack", "water", "park",
                                  "SHOW-walkjump", "SHOW-gym", "SHOW-flag", "SHOW-confetti"};
    printf("  t=%7.2f  %-14s cx=%6.1f\n", s_clock, names[a], D.cx);
#endif
    D.last_act = D.act;
    if (a >= ACT_NOTEBOOK && a <= ACT_PARK) {
        D.last_prop = a;
        pact_begin((pact_t)(a - ACT_NOTEBOOK));
    }
    D.act = a;
    D.t = 0;
    if (a == ACT_SLEEP) D.sleep_loop = rng_float(12, 20);
    D.dur = act_duration(a);
}

static void walk_to(float x, int then)
{
    D.walk_from = D.cx;
    D.walk_to = x;
    D.queued = then;
    start(ACT_WALK);
}

/* Prop acts happen at their own spot: walk there first. */
static void go_do(act_t a)
{
    float x = a == ACT_PARK ? PACT_PARK_START_X : a == ACT_WATER ? PACT_WATER_X : clampf(D.cx, 100, 220);
    if (fabsf(D.cx - x) > 2) walk_to(x, a);
    else start(a);
}

/* Freeze a walk where the mascot is now. */
static void stop_walk(void)
{
    float dir = D.walk_to > D.walk_from ? 1 : -1, walk_dur = D.dur - HOP_S - 0.2f;
    D.cx = D.walk_from + dir * WALK_SPEED * clampf(D.t - HOP_S, 0, walk_dur);
    D.queued = -1;
}

static void begin_show(int show)
{
    D.last_show = show;
    D.since_show = 0;
    D.show_every = rng_float(50, 80); /* leaves room for the prop acts */
    start((act_t)show);
}

static bool allowed(act_t a)
{
    if (a == D.last_act || a == D.last_prop) return false;
    if (a == ACT_SLEEP && D.since_sleep <= 90) return false;
    if (a == ACT_PARK && D.since_park <= 40) return false; /* often, but not all the time */
    return true;
}

static void pick_next(void)
{
#ifdef HOST_DEBUG
    const char *force = getenv("SHOW_FORCE"); /* e.g. sleep, walkjump: run that act first */
    if (force && !forced_done && D.act == ACT_IDLE) {
        forced_done = 1;
        if (!strcmp(force, "sleep")) { start(ACT_SLEEP); return; }
        if (!strcmp(force, "walkjump")) { walk_to(BODY_CX, ACT_SHOW_WALKJUMP); return; }
        if (!strcmp(force, "tap")) { start(ACT_TAP); return; }
        static const char *acts[] = {"notebook", "phone", "coffee", "snack", "water", "park"};
        for (int i = 0; i < 6; i++)
            if (!strcmp(force, acts[i])) {
                if (ACT_NOTEBOOK + i == ACT_WATER) D.cx = PACT_WATER_X;
                if (ACT_NOTEBOOK + i == ACT_PARK) D.cx = PACT_PARK_START_X;
                start((act_t)(ACT_NOTEBOOK + i));
                return;
            }
    }
#endif
    if (D.queued >= 0) {
        int s = D.queued;
        D.queued = -1;
        if (s >= ACT_SHOW_WALKJUMP) begin_show(s);
        else start((act_t)s);
        return;
    }
    if (D.want_water) {
        D.want_water = false;
        go_do(ACT_WATER);
        return;
    }
    if (D.act != ACT_IDLE) { /* breathe between acts */
        start(ACT_IDLE);
        return;
    }
    if (D.since_show > D.show_every) {
        /* about evenly, the flag only now and then */
        static const float k_show_w[4] = {1.0f, 1.0f, 0.2f, 1.0f}; /* walkjump, gym, flag, confetti */
        float left = 0;
        for (int i = 0; i < 4; i++) left += ACT_SHOW_WALKJUMP + i == D.last_show ? 0 : k_show_w[i];
        left = rng_float(0, left);
        int s = -1;
        for (int i = 0; i < 4; i++) {
            if (ACT_SHOW_WALKJUMP + i == D.last_show) continue;
            s = ACT_SHOW_WALKJUMP + i;
            if ((left -= k_show_w[i]) <= 0) break;
        }
        float x = s == ACT_SHOW_WALKJUMP ? BODY_CX : CENTER_X;
        if (fabsf(D.cx - x) > 2) walk_to(x, s);
        else begin_show(s);
        return;
    }
    static const struct { act_t a; float w; } k_weights[] = {
        {ACT_LOOK, 1.6f}, {ACT_WALK, 1.8f}, {ACT_THINK, 1}, {ACT_TAP, 0.8f}, {ACT_DANCE, 0.8f}, {ACT_SLEEP, 0.8f},
        {ACT_NOTEBOOK, 2.0f}, {ACT_PHONE, 1.5f}, {ACT_COFFEE, 2.0f}, {ACT_SNACK, 2.0f}, {ACT_WATER, 0}, {ACT_PARK, 4.0f},
    };
/* walks mostly by day; watering gets likelier as the flower wilts */
#define WEIGHT(i) (k_weights[i].a == ACT_PARK && D.daylight < 0.5f ? 2.0f : k_weights[i].a == ACT_WATER ? 0.2f + 3.5f * D.thirst : k_weights[i].w)
    float total = 0;
    for (unsigned i = 0; i < sizeof(k_weights) / sizeof(k_weights[0]); i++)
        if (allowed(k_weights[i].a)) total += WEIGHT(i);
    float r = rng_float(0, total);
    for (unsigned i = 0; i < sizeof(k_weights) / sizeof(k_weights[0]); i++) {
        act_t a = k_weights[i].a;
        if (!allowed(a)) continue;
        if ((r -= WEIGHT(i)) > 0) continue;
        if (a == ACT_WALK) {
            float x;
            do x = rng_float(70, 250);
            while (fabsf(x - D.cx) < 50);
            walk_to(x, -1);
        } else if (a >= ACT_NOTEBOOK && a <= ACT_PARK) {
            go_do(a); /* props need room on both sides; the walk starts next to the door */
        } else {
            if (a == ACT_SLEEP) D.since_sleep = 0;
            start(a);
        }
        return;
    }
    start(ACT_LOOK);
}

/* A shake interrupts most acts; it wakes the mascot up when sleeping. On the walk it starts a ball game or a storm. */
static void shaken(void)
{
    if (D.act == ACT_PARK) { /* out in the park: a ball game by day, a thunderstorm at night */
        float dur = pact_park_shake(D.t, D.daylight);
#ifdef HOST_DEBUG
        printf("  t=%7.2f  park-shake     at %.2f -> dur %.2f\n", s_clock, D.t, dur);
#endif
        if (dur > 0) D.dur = dur;
        return;
    }
    if (D.act == ACT_SHOW_WALKJUMP || D.act == ACT_DIZZY) return;
    if (D.act == ACT_WALK) {
        if (D.queued == ACT_WATER) D.want_water = true;
        stop_walk();
    }
    float in_bed_from = SL_YAWN + SL_BED_IN + SL_HOP_IN, in_bed_to = D.dur - SL_AFTER_LOOP + SL_WAKE_EYES + SL_BLANKET_OFF;
    D.startled = D.act == ACT_SLEEP && D.t > in_bed_from && D.t < in_bed_to;
    D.dizzy_end = -1;
    toss_start(&D.toss, D.cx);
    start(ACT_DIZZY);
}

/* A tap on the wilting flower: go and water it now from a neutral pose, otherwise right after the current act. */
static void flower_tapped(void)
{
    if (D.thirst < FLOWER_WILTING || D.act == ACT_WATER || D.queued == ACT_WATER) return;
    if (D.act == ACT_WALK) stop_walk();
    if (D.act == ACT_IDLE || D.act == ACT_LOOK || D.act == ACT_WALK) go_do(ACT_WATER);
    else D.want_water = true;
}

static void enter(uint32_t now)
{
    (void)now;
#ifdef HOST_DEBUG
    forced_done = 0;
#endif
    D.cx = CENTER_X;
    D.act = D.last_act = D.last_prop = ACT_IDLE;
    D.queued = -1;
    D.last_show = -1;
    D.since_show = 0;
    D.show_every = rng_float(6, 10); /* first showpiece comes early */
    D.since_sleep = 90; /* sleep may come early */
    D.blink_in = 2;
    D.blink_t = D.glance_t = -1;
    D.glance_in = 3;
    D.happy_t = -1;
    D.clock = 0;
    D.daylight = D.day_target = 0; /* starts at night: the moon is in the window */
#ifdef HOST_DEBUG
    if (getenv("SHOW_DAY")) D.daylight = D.day_target = 1;
#endif
    D.day_in = rng_float(60, 120);
    D.since_park = 40;
    D.thirst = 0.55f;
    D.want_water = false;
    start(ACT_IDLE);
}

static void update(const input_t *in, uint32_t now, float dt)
{
    (void)now;
#ifdef HOST_DEBUG
    s_clock += dt;
#endif
    D.t += dt;
    D.clock += dt;
    D.since_show += dt;
    D.since_park += dt;
    if (D.act != ACT_PARK && (D.day_in -= dt) <= 0) { /* sunrise / sunset in the window */
        D.day_target = D.day_target > 0.5f ? 0 : 1;
        D.day_in = D.day_target > 0.5f ? rng_float(90, 180) : rng_float(90, 150);
    }
    D.daylight += clampf(D.day_target - D.daylight, -dt / 3, dt / 3);
    if (D.act == ACT_WATER && D.t > PACT_WATER_POUR_T0 && D.t < PACT_WATER_POUR_T1)
        D.thirst = fmaxf(0, D.thirst - dt / 2.5f); /* perks up while watered */
    else
        D.thirst = fminf(1, D.thirst + dt / 300); /* wilts over ~5 minutes */
    room_set_plant(D.thirst);
    D.since_sleep += dt;
    if (D.blink_t >= 0 && (D.blink_t += dt) > 0.14f) D.blink_t = -1;
    if ((D.blink_in -= dt) <= 0) {
        D.blink_t = 0;
        D.blink_in = rng_float(2.5f, 5.5f);
    }
    if (D.glance_t >= 0 && (D.glance_t += dt) > 1.2f) D.glance_t = -1;
    if ((D.glance_in -= dt) <= 0) {
        D.glance_t = 0;
        D.glance_dx = rng_float(-3, 3);
        D.glance_dy = rng_float(-2, 1);
        D.glance_in = rng_float(3, 6);
    }
    if (D.happy_t >= 0 && (D.happy_t += dt) > 0.7f) D.happy_t = -1;

    D.jx = in->jx;
    D.jy = in->jy;
    D.level = in->shake_level;
    if (in->shake) shaken();
    if (D.act == ACT_DIZZY) {
        float t0 = D.startled ? DZ_STARTLE : 0;
        if (D.dizzy_end < 0 && D.t > t0) toss_update(&D.toss, in, dt, BODY_CX, GFX_W - BODY_CX, MASCOT_H - GROUND);
        if (D.dizzy_end < 0 && D.t > t0 + 0.4f && toss_settled(&D.toss) && (in->shake_level < 0.3f || D.t > t0 + 8)) {
            D.cx = D.toss.x; /* gets dizzy where it landed */
            D.dizzy_end = D.t;
            D.dur = D.t + DZ_SPIN + DZ_RECOVER;
        } else if (D.dizzy_end >= 0 && in->shake_level > 0.6f) { /* shaken again while dizzy */
            D.dizzy_end = -1;
            D.dur = 60;
        }
    }

    if (in->tap) {
        if (fabsf(in->start_x - (PLANT_X + 4)) < 26 && in->start_y > 56 && in->start_y < POT_TOP + 16) flower_tapped();
        if (D.act == ACT_SLEEP && D.t > SL_BEFORE_LOOP && D.t < D.dur - SL_AFTER_LOOP) D.t = D.dur - SL_AFTER_LOOP; /* tap wakes */
        else if (D.act <= ACT_DANCE) {
            D.glance_t = 0;
            D.glance_dx = clampf((in->start_x - D.cx) / 25, -4, 4);
            D.glance_dy = clampf((in->start_y - (POSE_OY + 16)) / 25, -4, 4);
        }
    }

    if (D.t < D.dur) return;
    if (D.act == ACT_WALK) D.cx = D.walk_to;
    if (D.act == ACT_SHOW_WALKJUMP) D.cx += WALKJUMP_TRAVEL;
    if (D.act == ACT_SHOW_FLAG) D.happy_t = 0;
    if (D.act == ACT_PARK) {
        D.cx = PACT_PARK_START_X;
        D.since_park = 0;
    }
    pick_next();
}

/* ---------- drawing ---------- */

/* The original stride: legs 1+3 and 2+4 alternate, 0.1 s down (power2.out), 0.1 s up (power2.in). */
static float stride_lift(float t, int pair)
{
    float tau = fmodf(t + pair * 0.1f, 0.2f);
    return tau < 0.1f ? ease_out2(tau / 0.1f) : 1 - ease_in2((tau - 0.1f) / 0.1f);
}

static void ambient_eyes(mascot_pose_t *p)
{
    if (D.blink_t >= 0) p->eye_open = fabsf(1 - D.blink_t / 0.07f);
    if (D.glance_t >= 0) {
        float k = D.glance_t < 0.4f ? ease_out2(D.glance_t / 0.4f) : D.glance_t > 0.9f ? 1 - ease_out2((D.glance_t - 0.9f) / 0.3f) : 1;
        p->eye_dx = D.glance_dx * k;
        p->eye_dy = D.glance_dy * k;
    }
    if (D.happy_t >= 0) p->happy = true;
}

static float blink_at(float t) { return t < 0.14f ? fabsf(1 - t / 0.07f) : 1; }

/* Big pixel "Z": 5x5 cells, readable on the small screen. */
static void draw_big_z(uint16_t *fb, float x, float y, float cell, uint16_t c)
{
    static const char *rows[] = {"#####", "...#.", "..#..", ".#...", "#####"};
    for (int r = 0; r < 5; r++)
        for (int k = 0; k < 5; k++)
            if (rows[r][k] == '#') gfx_fill_box(fb, x + k * cell, y + r * cell, x + (k + 1) * cell, y + (r + 1) * cell, c, 0);
}

static void draw_zs(uint16_t *fb, float t, float head_y)
{
    const uint16_t c = gfx_color(0xCFE0FF);
    const float side = D.cx > 200 ? -1 : 1;
    for (int k = 0; k < 3; k++) {
        float age = fmodf(t - k * 1.1f + 3.3f, 3.3f);
        if (age > 2.6f) continue;
        float cell = 2 + age * 0.8f, size = cell * 5; /* grows as it rises: z, Z, Z */
        float x = D.cx + side * (40 + age * 10) - (side < 0 ? size : 0);
        draw_big_z(fb, x, head_y - 8 - age * 18 - size, cell, c);
    }
}

static void draw_bed_back(uint16_t *fb, float ox, float oy)
{
    gfx_fill_box(fb, ox - 10, oy + 10, ox + 117, oy + 64, gfx_color(0x5A3E2E), 0); /* headboard */
    gfx_fill_box(fb, ox - 10, oy + 10, ox + 117, oy + 14, gfx_color(0x7A5642), 0);
    gfx_fill_box(fb, ox - 2, oy + 17, ox + 109, oy + 44, gfx_color(0xEDEDED), 0); /* pillow */
    gfx_fill_box(fb, ox - 2, oy + 42, ox + 109, oy + 44, gfx_color(0xBDBDBD), 0);
    gfx_fill_box(fb, ox - 10, oy + 64, ox + 117, oy + 80, gfx_color(0xD9D9E6), 0); /* mattress */
    gfx_fill_box(fb, ox - 10, oy + 80, ox - 2, oy + 86, gfx_color(0x5A3E2E), 0); /* legs */
    gfx_fill_box(fb, ox + 109, oy + 80, ox + 117, oy + 86, gfx_color(0x5A3E2E), 0);
}

static void draw_blanket(uint16_t *fb, float ox, float oy, float top)
{
    if (top >= 80) return;
    gfx_fill_box(fb, ox - 6, oy + top, ox + 113, oy + 80, gfx_color(0x3D74B8), 0);
    gfx_fill_box(fb, ox - 6, oy + top, ox + 113, oy + top + 4, gfx_color(0x7CA4D0), 0); /* folded edge */
    for (float x = 2; x < 110; x += 14) /* quilt pattern */
        for (float y = top + 9; y < 78; y += 9) gfx_fill_box(fb, ox + x, oy + y, ox + x + 3, oy + y + 3, gfx_color(0x2F5E99), 0);
}

/* Lying in bed, seen from the foot of the bed: squashed body on the pillow, eyes `eye_open`. */
static void draw_lying(uint16_t *fb, float ox, float oy, float breath, float eye_open)
{
    const uint16_t body = gfx_color(MASCOT_RGB), eye = gfx_color(0x000000);
    float top = 27 + breath;
    gfx_fill_box(fb, ox + 11, oy + top, ox + 96, oy + 74, body, 0);
    float h = 11 * clampf(eye_open, 0.15f, 1), ey = top + 8 + (11 - h) / 2;
    gfx_fill_box(fb, ox + 21, oy + ey, ox + 32, oy + ey + h, eye, 0);
    gfx_fill_box(fb, ox + 75, oy + ey, ox + 86, oy + ey + h, eye, 0);
}

static void draw_sleep(uint16_t *fb)
{
    const float t = D.t, ox = D.cx - BODY_CX, oy = POSE_OY;
    const float p_bed = SL_YAWN, p_hop = p_bed + SL_BED_IN, p_blanket = p_hop + SL_HOP_IN, p_eyes = p_blanket + SL_BLANKET;
    const float p_loop = p_eyes + SL_EYES, p_wake = p_loop + D.sleep_loop, p_off = p_wake + SL_WAKE_EYES;
    const float p_out = p_off + SL_BLANKET_OFF, p_away = p_out + SL_HOP_OUT, p_stretch = p_away + SL_BED_OUT;
    mascot_pose_t p = MASCOT_POSE_REST;

    if (t < p_bed) { /* sleepy yawn: arms up slowly, eyes droop */
        float up = t < 0.6f ? ease_inout2(t / 0.6f) : t > 0.9f ? 1 - ease_inout2((t - 0.9f) / 0.5f) : 1;
        p.hands_dy = -12 * up;
        p.body_dy = -2 * up;
        p.eye_open = 1 - 0.6f * clampf(t / 1.0f, 0, 1);
        mascot_pose_draw(fb, ox, oy, 1, &p);
        return;
    }
    if (t < p_blanket || t >= p_out) { /* standing next to the bed (or hopping in/out) */
        float bed_dy = t < p_hop ? 90 * (1 - ease_out2((t - p_bed) / SL_BED_IN))
                     : t >= p_away ? 90 * ease_in2((t - p_away) / SL_BED_OUT) : 0;
        if (t < p_stretch) draw_bed_back(fb, ox, oy + bed_dy);
        if (t >= p_stretch) { /* big stretch, wide awake */
            float k = t - p_stretch, up = k < 0.5f ? ease_out2(k / 0.5f) : k > 1.2f ? 1 - ease_inout2((k - 1.2f) / 0.6f) : 1;
            p.hands_dy = -26 * up;
            p.body_dy = -6 * up;
            p.happy = up > 0.3f;
            mascot_pose_draw(fb, ox, oy, 1, &p);
            return;
        }
        float hop = 0;
        if (t >= p_hop && t < p_blanket) hop = 22 * sinf((t - p_hop) / SL_HOP_IN * (float)M_PI);
        if (t >= p_out && t < p_away) hop = 22 * sinf((t - p_out) / SL_HOP_OUT * (float)M_PI);
        p.eye_open = t < p_blanket ? 0.4f : 1;
        p.leg_lift[0] = p.leg_lift[1] = p.leg_lift[2] = p.leg_lift[3] = hop > 2 ? 0.4f : 0;
        mascot_pose_draw(fb, ox, oy - hop, 1, &p);
        return;
    }

    /* In bed */
    float breath = 0, eye = 0.15f, blanket = 52;
    draw_bed_back(fb, ox, oy);
    if (t < p_eyes) {
        blanket = 80 - 28 * ease_out2((t - p_blanket) / SL_BLANKET);
        eye = 0.4f;
    } else if (t < p_loop) {
        eye = 0.4f - 0.25f * ease_inout2((t - p_eyes) / SL_EYES); /* eyes close slowly */
    } else if (t < p_wake) {
        breath = sinf((t - p_loop) * 2 * (float)M_PI / 3) > 0 ? 1 : 0;
        blanket = 52 + breath;
    } else if (t < p_off) { /* eyes open slowly, a sleepy blink, then wide open */
        float k = t - p_wake;
        eye = k < 0.6f ? 0.15f + 0.35f * ease_inout2(k / 0.6f) : k < 0.9f ? 0.5f : k < 1.1f ? 0.15f : 1;
    } else {
        blanket = 52 + 28 * ease_in2((t - p_off) / SL_BLANKET_OFF);
        eye = 1;
    }
    draw_lying(fb, ox, oy, breath, eye);
    draw_blanket(fb, ox, oy, blanket);
    if (t >= p_loop && t < p_wake) draw_zs(fb, t - p_loop, oy + 27);
}

static void draw_dizzy(uint16_t *fb)
{
    const float t = D.t, t0 = D.startled ? DZ_STARTLE : 0;
    mascot_pose_t p = MASCOT_POSE_REST;
    float ox = D.cx - BODY_CX, oy = POSE_OY;
    if (t < t0) { /* startled awake: the bed drops away, a jump and a "!" */
        draw_bed_back(fb, ox, oy + 90 * ease_in2(t / t0));
        float hop = 26 * sinf(t / t0 * (float)M_PI);
        mascot_pose_draw(fb, ox, oy - hop, 1, &p);
        font_draw(fb, (int)(D.cx - 3), (int)(oy - hop - 22), 4, gfx_color(0xFFFFFF), "!");
        return;
    }
    const float de = D.dizzy_end;
    if (de < 0 || t < de) { /* thrown around the room by the shaking */
        float lv = clampf(D.level, 0, 2);
        ox = D.toss.x - BODY_CX + 2 * lv * sinf(D.clock * 45);
        oy = POSE_OY + D.toss.y;
        if (D.toss.y < -2) p.leg_lift[0] = p.leg_lift[1] = p.leg_lift[2] = p.leg_lift[3] = 0.5f;
        p.body_dy = 2 * lv * sinf(D.clock * 50);
        p.hand_dy[0] = -10 * lv * sinf(D.clock * 30);
        p.hand_dy[1] = 10 * lv * sinf(D.clock * 30);
        p.eye_open = 0.45f;
        mascot_pose_draw(fb, ox, oy, 1, &p);
        return;
    }
    float k = t - de;
    float fade = k < 0.4f ? ease_out2(k / 0.4f) : k > DZ_SPIN ? 1 - ease_in2((k - DZ_SPIN) / DZ_RECOVER) : 1;
    if (k < DZ_SPIN) { /* spiral eyes, swaying, sparks */
        p.spiral = true;
        p.spiral_t = k;
        ox += 5 * sinf(k * 2 * (float)M_PI / 1.3f);
        p.body_dy = 1.5f * sinf(k * 2 * (float)M_PI / 0.65f);
        p.hands_dy = 4;
    } else { /* shakes it off */
        float r = k - DZ_SPIN;
        if (r < 0.5f) p.eye_dx = sinf(r * 60) > 0 ? 3 : -3;
        else p.eye_open = blink_at(r - 0.5f);
    }
    mascot_pose_draw(fb, ox, oy, 1, &p);
    draw_spark_orbit(fb, ox + BODY_CX, oy + p.body_dy, D.clock, fade);
}

static void draw(uint16_t *fb, uint32_t now)
{
    (void)now;
    const float t = D.t;
    if (D.act != ACT_PARK) room_draw(fb, D.daylight, 0, D.clock);
    switch (D.act) {
    case ACT_LOOK:
        if (t < LOOK_ORIG_END) {
            manim_draw(fb, MANIM_WALKING, t, D.cx - BODY_CX, POSE_OY, 1.0f);
            return;
        }
        break;
    case ACT_NOTEBOOK: case ACT_PHONE: case ACT_COFFEE: case ACT_SNACK: case ACT_WATER: case ACT_PARK:
        pact_draw(fb, (pact_t)(D.act - ACT_NOTEBOOK), t, D.cx, GROUND, D.daylight);
        return;
    case ACT_SHOW_WALKJUMP:
        manim_draw(fb, MANIM_WALKING, t, D.cx - BODY_CX, POSE_OY, 1.0f);
        return;
    case ACT_SHOW_GYM:
        manim_draw(fb, MANIM_GYM, t, D.cx - 78.5f, GROUND - 128, 1.0f);
        return;
    case ACT_SHOW_CONFETTI:
        manim_draw(fb, MANIM_CONFETTI, t, D.cx - 64.5f, GROUND - 113, 1.0f);
        return;
    case ACT_SHOW_FLAG: {
        float lower_at = D.dur - FLAG_LOWER_S; /* then replay the raise frames backwards */
        float ft = t < lower_at ? t : clampf(0.14f - (t - lower_at), 0, 0.14f);
        manim_draw(fb, MANIM_FLAG, ft, D.cx - 63.5f, GROUND - 146, 1.0f);
        return;
    }
    default: break;
    }

    mascot_pose_t p = MASCOT_POSE_REST;
    float ox = D.cx - BODY_CX, oy = POSE_OY;
    switch (D.act) {
    case ACT_IDLE:
        ambient_eyes(&p);
        p.body_dy = sinf(t * 2 * (float)M_PI / 2.4f) > 0.7f ? -1 : 0; /* one-pixel breath */
        break;
    case ACT_LOOK: /* eyes come back up after the original "look down" */
        p.eye_dy = 23 * (1 - ease_out2((t - LOOK_ORIG_END) / 0.25f));
        break;
    case ACT_WALK: {
        float dir = D.walk_to > D.walk_from ? 1 : -1, walk_t = t - HOP_S, walk_dur = D.dur - HOP_S - 0.2f;
        if (t < 0.18f) oy -= 18 * ease_out2(t / 0.18f); /* the original hop before walking */
        else if (t < HOP_S) { float k = (t - 0.18f) / 0.15f; oy -= 18 * (1 - k * k * k * k); }
        else if (walk_t < walk_dur) {
            ox = D.walk_from + dir * WALK_SPEED * walk_t - BODY_CX;
            p.leg_lift[0] = p.leg_lift[2] = stride_lift(walk_t, 0);
            p.leg_lift[1] = p.leg_lift[3] = stride_lift(walk_t, 1);
        } else ox = D.walk_to - BODY_CX;
        float look = t < HOP_S ? 0 : walk_t < walk_dur ? ease_out2(walk_t / 0.2f) : 1 - ease_out2((walk_t - walk_dur) / 0.2f);
        p.eye_dx = 4 * dir * look;
        break;
    }
    case ACT_THINK: {
        float look = t < 0.4f ? ease_out2(t / 0.4f) : t > D.dur - 0.7f ? 1 - ease_out2((t - (D.dur - 0.7f)) / 0.3f) : 1;
        p.eye_dx = 3 * look;
        p.eye_dy = -4 * look;
        if (t < D.dur - 0.7f) {
            int dots = (int)(t / 0.45f) % 4;
            float side = D.cx > 200 ? -1 : 1; /* keep the bubble on screen */
            for (int i = 0; i < dots; i++) gfx_fill_rect(fb, (int)(D.cx + side * (22 + i * 11) - (side < 0 ? 6 : 0)), (int)(oy - 16), 6, 6, gfx_color(0xFFFFFF));
        } else { /* eureka: happy hop */
            float k = (t - (D.dur - 0.7f)) / 0.7f;
            p.happy = true;
            oy -= 10 * sinf(k * (float)M_PI);
            font_draw(fb, (int)(D.cx + (D.cx > 200 ? -39 : 30)), (int)(oy - 26), 3, gfx_color(0xFFFFFF), "!");
        }
        break;
    }
    case ACT_TAP: { /* bored: tap the right foot, half-closed eyes looking away */
        float tau = fmodf(t, 0.3f);
        p.leg_lift[3] = tau < 0.1f ? ease_out2(tau / 0.1f) * 0.7f : tau < 0.2f ? 0.7f * (1 - ease_in2((tau - 0.1f) / 0.1f)) : 0;
        p.eye_open = 0.6f;
        p.eye_dx = -2;
        break;
    }
    case ACT_DANCE: {
        float w = sinf(t * 2 * (float)M_PI / 0.8f), fade = clampf(fminf(t, D.dur - t) / 0.3f, 0, 1);
        ox += 3 * w * fade;
        p.hand_dy[0] = -8 * fmaxf(0, w) * fade;
        p.hand_dy[1] = -8 * fmaxf(0, -w) * fade;
        p.body_dy = -1.5f * fabsf(sinf(t * 2 * (float)M_PI / 0.4f)) * fade;
        p.leg_lift[0] = p.leg_lift[2] = 0.5f * fmaxf(0, w) * fade;
        p.leg_lift[1] = p.leg_lift[3] = 0.5f * fmaxf(0, -w) * fade;
        p.happy = true;
        break;
    }
    case ACT_SLEEP:
        draw_sleep(fb);
        return;
    case ACT_DIZZY:
        draw_dizzy(fb);
        return;
    default: break;
    }
    mascot_pose_draw(fb, ox, oy, 1.0f, &p);
}

const scene_t scene_idle = {enter, update, draw, 0};
