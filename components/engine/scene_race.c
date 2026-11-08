/* RACE: first-person pseudo-3D road. A small steering wheel held by Claude's paws follows the board's
 * tilt (spring physics); hold a finger to brake. Three levels with their own tracks and obstacles; every race
 * also gets a random season (summer lakes, an autumn serpentine, snowy winter mountains) and weather. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef HOST_DEBUG
#include <stdlib.h>
#endif

#include "font.h"
#include "gfx.h"
#include "mascot_pose.h"
#include "scenes.h"

static float s_vh = 142;   /* road view height: 142 with the dashboard, full screen in the win scene */
#define VIEW_H s_vh
#define Y_H 46.0f          /* horizon */
#define HALF_W 150.0f      /* road half-width in px at the bottom row */
#define STRIPE 2.4f        /* track units per colour band */
#define MAX_OBS 96
#define PLAYER_W 0.28f     /* car half-width, road units */
#define LIVES 3
#define WHEEL_CX 160.0f
#define WHEEL_CY 172.0f
#define WHEEL_R 34.0f
#define DRAW_SCALE 2.2f /* obstacles are drawn larger than their hit box to read on the small screen */
#define CAR_HW 0.18f    /* hit half-width of every car: rivals, traffic, wrecks */
#define CAR_DRAW_W (2 * CAR_HW * DRAW_SCALE)

typedef enum { O_CONE, O_BARREL, O_ROCK, O_CAR, O_OIL, O_PUDDLE, O_BOOST, O_DUCK, O_TREE, O_DEER, O_WRECK, O_SIGN, O_HAY, O_WORKS, O_BARRIER } otype_t;
typedef struct {
    otype_t type;
    float s, x, v, vx; /* track position, lateral (road units), own speed, lateral speed */
    bool hit;
    uint32_t color;
    float timer; /* deer: >0 frozen on the road, <0 done; rolling rock: bounce phase */
} obs_t;

typedef struct {
    const char *name;
    float length, speed, curve_max, gap_lo, gap_hi;
    otype_t kinds[8];
    int nkinds;
} level_t;

static const level_t k_levels[3] = {
    {"EASY", 420, 10, 0.012f, 36, 56, {O_CONE, O_CONE, O_HAY, O_HAY, O_BOOST, O_TREE}, 6},
    {"MEDIUM", 560, 13, 0.019f, 26, 42, {O_CONE, O_BARREL, O_OIL, O_PUDDLE, O_BOOST, O_DUCK, O_WORKS}, 7},
    {"HARD", 760, 16, 0.028f, 20, 32, {O_CAR, O_ROCK, O_OIL, O_BARREL, O_CAR, O_DUCK}, 6},
};

/* The landscape and the weather: random every race, all in daylight (the level sets only the track and obstacles). */
typedef enum { SUMMER, AUTUMN, WINTER } season_t;
typedef enum { W_CLEAR, W_RAIN, W_FOG, W_SNOW } weather_t;
typedef struct {
    const char *name;
    uint32_t sky_top, sky_low, far, grass_a, grass_b, road_a, road_b, map_bg;
} look_t;
static const look_t k_looks[3] = {
    {"SUMMER", 0x4FA3E0, 0xB8E2F8, 0x3E6A58, 0x4CAF50, 0x43A047, 0x6A6A74, 0x64646E, 0x0E1A10},
    {"AUTUMN", 0x5E96CC, 0xF2D9A6, 0x8A5A30, 0xA8963C, 0x9A8834, 0x66646C, 0x605E66, 0x1E140C},
    {"WINTER", 0x7FAEDC, 0xE2ECF6, 0x6A7A94, 0xEEF2F8, 0xDDE5EF, 0x787C88, 0x727682, 0x141A24},
};
static const char *const k_weather_name[4] = {"", "RAIN", "FOG", "SNOW"};
#define FOG_RGB 0xC4C8CE
static season_t s_season = AUTUMN; /* the first race is never autumn twice: see pick_look */
static weather_t s_weather;
static int s_fog; /* fog of the thing being drawn, 0..256 (by depth) */
#define MAX_LAKES 6
static float s_lake[MAX_LAKES][3]; /* summer: s0, s1, side */
static int s_nlakes;
/* Scenery: trees further from the road, for looks only (no collisions), sorted by track position. */
#define MAX_SCN 240
typedef struct {
    float s, x;
    uint32_t color;
} scn_t;
static scn_t s_scn[MAX_SCN];
static int s_nscn;

typedef enum { SELECT, COUNTDOWN, RACE, CRASHED_OUT, VICTORY_LAP, FINISHED } state_t;

static state_t s_state;
static int s_level, s_lives;
static float s_s, s_v, s_x, s_vx, s_t, s_count, s_invuln, s_shake, s_boost, s_skid, s_bg;
static float s_wheel, s_wheel_v;
static float s_seg_len[64], s_seg_curve[64];
static int s_nseg;
/* track map shown during the countdown: the road's own curves integrated into a polyline */
#define MAP_PTS 160
#define MAP_K 1.5f /* map heading per unit of road curve */
static float s_map_x[MAP_PTS], s_map_y[MAP_PTS];
static int s_turns;
static obs_t s_obs[MAX_OBS];
static int s_nobs;
static int32_t s_best[3];
static uint32_t s_end_at;
static float s_row_cx[GFX_H], s_row_sc[GFX_H];

/* rival racers */
#define RIVALS 3
typedef struct {
    float s, x, lane, speed, phase, wreck_t;      /* wreck_t > 0: crashed and stopped */
    float corner, weave, weave_f, aggr, slip_rate; /* personality, rolled for every race */
    float surge, surge_to, surge_t, change_t;     /* pace wander, time to the next own lane change */
    float mistake_t, spin;                        /* a mistake in progress: spin 0 = braking, +-1 = sliding out that way */
    uint32_t color;
} rival_t;

/* events on levels 2-3: a deer on the road, a crash between a rival and another car */
#define EVENT_DEER_RATE (s_level == 2 ? 0.035f : 0.02f)  /* per second */
#define EVENT_CRASH_RATE (s_level == 2 ? 0.03f : 0.018f)
#define EVENT_ROCK_RATE 0.035f /* level 3 only: rocks roll down onto the road */
#define ROCK_K 2.5f            /* rolling rocks lose speed at this rate (1/s) */

/* windshield cracks, one per hit (more when out of lives) */
#define MAX_CRACKS 6
static float s_crack[MAX_CRACKS][3]; /* x, y, seed */
static int s_ncracks;
static float s_win_t; /* victory lap clock */
#define WIN_DRIVE_S 2.2f  /* our car drives off into the distance in this time */
#define WIN_BANNER_S 0.5f /* the result banner and confetti come up this soon, while it is still driving */
static float s_clock; /* always running: smoke keeps moving on the result screens */
static rival_t s_riv[RIVALS];
static int s_place; /* finishing place, 1-based */

/* finish confetti */
#define CONFETTI 60
static float s_cf[CONFETTI][3]; /* x, y, phase */
static const uint32_t k_cf_colors[5] = {0x7CA4D0, 0xDD8361, 0xC87392, 0xF7D23C, 0x5DBB3A};

static int position(void)
{
    int p = 1;
    for (int i = 0; i < RIVALS; i++)
        if (s_riv[i].s > s_s) p++;
    return p;
}

static float curve_at(float s)
{
    float acc = 0;
    for (int i = 0; i < s_nseg; i++) {
        if (s < acc + s_seg_len[i]) {
            float into = s - acc, ease = 12; /* blend into and out of every curve */
            float k = fminf(1, fminf(into, s_seg_len[i] - into) / ease);
            return s_seg_curve[i] * clampf(k, 0, 1);
        }
        acc += s_seg_len[i];
    }
    return 0;
}

/* curve_at for increasing s along one frame: walks the segments from a cursor instead of from the start */
typedef struct {
    int i;
    float acc;
} curve_cur_t;
static float curve_next(curve_cur_t *c, float s)
{
    while (c->i < s_nseg && s >= c->acc + s_seg_len[c->i]) c->acc += s_seg_len[c->i++];
    if (c->i >= s_nseg) return 0;
    float into = s - c->acc, k = fminf(1, fminf(into, s_seg_len[c->i] - into) / 12);
    return s_seg_curve[c->i] * clampf(k, 0, 1);
}

static void add_seg(float len, float curve)
{
    if (s_nseg >= 64) return;
    s_seg_len[s_nseg] = len;
    s_seg_curve[s_nseg++] = curve;
}

/* heading change of one curve segment on the map, matching curve_at's 12-unit ease in and out */
static float seg_turn(float len, float curve) { return curve * MAP_K * (len >= 24 ? len - 12 : len * len / 48); }

static uint32_t mixc(uint32_t a, uint32_t b, int k) /* a -> b by k/256, 0xRRGGBB */
{
    uint32_t c = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        int x = (int)((a >> sh) & 255), y = (int)((b >> sh) & 255);
        c |= (uint32_t)(x + (((y - x) * k) >> 8)) << sh;
    }
    return c;
}

/* A colour at the current depth: faded into the fog. */
static uint16_t fcol(uint32_t c) { return gfx_color(s_fog ? mixc(c, FOG_RGB, s_fog) : c); }
static int fog_at(float z) { return s_weather == W_FOG ? (int)clampf((z - 3) * 11, 0, 225) : 0; }

static float h01(int i, int salt)
{
    uint32_t h = (uint32_t)i * 2654435761u ^ (uint32_t)salt * 40503u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (h & 0xFFFF) / 65535.0f;
}

/* summer lake beside the road at track position s: its side (-1 left, 1 right) or 0 */
static int lake_at(float s, float margin)
{
    for (int i = 0; i < s_nlakes; i++)
        if (s > s_lake[i][0] - margin && s < s_lake[i][1] + margin) return (int)s_lake[i][2];
    return 0;
}

/* A different season from the last race, and weather that fits it. */
static void pick_look(void)
{
    season_t last = s_season;
    do s_season = (season_t)(rng_next() % 3);
    while (s_season == last);
    float r = rng_float(0, 1);
    s_weather = s_season == SUMMER ? (r < 0.6f ? W_CLEAR : W_RAIN)
              : s_season == AUTUMN ? (r < 0.5f ? W_CLEAR : r < 0.75f ? W_FOG : W_RAIN)
                                   : (r < 0.4f ? W_CLEAR : W_SNOW);
#ifdef HOST_DEBUG
    const char *se = getenv("RACE_SEASON"), *we = getenv("RACE_WEATHER"); /* tests only */
    if (se) s_season = se[0] == 's' ? SUMMER : se[0] == 'a' ? AUTUMN : WINTER;
    if (we) s_weather = we[0] == 'c' ? W_CLEAR : we[0] == 'r' ? W_RAIN : we[0] == 'f' ? W_FOG : W_SNOW;
#endif
}

/* A fresh point-to-point track every race: straights, sweepers, corners, S-bends and (levels 2-3) tight corners.
 * The map heading is kept within ~70 degrees of the start so the track runs forward instead of spiralling. */
static void build_track(int lv, const level_t *L)
{
    static const float k_mix[3][4] = {{0.35f, 0.5f, 0.15f, 0}, {0.3f, 0.35f, 0.2f, 0.15f}, {0.2f, 0.3f, 0.3f, 0.2f}}; /* sweeper, corner, S, tight */
    static const float k_serpentine[3][4] = {{0.15f, 0.3f, 0.55f, 0}, {0.1f, 0.25f, 0.45f, 0.2f}, {0.05f, 0.2f, 0.45f, 0.3f}};
    const float(*mix)[4] = s_season == AUTUMN ? k_serpentine : k_mix; /* autumn: a winding mountain road */
    s_nseg = s_turns = 0;
    add_seg(50, 0); /* straight start */
    float total = 50, heading = 0, cm = L->curve_max;
    while (s_nseg < 58) {
        float pick = rng_float(0, 1), dir = fabsf(heading) > 1.2f ? -copysignf(1, heading) : rng_next() & 1 ? 1 : -1;
        int kind = 0;
        for (float acc = mix[lv][0]; kind < 3 && pick >= acc;) acc += mix[lv][++kind];
        float len, c, len2 = 0, straight = rng_float(lv == 2 ? 10 : 15, lv == 0 ? 45 : 40) * (s_season == AUTUMN ? 0.6f : 1);
        switch (kind) {
        case 0: len = rng_float(60, 100), c = rng_float(0.35f, 0.55f) * cm; break;
        case 1: len = rng_float(35, 70), c = rng_float(0.65f, 1) * cm; break;
        case 2: len = len2 = rng_float(30, 45), c = rng_float(0.6f, 0.9f) * cm; break;
        default: len = rng_float(30, 40), c = 1.2f * cm; break;
        }
        if (total + len + len2 + straight > L->length - 40) break; /* keep the run-in to the finish straight */
        add_seg(len, dir * c);
        heading += seg_turn(len, dir * c);
        s_turns++;
        if (len2 > 0) { /* S-bend: straight into the opposite curve */
            add_seg(len2, -dir * c);
            heading += seg_turn(len2, -dir * c);
            s_turns++;
        }
        add_seg(straight, 0);
        total += len + len2 + straight;
    }
    add_seg(L->length - total + 200, 0); /* through the finish line and on for the victory lap */

    float x = 0, y = 0, th = 0, step = L->length / (MAP_PTS - 1); /* the map runs left to right */
    for (int i = 0; i < MAP_PTS; i++) {
        s_map_x[i] = x;
        s_map_y[i] = y;
        th += curve_at(i * step) * MAP_K * step;
        x += cosf(th) * step;
        y += sinf(th) * step;
    }
}

static void build_level(int lv)
{
    const level_t *L = &k_levels[lv];
    build_track(lv, L);

    s_nlakes = 0;
    if (s_season == SUMMER)
        for (float s = rng_float(50, 90); s < L->length - 40 && s_nlakes < MAX_LAKES; s += rng_float(80, 150)) {
            float len = rng_float(30, 55);
            s_lake[s_nlakes][0] = s, s_lake[s_nlakes][1] = s + len, s_lake[s_nlakes][2] = rng_next() & 1 ? 1 : -1;
            s_nlakes++;
            s += len;
        }
    static const uint32_t k_autumn[5] = {0xE8B830, 0xE07A28, 0xC8402A, 0xD89A28, 0x5A9A40}; /* yellow, orange, red, amber, green */

    s_nobs = 0;
    for (float s = 45; s < L->length - 25 && s_nobs < MAX_OBS - 8; s += rng_float(L->gap_lo, L->gap_hi)) {
        obs_t o = {L->kinds[rng_next() % L->nkinds], s, rng_float(-0.75f, 0.75f), 0, 0, false, 0, 0};
        if (o.type == O_WORKS) { /* road works close one lane: two cones, then three barriers */
            if (s + 12 > L->length - 25) continue;
            float lane = (float)((int)(rng_next() % 3) - 1) * 0.66f;
            s_obs[s_nobs++] = (obs_t){O_CONE, s, lane - 0.18f, 0, 0, false, 0, 0};
            s_obs[s_nobs++] = (obs_t){O_CONE, s + 1.2f, lane + 0.18f, 0, 0, false, 0, 0};
            for (int k = 0; k < 3; k++) s_obs[s_nobs++] = (obs_t){O_BARRIER, s + 4 + k * 3, lane, 0, 0, false, 0, 0};
            s += 12;
            continue;
        }
        if (o.type == O_CAR) {
            static const uint32_t cars[4] = {0x3D74B8, 0xC0503A, 0xF2F2F2, 0x5DBB3A};
            o.color = cars[rng_next() % 4];
            o.v = L->speed * rng_float(0.45f, 0.65f);
            o.x = (rng_next() % 3 - 1) * 0.55f;
        }
        if (o.type == O_DUCK) {
            o.x = rng_next() & 1 ? -1.4f : 1.4f;
            o.vx = o.x > 0 ? -0.35f : 0.35f;
        }
        if (o.type == O_TREE) {
            o.x = (rng_next() & 1 ? 1 : -1) * rng_float(1.5f, 2.2f);
            if (lake_at(s, 6) * o.x > 0) o.x = -o.x; /* not in the lake */
            o.color = k_autumn[rng_next() % 5];
        }
        s_obs[s_nobs++] = o;
    }
    for (float s = 8; s < L->length + 60 && s_nobs < MAX_OBS - 12; s += s_season == SUMMER ? 22 : 16) { /* roadside trees: scenery, but solid */
        float side = s_nobs & 1 ? 1 : -1;
        if (lake_at(s, 6) == (int)side) side = -side;
        s_obs[s_nobs] = (obs_t){O_TREE, s, side * rng_float(1.4f, 2.4f), 0, 0, false, k_autumn[rng_next() % 5], 0};
        s_nobs++;
    }
    s_nscn = 0; /* a forest in autumn, firs in winter, a few trees between the lakes in summer */
    const float step = s_season == AUTUMN ? 2.2f : s_season == WINTER ? 3.2f : 6.0f;
    for (float s = 2; s < L->length + 60 && s_nscn < MAX_SCN; s += rng_float(step * 0.6f, step * 1.4f)) {
        float side = rng_next() & 1 ? 1 : -1;
        if (lake_at(s, 4) == (int)side) side = -side;
        s_scn[s_nscn++] = (scn_t){s, side * rng_float(2.2f, 5.0f), k_autumn[rng_next() % 5]};
    }
}

static void start_level(int lv, uint32_t now)
{
    rng_seed(rng_next() ^ now * 2654435761u); /* the tap's timing makes every race different, even right after boot */
    s_level = lv;
    pick_look();
    build_level(lv);
    s_s = s_v = s_x = s_vx = s_t = s_invuln = s_shake = s_boost = s_skid = s_bg = s_win_t = 0;
    s_ncracks = 0;
    s_vh = 142;
    s_lives = LIVES;
#ifdef HOST_DEBUG
    if (getenv("RACE_FRAGILE")) s_lives = 1; /* tests only: reach the game-over screen fast */
#endif
    s_count = 3.0f;
    s_state = COUNTDOWN;
    static const uint32_t colors[RIVALS] = {0x3D74B8, 0x8A4AC0, 0x2E9A5A};
    static const float pace[3][2] = {{0.80f, 0.97f}, {0.86f, 1.02f}, {0.92f, 1.07f}};
    int slot[RIVALS] = {0, 1, 2}; /* a grid start in a random order: rivals line up just ahead in the lanes */
    for (int i = RIVALS - 1; i > 0; i--) {
        int j = (int)(rng_next() % (uint32_t)(i + 1)), t = slot[i];
        slot[i] = slot[j];
        slot[j] = t;
    }
    for (int i = 0; i < RIVALS; i++) {
        rival_t *rv = &s_riv[i];
        memset(rv, 0, sizeof(*rv));
        rv->s = 2.5f + slot[i] * 2.2f;
        rv->lane = rv->x = (float)((slot[i] % 3) - 1) * 0.66f;
        rv->speed = k_levels[lv].speed * rng_float(pace[lv][0], pace[lv][1]);
        rv->phase = rng_float(0, 6.28f);
        rv->corner = rng_float(0.4f, 1);  /* 1: full speed through corners */
        rv->weave = rng_float(0, 0.12f);  /* lane wander */
        rv->weave_f = rng_float(0.6f, 1.6f);
        rv->aggr = rng_float(0, 1);       /* >0.5 overtakes, otherwise waits behind */
        rv->slip_rate = rng_float(0.5f, 1.5f) * (0.02f + 0.01f * lv); /* mistakes per second */
        rv->surge = rv->surge_to = 1;
        rv->surge_t = rng_float(1, 3);
        rv->change_t = rng_float(3, 9);
        rv->color = colors[i];
    }
    s_place = 0;
}

static void enter(uint32_t now)
{
    (void)now;
    s_state = SELECT;
    for (int i = 0; i < 3; i++) {
        char key[16];
        snprintf(key, sizeof(key), "race_best%d", i);
        s_best[i] = app_load(key, 0);
    }
    s_wheel = s_wheel_v = 0;
}

static float obs_halfw(otype_t t)
{
    switch (t) {
    case O_CONE: return 0.09f;
    case O_BARREL: return 0.14f;
    case O_ROCK: return 0.17f;
    case O_CAR: return CAR_HW;
    case O_OIL: case O_PUDDLE: return 0.26f;
    case O_BOOST: return 0.2f;
    case O_DUCK: return 0.12f;
    case O_DEER: return 0.2f;
    case O_WRECK: return CAR_HW;
    case O_SIGN: return 0.12f;
    case O_HAY: return 0.17f;
    case O_BARRIER: return 0.16f; /* drawn about one lane wide */
    case O_TREE: return 0.2f;
    default: return 0.3f;
    }
}

static void add_crack(void)
{
    if (s_ncracks >= MAX_CRACKS) return;
    static const float zones[4][4] = {{6, 20, 50, 128}, {270, 20, 314, 128}, {36, 112, 110, 138}, {210, 112, 284, 138}};
    const float *z = zones[rng_next() % 4]; /* edges only: the road in the middle stays visible */
    s_crack[s_ncracks][0] = rng_float(z[0], z[2]);
    s_crack[s_ncracks][1] = rng_float(z[1], z[3]);
    s_crack[s_ncracks][2] = rng_float(0, 100);
    s_ncracks++;
}

static void crash(void)
{
    if (s_invuln > 0) return;
    add_crack();
    s_lives--;
    s_v *= 0.3f;
    s_invuln = 1.6f;
    s_shake = 0.45f;
    s_wheel_v += (rng_next() & 1 ? 1 : -1) * 6;
}

static void update(const input_t *in, uint32_t now, float dt)
{
    s_clock += dt;
    /* the wheel: a damped spring towards the board's tilt (upright = straight ahead) */
    float tilt = atan2f(in->gix, fmaxf(0.2f, in->giy));
    float target = clampf(tilt * 2.4f, -1.5f, 1.5f); /* full lock at ~20 degrees of tilt */
    s_wheel_v += (110 * (target - s_wheel) - 13 * s_wheel_v) * dt;
    s_wheel += s_wheel_v * dt;
    if (s_shake > 0) s_shake -= dt;

    if (s_state == SELECT) {
        if (in->tap) {
            int i = (int)clampf((in->start_x - 8) / 102.0f, 0, 2);
            if (in->start_y > 40 && in->start_y < 150) start_level(i, now);
        }
        return;
    }
    if (s_state == FINISHED) {
        s_win_t += dt; /* our car keeps driving off behind the banner */
        for (int r = 0; r < RIVALS; r++) /* once it is gone, the others cross the line one after another, no drifting */
            if (s_riv[r].wreck_t <= 0 && (s_win_t > WIN_DRIVE_S || s_riv[r].s > s_s)) s_riv[r].s += s_riv[r].speed * dt;
        for (int i = 0; i < CONFETTI; i++) {
            s_cf[i][1] += (45 + (i % 5) * 9) * dt;
            s_cf[i][0] += 18 * sinf(s_cf[i][2] + s_cf[i][1] * 0.05f) * dt;
            if (s_cf[i][1] > VIEW_H) {
                s_cf[i][1] = -6;
                s_cf[i][0] = rng_float(0, GFX_W);
            }
        }
    }
    if (s_state == VICTORY_LAP) { /* third person: our car drifts away into the distance */
        s_win_t += dt;
        for (int r = 0; r < RIVALS; r++) /* only those who finished before us drive on; the rest wait out of view */
            if (s_riv[r].wreck_t <= 0 && s_riv[r].s > s_s) s_riv[r].s += s_riv[r].speed * dt;
        if (s_win_t > WIN_BANNER_S) {
            s_state = FINISHED;
            s_end_at = now;
        }
        return;
    }
    if (s_state == CRASHED_OUT || s_state == FINISHED) {
        if (in->tap && now - s_end_at > 800) {
            if (s_state == FINISHED && s_level < 2) start_level(s_level + 1, now);
            else if (s_state == FINISHED) s_state = SELECT;
            else start_level(s_level, now);
        }
        return;
    }
    const level_t *L = &k_levels[s_level];
    if (s_state == COUNTDOWN) {
        if ((s_count -= dt) <= 0) s_state = RACE;
        return;
    }

    s_t += dt;
    if (s_invuln > 0) s_invuln -= dt;
    if (s_boost > 0) s_boost -= dt;
    if (s_skid > 0) s_skid -= dt;
    bool off_road = fabsf(s_x) > 1.0f;
    float vmax = L->speed * (s_boost > 0 ? 1.45f : 1) * (off_road ? 0.15f : 1) * (in->down ? 0.3f : 1); /* no short cuts over the grass */
    s_v += clampf(vmax - s_v, -30 * dt, 9 * dt);
    if (off_road && fmodf(s_t, 0.12f) < 0.06f) s_shake = fmaxf(s_shake, 0.1f);

    float steer = clampf(s_wheel / 0.85f, -1, 1);
    if (fabsf(steer) < 0.03f) steer = 0;
    float c = curve_at(s_s + 2);
#ifdef HOST_DEBUG
    if (getenv("RACE_AUTOPILOT")) { /* tests only: pick a free lane and hold it */
        float best = 0, lanes[3] = {0, -0.66f, 0.66f};
        for (int l = 0; l < 3; l++) {
            bool free = true;
            for (int i = 0; i < s_nobs; i++) {
                float ahead = s_obs[i].s - s_s;
                if (!s_obs[i].hit && s_obs[i].type != O_TREE && s_obs[i].type != O_BOOST && ahead > 0 && ahead < 9 &&
                    fabsf(s_obs[i].x - lanes[l]) < 0.45f) free = false;
            }
            if (free) { best = lanes[l]; break; }
        }
        steer = clampf(((best - s_x) * 2.5f + c * 0.7f * s_v) / 1.9f, -1, 1);
    }
#endif
    s_vx = steer * 1.9f - c * 0.7f * s_v; /* constant steering authority against the centrifugal push */
    if (s_skid > 0) s_vx += 1.6f * sinf(s_t * 18) * s_skid; /* oil: the car fishtails */
    s_x = clampf(s_x + s_vx * dt, -2.2f, 2.2f);
    s_s += s_v * dt;
    s_bg += c * s_v * dt * 90;

    if (s_level > 0) { /* rare events */
        if (rng_float(0, 1) < EVENT_DEER_RATE * dt && s_nobs < MAX_OBS) {
            float side = rng_next() & 1 ? 1 : -1;
            s_obs[s_nobs++] = (obs_t){O_DEER, s_s + 22, side * 1.7f, 0, -side * 1.3f, false, 0, 0};
        }
        float rock_rate = s_level == 2 ? EVENT_ROCK_RATE : 0;
#ifdef HOST_DEBUG
        if (getenv("RACE_ROCKS") && s_level == 2 && s_t > 4 && s_t - dt <= 4) rock_rate = 1 / dt; /* tests only: one rockfall at 4 s */
#endif
        if (rng_float(0, 1) < rock_rate * dt && s_nobs < MAX_OBS - 3) { /* rocks roll in from one verge; the far lane stays clear */
            float side = rng_next() & 1 ? 1 : -1, x0 = side * 1.8f;
            int n = 2 + (int)(rng_next() % 2);
            for (int k = 0; k < n; k++) {
                float target = side * (k % 2 ? 0.66f : 0) + rng_float(-0.12f, 0.12f);
                s_obs[s_nobs++] = (obs_t){O_ROCK, s_s + rng_float(30, 36) + k * 1.5f, x0, 0, (target - x0) * ROCK_K, false, 0, rng_float(0, 6)};
            }
        }
        float crash_rate = EVENT_CRASH_RATE;
#ifdef HOST_DEBUG
        bool calm = true;
        for (int r = 0; r < RIVALS; r++) calm &= s_riv[r].wreck_t <= 0;
        if (getenv("RACE_CRASH") && s_t > 4 && calm) crash_rate = 50; /* tests only: a pile-up as soon as a rival is in range */
#endif
        if (rng_float(0, 1) < crash_rate * dt && s_nobs < MAX_OBS - 1)
            for (int r = 0; r < RIVALS; r++) {
                float z = s_riv[r].s - s_s;
                if (s_riv[r].wreck_t > 0 || z < 14 || z > 36) continue;
                s_riv[r].wreck_t = 0.01f; /* the rival hits another car: both stop, the other one towards the verge */
                float side = s_riv[r].x > 0.2f ? 1 : s_riv[r].x < -0.2f ? -1 : rng_next() & 1 ? 1 : -1;
                float other = s_riv[r].x + side * 0.4f;
                s_obs[s_nobs++] = (obs_t){O_WRECK, s_riv[r].s + 0.6f, other, 0, 0, false, 0xF2F2F2, 0};
                s_obs[s_nobs++] = (obs_t){O_SIGN, s_riv[r].s - 7, s_riv[r].x, 0, 0, true, 0, 0}; /* warning triangle, scenery */
                break;
            }
    }
    for (int r = 0; r < RIVALS; r++) { /* rivals: own pace, change lanes to dodge what is ahead */
        rival_t *rv = &s_riv[r];
        if (rv->wreck_t > 0) { /* stopped after a crash; drives on a while after we pass */
            rv->wreck_t += dt;
            if (rv->wreck_t > 6 && rv->s < s_s - 3) {
                rv->wreck_t = 0;
                rv->speed *= 0.8f;
            }
            if (fabsf(rv->s - s_s) < 0.8f && fabsf(rv->x - s_x) < CAR_HW + PLAYER_W) crash();
            continue;
        }
        if ((rv->surge_t -= dt) <= 0) { /* the pace wanders a little */
            rv->surge_to = rng_float(0.95f, 1.05f);
            rv->surge_t = rng_float(1, 3);
        }
        rv->surge += (rv->surge_to - rv->surge) * fminf(1, dt);
        float pace = rv->surge * (1 - 0.15f * (1 - rv->corner) * fminf(fabsf(curve_at(rv->s + 4)) / L->curve_max, 1.3f));
        if (rv->mistake_t > 0) {
            rv->mistake_t -= dt;
            pace *= rv->spin != 0 ? 0.5f : 0.55f;
            rv->x = clampf(rv->x + rv->spin * 0.5f * dt, -0.9f, 0.9f);
        } else if (s_t > 3 && rng_float(0, 1) < rv->slip_rate * dt) { /* a mistake: brakes too hard or slides out */
            rv->spin = (float)((int)(rng_next() % 3) - 1);
            rv->mistake_t = rv->spin != 0 ? 1.0f : 1.2f;
        }
        bool stuck = false; /* a car just ahead in this lane: the bold ones go round, the rest wait */
        for (int k = -1; k < RIVALS; k++) {
            float os = k < 0 ? s_s : s_riv[k].s, ox = k < 0 ? s_x : s_riv[k].x;
            if (k != r && os - rv->s > 0 && os - rv->s < 4 && fabsf(ox - rv->lane) < 0.35f) stuck = true;
        }
        if ((rv->change_t -= dt * (0.5f + rv->aggr)) <= 0 || (stuck && rv->aggr > 0.5f && fabsf(rv->x - rv->lane) < 0.1f)) {
            rv->lane = fabsf(rv->lane) > 0.3f ? 0 : (rng_next() & 1 ? 0.66f : -0.66f);
            rv->change_t = rng_float(3, 9);
        } else if (stuck)
            pace *= 0.9f;
        if (rv->s < L->length + 20) rv->s += rv->speed * pace * dt;
        for (int i = 0; i < s_nobs; i++) {
            const obs_t *o = &s_obs[i];
            float ahead = o->s - rv->s;
            if (o->type != O_TREE && o->type != O_BOOST && ahead > 0 && ahead < 7 && fabsf(o->x - rv->lane) < 0.35f)
                rv->lane = rv->lane > 0.3f ? 0 : rv->lane < -0.3f ? 0 : (o->x > 0 ? -0.66f : 0.66f);
        }
        float wander = rv->weave * sinf(s_t * rv->weave_f + rv->phase);
        rv->x += clampf(rv->lane + wander - rv->x, -1.2f * dt, 1.2f * dt);
        float dz = rv->s - s_s; /* bumping a rival costs speed, not a life */
        if (fabsf(dz) < 0.9f && fabsf(rv->x - s_x) < CAR_HW + PLAYER_W) {
            s_x += (s_x > rv->x ? 1 : -1) * 0.04f;
            s_v *= 0.97f;
            if (s_shake < 0.1f) s_shake = 0.12f;
        }
    }

    for (int i = 0; i < s_nobs; i++) {
        obs_t *o = &s_obs[i];
        o->s += o->v * dt;
        if (o->type == O_DUCK && o->s - s_s < 26) {
            o->x += o->vx * dt;
            if (fabsf(o->x) > 1.5f) o->vx = -o->vx;
        }
        if (o->type == O_ROCK && o->vx != 0) { /* rolling in: exact exponential slow-down, so it stops where it was aimed */
            float decay = expf(-ROCK_K * dt);
            o->x += o->vx * (1 - decay) / ROCK_K;
            o->vx *= decay;
            o->timer += dt * 9;
            if (fabsf(o->vx) < 0.03f) o->vx = 0;
        }
        if (o->type == O_DEER) { /* leaps in, freezes in the headlights, then bolts across */
            if (o->timer == 0 && fabsf(o->x) < 0.35f) o->timer = 1.6f;
            if (o->timer > 0) {
                if ((o->timer -= dt) <= 0) o->timer = -1;
            } else
                o->x += o->vx * dt;
        }
        float dz = o->s - s_s;
        if (o->hit || dz > 0.8f || dz < -0.3f) continue;
        if (fabsf(o->x - s_x) > obs_halfw(o->type) + PLAYER_W) continue;
        o->hit = true;
        switch (o->type) {
        case O_OIL: s_skid = 1.4f; break;
        case O_PUDDLE: s_v *= 0.6f; s_shake = 0.2f; break;
        case O_BOOST: s_boost = 1.6f; break;
        default: crash(); break; /* trees included: it stays standing, we lose a life */
        }
    }
    if (s_lives <= 0) {
        s_state = CRASHED_OUT;
        s_end_at = now;

    } else if (s_s >= L->length) {
        s_state = VICTORY_LAP;
        s_win_t = 0;
        s_vh = GFX_H;
        for (int r = 0; r < RIVALS; r++) s_riv[r].mistake_t = 0;
        for (int r = 0, k = 0; r < RIVALS; r++) /* rivals still behind arrive one after another */
            if (s_riv[r].s < s_s && s_riv[r].wreck_t <= 0) {
                s_riv[r].s = s_s - 2.0f - k * 7.0f;
                s_riv[r].speed = L->speed * 0.6f; /* unhurried, so they stay on screen for a while */
                k++;
            }
        s_end_at = now;
        s_place = position();
        for (int i = 0; i < CONFETTI; i++) {
            s_cf[i][0] = rng_float(0, GFX_W);
            s_cf[i][1] = rng_float(-GFX_H, 0);
            s_cf[i][2] = rng_float(0, 6.28f);
        }
        int32_t t10 = (int32_t)(s_t * 10);
        if (!s_best[s_level] || t10 < s_best[s_level]) {
            char key[16];
            snprintf(key, sizeof(key), "race_best%d", s_level);
            s_best[s_level] = t10;
            app_save(key, t10);
        }
    }
}

/* ---------- drawing ---------- */

static uint32_t pal(char ch, uint32_t car)
{
    switch (ch) {
    case 'o': return 0xF08A2A;
    case 'w': return 0xF2F2F2;
    case 'r': return 0xC0503A;
    case 'g': return 0x8A8A90;
    case 'G': return 0x6A6A70;
    case 'c': return car;
    case 'C': return mixc(car, 0xFFFFFF, 90); /* lit side of an autumn crown */
    case 'k': return 0x202024;
    case 'l': return 0xFF5050;
    case 'b': return 0x6EB4E0;
    case 'y': return 0xF7D23C;
    case 'n': return 0x6B4A3A;
    case 't': return 0x2E7D32;
    case 'T': return 0x43A047;
    case 'm': return MASCOT_RGB;
    case 'R': return 0xC0302A;
    case 'h': return 0xE0B848; /* hay */
    case 'H': return 0xA87C28;
    default: return 0;
    }
}

static const char *const k_sign[] = {"....y....", "...yyy...", "...yky...", "..yykyy..", "..yykyy..", ".yyyyyyy.",
                                     ".yyykyyy.", "yyyyyyyyy", "...g.g..."};
static const char *const k_hay[] = {"..hhh..", ".hHHHh.", "hHhhhHh", "hHhHhHh", "hHhhhHh", ".hHHHh."}; /* round bale, rolled face on */
static const char *const k_barrier[] = {"o.......o", "RRwwRRwwR", "RwwRRwwRR", "wwRRwwRRw", "g.......g", "g.......g"};
static const char *const k_cone[] = {"..o..", "..o..", ".owo.", ".ooo.", "owwwo", "ooooo"};
static const char *const k_barrel[] = {".rrrr.", "rwwwwr", "rrrrrr", "rwwwwr", "rrrrrr", "rwwwwr", ".rrrr."};
static const char *const k_rock[] = {"..ggg..", ".ggGgg.", "gGgggGg", "ggggggg", ".ggggg."};
static const char *const k_car[] = {"..kkkkk..", ".kbbbbbk.", "ccccccccc", "clccccclc", "ccccccccc", "kk.....kk"};
static const char *const k_duck[] = {"....yy....", "...yyyy...", "...yykyo..", "...yyyyoo.", "y..yyyy...", "yyyyyyyyy.", "yyyyyyyyy.", ".yyyyyyy.."};
static const char *const k_deer[] = {"..........w.", ".........ww.", "........www.", ".........nn.", "........nnnk", "nnnnnnnnnn..", "nnnnnnnnn...",
                                     "nnnnnnnn....", ".n.n...n.n..", ".n.n...n.n.."};
/* Our car on the victory lap, as a tiny 3D model in perspective (like the reference: seen from behind and above,
 * nose turned away to the right, so the rear and one side show and only tail lights are visible).
 * Car frame: X forward from the rear (length 1), Y from the visible side inwards, Z up. */
typedef struct {
    float ox, oy, s, cy, sy; /* screen anchor of the rear-near-bottom corner, px per car length, cos/sin of yaw */
} car_view_t;

#define CAR_W 0.52f

static void car_pt(const car_view_t *v, float X, float Y, float Z, float *sx, float *sy)
{
    const float D = 2.6f, CAM_H = 1.2f, PS = 0.343f, PC = 0.939f; /* camera distance, height, pitch 20 deg down */
    const float d0 = CAM_H * PS + D * PC, up0 = -CAM_H * PC + D * PS;
    float x = X * v->sy - Y * v->cy, z = D + X * v->cy + Y * v->sy, y = Z - CAM_H;
    float d = -y * PS + z * PC, up = y * PC + z * PS;
    *sx = v->ox + v->s * d0 * x / d;
    *sy = v->oy - v->s * d0 * (up / d - up0 / d0);
}

static void car_face(uint16_t *fb, const car_view_t *v, const float p[4][3], uint32_t c)
{
    float xs[4], ys[4];
    for (int i = 0; i < 4; i++) car_pt(v, p[i][0], p[i][1], p[i][2], &xs[i], &ys[i]);
    gfx_fill_quad(fb, xs, ys, gfx_color(c), 0);
}

/* wheel: an octagon in the X-Z plane at side Y, centre height zc, filled as three quads */
static void car_wheel(uint16_t *fb, const car_view_t *v, float X, float Y, float r, float zc, uint32_t c)
{
    float xs[8], ys[8];
    for (int i = 0; i < 8; i++) {
        float a = (i + 0.5f) * 0.7854f;
        car_pt(v, X + r * cosf(a), Y, zc + r * sinf(a), &xs[i], &ys[i]);
    }
    static const int q[3][4] = {{0, 1, 2, 3}, {0, 3, 4, 7}, {4, 5, 6, 7}};
    for (int k = 0; k < 3; k++) {
        float qx[4], qy[4];
        for (int i = 0; i < 4; i++) qx[i] = xs[q[k][i]], qy[i] = ys[q[k][i]];
        gfx_fill_quad(fb, qx, qy, gfx_color(c), 0);
    }
}

#define FACE(...) car_face(fb, v, (const float[4][3]){__VA_ARGS__}
/* A low sports car: wedge body (the bonnet dips to the nose), fastback cabin, rear wing, twin white stripes,
 * a full-width tail-light bar and twin exhausts. */
static void draw_car34(uint16_t *fb, const car_view_t *v)
{
    const uint32_t rear = 0xB0221C, side = 0xD8302A, top = 0xF0503F, glass = 0x16202E, tyre = 0x141418, dark = 0x222228, white = 0xF2F2F2;
    const float W = CAR_W, BZ0 = 0.06f, BZ1 = 0.22f, NZ = 0.15f, RZ = 0.34f, WR = 0.105f, YI = 0.07f;
    const float S0 = W / 2 - 0.08f, S1 = W / 2 - 0.035f, S2 = W / 2 + 0.035f, S3 = W / 2 + 0.08f; /* stripe edges */
#define TOPZ(X) (BZ1 + (NZ - BZ1) * (X))
    FACE({-0.04f, -0.06f, 0}, {1.04f, -0.06f, 0}, {1.04f, W + 0.04f, 0}, {-0.04f, W + 0.04f, 0}), 0x1A1A1E); /* shadow */
    car_wheel(fb, v, 0.2f, W, WR, WR, tyre); /* far side first: the body hides most of them */
    car_wheel(fb, v, 0.78f, W, WR, WR, tyre);
    /* body */
    FACE({0, 0, BZ0}, {0, W, BZ0}, {0, W, BZ1}, {0, 0, BZ1}), rear);
    FACE({0, 0, BZ0}, {1, 0, BZ0}, {1, 0, NZ}, {0, 0, BZ1}), side);
    FACE({0, 0, BZ1}, {1, 0, NZ}, {1, W, NZ}, {0, W, BZ1}), top);
    FACE({0, S0, BZ1}, {1, S0, NZ}, {1, S1, NZ}, {0, S1, BZ1}), white);
    FACE({0, S2, BZ1}, {1, S2, NZ}, {1, S3, NZ}, {0, S3, BZ1}), white);
    FACE({0.30f, -0.001f, 0.11f}, {0.42f, -0.001f, 0.11f}, {0.42f, -0.001f, 0.17f}, {0.32f, -0.001f, 0.17f}), dark); /* side intake */
    /* fastback cabin */
    const float CR = 0.32f, CF = 0.80f, RR = 0.52f, RF = 0.66f;
    FACE({CR, YI, TOPZ(CR)}, {CR, W - YI, TOPZ(CR)}, {RR, W - YI, RZ}, {RR, YI, RZ}), glass);
    FACE({CR, YI, TOPZ(CR)}, {CF, YI, TOPZ(CF)}, {RF, YI, RZ}, {RR, YI, RZ}), glass);
    FACE({RR, YI, RZ}, {RF, YI, RZ}, {RF, W - YI, RZ}, {RR, W - YI, RZ}), top);
    FACE({RR, S0, RZ}, {RF, S0, RZ}, {RF, S1, RZ}, {RR, S1, RZ}), white);
    FACE({RR, S2, RZ}, {RF, S2, RZ}, {RF, S3, RZ}, {RR, S3, RZ}), white);
    /* rear wing on two struts */
    FACE({0.04f, 0.12f, TOPZ(0.04f)}, {0.04f, 0.15f, TOPZ(0.04f)}, {0.04f, 0.15f, 0.30f}, {0.04f, 0.12f, 0.30f}), dark);
    FACE({0.04f, W - 0.15f, TOPZ(0.04f)}, {0.04f, W - 0.12f, TOPZ(0.04f)}, {0.04f, W - 0.12f, 0.30f}, {0.04f, W - 0.15f, 0.30f}), dark);
    FACE({-0.03f, 0.0f, 0.30f}, {0.09f, 0.0f, 0.30f}, {0.09f, W, 0.30f}, {-0.03f, W, 0.30f}), 0x34343C);
    FACE({-0.03f, 0.0f, 0.28f}, {-0.03f, W, 0.28f}, {-0.03f, W, 0.30f}, {-0.03f, 0.0f, 0.30f}), dark);
    FACE({-0.03f, -0.001f, 0.26f}, {0.09f, -0.001f, 0.26f}, {0.09f, -0.001f, 0.31f}, {-0.03f, -0.001f, 0.31f}), dark); /* end plate */
    /* rear: stripes run down, diffuser, exhausts, tail-light bar */
    FACE({-0.002f, S0, 0.10f}, {-0.002f, S1, 0.10f}, {-0.002f, S1, BZ1}, {-0.002f, S0, BZ1}), white);
    FACE({-0.002f, S2, 0.10f}, {-0.002f, S3, 0.10f}, {-0.002f, S3, BZ1}, {-0.002f, S2, BZ1}), white);
    FACE({-0.002f, 0, BZ0}, {-0.002f, W, BZ0}, {-0.002f, W, 0.10f}, {-0.002f, 0, 0.10f}), dark);
    FACE({-0.004f, 0.08f, 0.068f}, {-0.004f, 0.14f, 0.068f}, {-0.004f, 0.14f, 0.092f}, {-0.004f, 0.08f, 0.092f}), 0x8A8A92);
    FACE({-0.004f, W - 0.14f, 0.068f}, {-0.004f, W - 0.08f, 0.068f}, {-0.004f, W - 0.08f, 0.092f}, {-0.004f, W - 0.14f, 0.092f}), 0x8A8A92);
    FACE({-0.003f, 0.02f, 0.165f}, {-0.003f, W - 0.02f, 0.165f}, {-0.003f, W - 0.02f, 0.195f}, {-0.003f, 0.02f, 0.195f}), 0xFF3A30);
    /* near wheels on top of the body side: big tyres, light rims */
    for (int i = 0; i < 2; i++) {
        float X = i ? 0.78f : 0.2f;
        car_wheel(fb, v, X, -0.01f, WR, WR, tyre);
        car_wheel(fb, v, X, -0.012f, 0.06f, WR, 0xB8B8C0);
        car_wheel(fb, v, X, -0.013f, 0.025f, WR, 0x505058);
    }
#undef TOPZ
}
#undef FACE

static const char *const k_tree[] = {"..ttt..", ".tTttt.", "tTtttTt", "ttTtttt", ".ttttt.", "...n...", "...n...", "...n..."};
static const char *const k_tree_autumn[] = {"..ccc..", ".cCccc.", "cCcccCc", "ccCcccc", ".ccccc.", "...n...", "...n...", "...n..."};
static const char *const k_fir[] = {"....w....", "...www...", "...tTt...", "..wwtww..", "..ttTtt..", ".wwtwwww.", ".tttTttt.",
                                    "wwwttwwww", "ttttTtttt", "....n....", "....n...."};

static void sprite(uint16_t *fb, const char *const *rows, int nr, float cx, float bottom, float w_px, uint32_t car)
{
    int nc = 0;
    while (rows[0][nc]) nc++;
    float cell = w_px / nc;
    if (cell < 0.5f) return;
    float x0 = cx - w_px / 2, y0 = bottom - nr * cell;
    char keys[8];
    uint16_t cols[8];
    int nk = 0;
    for (int r = 0; r < nr; r++)
        for (int k = 0; rows[r][k]; k++) {
            char ch = rows[r][k];
            if (ch == '.' || ch == ' ') continue;
            int c = 0;
            while (c < nk && keys[c] != ch) c++;
            if (c == nk && nk < 8) keys[nk] = ch, cols[nk++] = fcol(pal(ch, car));
            gfx_fill_box(fb, x0 + k * cell, y0 + r * cell, x0 + (k + 1) * cell, y0 + (r + 1) * cell, cols[c < nk ? c : nk - 1], 0);
        }
}

static void sprite_mirror(uint16_t *fb, const char *const *rows, int nr, float cx, float bottom, float w_px, bool mirror)
{
    int nc = 0;
    while (rows[0][nc]) nc++;
    float cell = w_px / nc;
    if (cell < 0.5f) return;
    float x0 = cx - w_px / 2, y0 = bottom - nr * cell;
    for (int r = 0; r < nr; r++)
        for (int k = 0; k < nc; k++) {
            char ch = rows[r][mirror ? nc - 1 - k : k];
            if (ch == '.' || ch == ' ') continue;
            uint32_t col = ch == 'n' ? 0x9A6A3A : ch == 'w' ? 0xD8C8A8 : pal(ch, 0);
            gfx_fill_box(fb, x0 + k * cell, y0 + r * cell, x0 + (k + 1) * cell, y0 + (r + 1) * cell, fcol(col), 0);
        }
}

/* sprite rotated about its bottom centre (crashed cars, the drifting car) */
static void sprite_rot(uint16_t *fb, const char *const *rows, int nr, float cx, float bottom, float w_px, uint32_t car, float ang)
{
    int nc = 0;
    while (rows[0][nc]) nc++;
    float cell = w_px / nc;
    if (cell < 0.5f) return;
    for (int r = 0; r < nr; r++)
        for (int k = 0; k < nc; k++) {
            char ch = rows[r][k];
            if (ch == '.' || ch == ' ') continue;
            float lx = (k - nc / 2.0f) * cell, ly = (r - nr) * cell;
            gfx_fill_rot_box(fb, cx, bottom, ang, lx, ly, lx + cell + 0.6f, ly + cell + 0.6f, fcol(pal(ch, car)));
        }
}

/* procedural smoke: puffs rising from (x, y), size relative to w */
static void smoke(uint16_t *fb, float x, float y, float w, float density)
{
    for (int k = 0; k < 4; k++) {
        float ph = fmodf(s_clock * 0.7f + k * 0.25f, 1.0f);
        if (ph > density + 0.25f) continue;
        float r = w * (0.12f + 0.25f * ph), px = x + w * 0.2f * sinf(k * 2.1f + ph * 3), py = y - ph * w * 1.2f;
        gfx_fill_box_dither(fb, px - r, py - r * 0.7f, px + r, py + r * 0.7f, gfx_color(ph < 0.4f ? 0x6A6A70 : 0x9A9AA0));
        gfx_fill_box_dither(fb, px - r * 0.6f, py - r, px + r * 0.6f, py + r, gfx_color(ph < 0.4f ? 0x6A6A70 : 0x9A9AA0));
    }
}

/* out of lives: the whole windshield shatters from the centre, rings and diagonal rays */
static void shatter(uint16_t *fb)
{
    const uint16_t c = gfx_color(0xE0E8F0);
    const float cx = 160, cy = 70;
    gfx_fill_box_dither_sparse(fb, cx - 34, cy - 24, cx + 34, cy + 24, gfx_color(0xC8D4E0)); /* frosted impact */
    for (int ring = 0; ring < 3; ring++) {
        float r = 14 + ring * 22;
        for (int k = 0; k < 18; k++) { /* each ring is a chain of short chords, slightly jagged */
            float a0 = k * 0.349f, a1 = a0 + 0.349f, rj0 = r + 3 * sinf(k * 2.7f + ring), rj1 = r + 3 * sinf((k + 1) * 2.7f + ring);
            float x0 = cx + rj0 * cosf(a0), y0 = cy + rj0 * 0.75f * sinf(a0), x1 = cx + rj1 * cosf(a1), y1 = cy + rj1 * 0.75f * sinf(a1);
            float len = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
            gfx_fill_rot_box(fb, x0, y0, atan2f(y1 - y0, x1 - x0), 0, -0.6f, len, 0.6f, c);
        }
    }
    for (int k = 0; k < 12; k++) { /* rays out to the frame */
        float a = k * 0.5236f + 0.2f * sinf(k * 1.9f), px = cx, py = cy;
        for (int seg = 0; seg < 5; seg++) {
            float aa = a + 0.18f * sinf(k * 3.1f + seg * 1.7f), l = 20 + 6 * seg;
            gfx_fill_rot_box(fb, px, py, aa, 0, -0.6f, l, 0.6f, c);
            px += l * cosf(aa);
            py += l * sinf(aa) * 0.8f;
        }
    }
}

static void cracks(uint16_t *fb)
{
    const uint16_t c = gfx_color(0xE0E8F0);
    for (int i = 0; i < s_ncracks; i++) {
        float x = s_crack[i][0], y = s_crack[i][1], seed = s_crack[i][2];
        gfx_fill_box(fb, x - 2, y - 2, x + 2, y + 2, c, 0);
        for (int r = 0; r < 7; r++) { /* short rays with a kink or two */
            float a = r * 0.9f + seed, len = 10 + fmodf(seed * (r + 3), 16), px = x, py = y;
            for (int seg = 0; seg < 3; seg++) {
                float aa = a + 0.35f * sinf(seed + r * 1.7f + seg * 2.3f), l = len / 3;
                gfx_fill_rot_box(fb, px, py, aa, 0, -0.6f, l, 0.6f, c);
                px += l * cosf(aa);
                py += l * sinf(aa);
            }
        }
    }
}

/* screen row for a depth z (z = 1 at the bottom row of the view) */
static float row_for(float z) { return Y_H + (VIEW_H - Y_H) / z; }


static void season_tree(uint16_t *fb, float cx, float y, float sc, uint32_t color)
{
    if (s_season == WINTER) sprite(fb, k_fir, 11, cx, y, HALF_W * sc * 0.55f, 0);
    else if (s_season == AUTUMN) sprite(fb, k_tree_autumn, 8, cx, y, HALF_W * sc * 0.5f, color);
    else sprite(fb, k_tree, 8, cx, y, HALF_W * sc * 0.5f, 0);
}

/* The top half of an ellipse standing on y = base (tree crowns, hills); integer rows from a quarter-circle table. */
static void dome(uint16_t *fb, int cx, int base, int rx, int ry, uint16_t c)
{
    static uint8_t k_q[33]; /* 32 * sqrt(1 - (i/32)^2) */
    if (!k_q[0])
        for (int i = 0; i <= 32; i++) k_q[i] = (uint8_t)(32 * sqrtf(1 - (i / 32.0f) * (i / 32.0f)) + 0.5f);
    for (int dy = 1; dy <= ry; dy++) {
        int w = rx * k_q[(2 * dy - 1) * 16 / ry] / 32;
        gfx_fill_rect(fb, cx - w, base - dy, 2 * w, 1, c);
    }
}

static void draw_world(uint16_t *fb, float shake_x)
{
    const level_t *L = &k_levels[s_level];
    const look_t *K = &k_looks[s_season];
    /* the weather greys the sky and the ground; rain darkens the road */
    uint32_t sky_top = K->sky_top, sky_low = K->sky_low, grass_a = K->grass_a, grass_b = K->grass_b, road_a = K->road_a, road_b = K->road_b;
    if (s_weather == W_RAIN) {
        sky_top = 0x5E6672, sky_low = 0x9AA2AE;
        grass_a = mixc(grass_a, 0x000000, 50), grass_b = mixc(grass_b, 0x000000, 50);
        road_a = mixc(road_a, 0x000000, 60), road_b = mixc(road_b, 0x000000, 60);
    } else if (s_weather == W_FOG) sky_top = sky_low = FOG_RGB;
    else if (s_weather == W_SNOW) sky_top = 0x9AA4B4, sky_low = 0xD4DAE2;
    for (int y = 0; y < (int)Y_H; y++) gfx_fill_rect(fb, 0, y, GFX_W, 1, gfx_color(mixc(sky_top, sky_low, (int)(256 * y / Y_H))));
    s_fog = s_weather == W_FOG ? 200 : 0;
    if (s_season == WINTER) /* mountains with snow caps, parallax with the curves */
        for (int i = 0; i < 6; i++) {
            const int x = (int)fmodf(i * 78 - s_bg * 0.6f + 3200, 470) - 75, h = 22 + (i * 5) % 15, hw = 40 + (i * 9) % 23;
            const uint16_t snow = fcol(0xF4F8FC), rock = fcol(i & 1 ? K->far : 0x5A6A84);
            for (int dy = 0; dy < h; dy++) {
                int w = hw * dy / h;
                gfx_fill_rect(fb, x - w, (int)Y_H - h + dy, 2 * w + 1, 1, dy * 20 < h * 7 ? snow : rock);
            }
        }
    else if (s_season == AUTUMN) { /* a forest on the hills: two rows of round crowns in autumn colours */
        static const uint32_t k_far_crown[5] = {0xC8402A, 0xE8B830, 0xE07A28, 0x6A8A34, 0xB86A2A};
        for (int layer = 0; layer < 2; layer++)
            for (int i = 0; i < 34; i++) {
                int x = (int)fmodf(i * 11 - s_bg * (0.6f + 0.3f * layer) + 4000, 374) - 27, r = 6 + (i * 7 + layer * 3) % 5;
                uint32_t c = k_far_crown[(i * 3 + layer) % 5];
                dome(fb, x, (int)Y_H - (layer ? 0 : 6) - i % 3, r, r * 9 / 10, fcol(layer ? c : mixc(c, sky_low, 90)));
            }
    } else
        for (int i = 0; i < 9; i++) { /* rolling green hills */
            float x = fmodf(i * 46 - s_bg + 3200, 400) - 40, h = 8 + (i * 7) % 12;
            dome(fb, (int)x + 22, (int)Y_H, 34, (int)h, fcol(K->far));
        }
    s_fog = 0;

    s_fog = fog_at(1000);
    gfx_fill_rect(fb, 0, (int)Y_H, GFX_W, 1, fcol(grass_b)); /* the horizon row itself */
    /* road, bottom row to horizon: accumulate the curve (dx'' = curve) */
    float dx = 0, ddx = 0, z_prev = 1;
    curve_cur_t cur = {0, 0};
    for (int y = (int)VIEW_H - 1; y > (int)Y_H; y--) {
        float sc = (y - Y_H) / (VIEW_H - Y_H), z = 1 / sc, dz = z - z_prev;
        z_prev = z;
        ddx += curve_next(&cur, s_s + z) * dz;
        dx += ddx * dz;
        float cx = 160 + shake_x + (dx - s_x) * HALF_W * sc, hw = HALF_W * sc;
        s_row_cx[y] = cx;
        s_row_sc[y] = sc;
        bool band = ((int)((s_s + z) / STRIPE)) & 1;
        s_fog = fog_at(z);
        gfx_fill_rect(fb, 0, y, GFX_W, 1, fcol(band ? grass_a : grass_b));
        int lake = lake_at(s_s + z, 0);
        if (lake) { /* sand, then water out to the edge of the screen; the ends round off */
            float sl = s_s + z, edge = 1e9f;
            for (int i = 0; i < s_nlakes; i++)
                if (sl > s_lake[i][0] && sl < s_lake[i][1]) edge = fminf(sl - s_lake[i][0], s_lake[i][1] - sl);
            float in = hw * (1.3f + 2.2f * (1 - clampf(edge / 10, 0, 1))), sand = hw * 0.14f;
            float w0 = lake > 0 ? cx + in : 0, w1 = lake > 0 ? GFX_W : cx - in;
            gfx_fill_box(fb, lake > 0 ? cx + in - sand : cx - in, y, lake > 0 ? cx + in : cx - in + sand, y + 1, fcol(0xD8C890), 0);
            gfx_fill_box(fb, w0, y, w1, y + 1, fcol(band ? 0x3F8FD0 : 0x4A9ADA), 0);
            if ((y * 7 + (int)(sl * 3)) % 19 == 0) { /* glints */
                float gx = lake > 0 ? cx + in + (GFX_W - cx - in) * h01(y, 3) : (cx - in) * h01(y, 3);
                gfx_fill_box(fb, gx, y, gx + 1 + hw * 0.05f, y + 1, fcol(0xE8F4FF), 0);
            }
        }
        gfx_fill_box(fb, cx - hw, y, cx + hw, y + 1, fcol(band ? road_a : road_b), 0);
        float rw = hw * 0.1f;
        gfx_fill_box(fb, cx - hw - rw, y, cx - hw, y + 1, fcol(band ? 0xD03030 : 0xF2F2F2), 0);
        gfx_fill_box(fb, cx + hw, y, cx + hw + rw, y + 1, fcol(band ? 0xD03030 : 0xF2F2F2), 0);
        if (band) /* three lanes: two dashed dividers */
            for (int k = -1; k <= 1; k += 2) {
                float lx = cx + k * hw / 3;
                gfx_fill_box(fb, lx - hw * 0.022f, y, lx + hw * 0.022f, y + 1, fcol(0xF2F2F2), 0);
            }
        if (s_s + z > L->length && s_s + z < L->length + 3) { /* finish line: checkered */
            for (float x = -hw; x < hw; x += hw / 6)
                if ((int)((x + hw) / (hw / 6) + (s_s + z) * 2) & 1) gfx_fill_box(fb, cx + x, y, cx + x + hw / 6, y + 1, fcol(0x101010), 0);
                else gfx_fill_box(fb, cx + x, y, cx + x + hw / 6, y + 1, fcol(0xF2F2F2), 0);
        }
    }

    /* objects, far to near: one-unit depth buckets give a cheap painter's order */
    static int16_t head[40], next[MAX_OBS]; /* obstacles by one-unit depth bucket, one pass */
    for (int b = 0; b < 40; b++) head[b] = -1;
    for (int i = 0; i < s_nobs; i++) {
        float z = s_obs[i].s - s_s;
        if (z < 1 || z >= 40) continue;
        int b = (int)z;
        next[i] = head[b];
        head[b] = (int16_t)i;
    }
    int j = 0; /* scenery: from the first tree beyond 30 units (further on the treeline covers it), walking nearer */
    while (j < s_nscn && s_scn[j].s < s_s + 30) j++;
    for (int bucket = 39; bucket >= 1; bucket--) {
        s_fog = fog_at((float)bucket);
        for (; j > 0 && s_scn[j - 1].s >= s_s + bucket; j--) {
            const scn_t *t = &s_scn[j - 1];
            int y = (int)row_for(t->s - s_s);
            if (y <= Y_H || y >= VIEW_H) continue;
            float sc = s_row_sc[y], cx = s_row_cx[y] + t->x * HALF_W * sc;
            if (cx > -HALF_W * sc && cx < GFX_W + HALF_W * sc) season_tree(fb, cx, y, sc, t->color);
        }
        for (int r = 0; r < RIVALS; r++) { /* rivals share the depth buckets */
                    float rz = s_riv[r].s - s_s;
                    if (rz < bucket || rz >= bucket + 1) continue;
                    int ry = (int)row_for(rz);
                    if (ry <= Y_H || ry >= VIEW_H) continue;
                    float rsc = s_row_sc[ry];
                    float rx = s_row_cx[ry] + s_riv[r].x * HALF_W * rsc, rw = CAR_DRAW_W * HALF_W * rsc;
                    if (s_riv[r].wreck_t > 0) {
                        sprite_rot(fb, k_car, 6, rx, ry, rw, s_riv[r].color, -0.45f);
                        smoke(fb, rx, ry - rw * 0.6f, rw, 0.6f);
                    } else if (s_riv[r].mistake_t > 0 && s_riv[r].spin != 0) { /* sliding out: turns and catches it again */
                        sprite_rot(fb, k_car, 6, rx, ry, rw, s_riv[r].color, s_riv[r].spin * 0.4f * sinf(s_riv[r].mistake_t * 3.1416f));
                        smoke(fb, rx, ry, rw * 0.5f, 0.4f);
                    } else
                        sprite(fb, k_car, 6, rx, ry, rw, s_riv[r].color);
                }
        for (int i = head[bucket]; i >= 0; i = next[i]) {
            const obs_t *o = &s_obs[i];
            float z = o->s - s_s;
            if (o->hit && o->type != O_TREE && o->type != O_SIGN) continue; /* a hit tree stays standing */
            int y = (int)row_for(z);
            if (y <= Y_H || y >= VIEW_H) continue;
            float sc = s_row_sc[y], cx = s_row_cx[y] + o->x * HALF_W * sc, w = obs_halfw(o->type) * 2 * HALF_W * sc * DRAW_SCALE;
            switch (o->type) {
            case O_CONE: sprite(fb, k_cone, 6, cx, y, w, 0); break;
            case O_SIGN: sprite(fb, k_sign, 9, cx, y, w, 0); break;
            case O_BARREL: sprite(fb, k_barrel, 7, cx, y, w, 0); break;
            case O_ROCK: sprite(fb, k_rock, 5, cx, y - w * 0.35f * fabsf(sinf(o->timer)) * fminf(1, fabsf(o->vx)), w, 0); break; /* bounces while rolling */
            case O_HAY: sprite(fb, k_hay, 6, cx, y, w, 0); break;
            case O_BARRIER: sprite(fb, k_barrier, 6, cx, y, w, 0); break;
            case O_WORKS: break; /* placement marker only: build_level expands it into cones and barriers */
            case O_CAR: sprite(fb, k_car, 6, cx, y, w, o->color); break;
            case O_DUCK: sprite(fb, k_duck, 8, cx, y, w * 1.6f, 0); break;
            case O_TREE: season_tree(fb, cx, y, sc, o->color); break;
            case O_DEER: sprite_mirror(fb, k_deer, 10, cx, y, w * 1.4f, o->vx < 0); break;
            case O_WRECK:
                sprite_rot(fb, k_car, 6, cx, y, w, o->color, 0.5f);
                smoke(fb, cx, y - w * 0.6f, w, 0.6f);
                break;
            case O_OIL:
                gfx_fill_box(fb, cx - w / 2, y - fmaxf(1, w * 0.12f), cx + w / 2, y, fcol(0x151518), 0);
                gfx_fill_box(fb, cx - w / 6, y - fmaxf(1, w * 0.1f), cx, y - fmaxf(1, w * 0.05f), fcol(0x5A3A7A), 0);
                break;
            case O_PUDDLE: /* ice in winter */
                gfx_fill_box(fb, cx - w / 2, y - fmaxf(1, w * 0.12f), cx + w / 2, y, fcol(s_season == WINTER ? 0xCFE8F8 : 0x5AA8E0), 0);
                break;
            case O_BOOST:
                for (int k = 0; k < 2; k++) {
                    float yy = y - k * fmaxf(2, w * 0.18f);
                    gfx_fill_box(fb, cx - w / 2, yy - fmaxf(1, w * 0.08f), cx + w / 2, yy, fcol(0xF7D23C), 0);
                }
                break;
            }
        }
    }
    s_fog = 0;
}

/* Over the road view: rain streaks, snowflakes, or autumn leaves blowing past. */
static void draw_weather(uint16_t *fb)
{
    const gfx_clip_t view = {0, 0, GFX_W, (int)VIEW_H};
    const float t = s_clock, drift = -s_bg * 0.5f; /* the scene swings round in curves */
    if (s_weather == W_RAIN) {
        const uint16_t c = gfx_color(0xB4BECE);
        for (int i = 0; i < 70; i++) {
            float y = fmodf(h01(i, 22) * 400 + t * (260 + 90 * h01(i, 21)), VIEW_H + 20) - 10;
            float x = fmodf(h01(i, 23) * 360 + 0.25f * y + drift + 4000, 340) - 10;
            gfx_fill_box(fb, x, y, x + 1, y + 5, c, &view);
            gfx_fill_box(fb, x + 1, y + 5, x + 2, y + 10, c, &view);
        }
    } else if (s_weather == W_SNOW) {
        const uint16_t c = gfx_color(0xFFFFFF);
        for (int i = 0; i < 80; i++) {
            float y = fmodf(h01(i, 24) * 300 + t * (22 + 30 * h01(i, 25)), VIEW_H + 10) - 5;
            float x = fmodf(h01(i, 26) * 360 + 10 * sinf(t * 1.3f + i) + drift + 4000, 340) - 10, d = h01(i, 27) > 0.6f ? 3 : 2;
            gfx_fill_box(fb, x, y, x + d, y + d, c, &view);
        }
    } else if (s_season == AUTUMN) {
        static const uint32_t k_leaf[4] = {0xE8B830, 0xE07A28, 0xC8402A, 0xA86A28};
        for (int i = 0; i < 14; i++) {
            float y = fmodf(h01(i, 28) * 300 + t * (18 + 18 * h01(i, 29)), VIEW_H + 10) - 5;
            float x = fmodf(h01(i, 30) * 360 + 24 * sinf(t * 0.9f + i * 1.7f) - t * 30 + drift + 4000, 340) - 10;
            bool flat = fmodf(t * 3 + i, 2) < 1; /* tumbling */
            gfx_fill_box(fb, x, y, x + (flat ? 4 : 2), y + (flat ? 2 : 4), gfx_color(k_leaf[i % 4]), &view);
        }
    }
}

static void draw_dashboard(uint16_t *fb)
{
    const uint16_t dash = gfx_color(0x1A1A1E), edge = gfx_color(0x34343A);
    gfx_fill_box(fb, 0, VIEW_H, GFX_W, GFX_H, dash, 0);
    gfx_fill_box(fb, 0, VIEW_H, GFX_W, VIEW_H + 2, edge, 0);
    /* small steering wheel: rim, spokes, hub, and Claude's paws on the rim */
    const float a = s_wheel + (s_shake > 0 ? 0.08f * sinf(s_t * 70) : 0);
    const uint16_t rim = gfx_color(0x3A3A44), rim_hi = gfx_color(0x6A6A76);
    for (int k = 0; k < 28; k++) {
        float ang = k * 6.2832f / 28 + a;
        gfx_fill_rot_box(fb, WHEEL_CX, WHEEL_CY, ang, -4.5f, -WHEEL_R - 1, 4.5f, -WHEEL_R + 6, rim);
    }
    gfx_fill_rot_box(fb, WHEEL_CX, WHEEL_CY, a, -3, -WHEEL_R + 4, 3, 0, rim_hi);
    gfx_fill_rot_box(fb, WHEEL_CX, WHEEL_CY, a + 2.1f, -3, -WHEEL_R + 4, 3, 0, rim_hi);
    gfx_fill_rot_box(fb, WHEEL_CX, WHEEL_CY, a - 2.1f, -3, -WHEEL_R + 4, 3, 0, rim_hi);
    gfx_fill_rot_box(fb, WHEEL_CX, WHEEL_CY, a, -7, -7, 7, 7, gfx_color(0xC0503A));
    const uint16_t paw = gfx_color(MASCOT_RGB), paw_edge = gfx_color(0xA8503A);
    for (int side = -1; side <= 1; side += 2) {
        float ang = a + side * 0.95f;
        gfx_fill_rot_box(fb, WHEEL_CX, WHEEL_CY, ang, -8, -WHEEL_R - 6, 8, -WHEEL_R + 8, paw_edge);
        gfx_fill_rot_box(fb, WHEEL_CX, WHEEL_CY, ang, -7, -WHEEL_R - 5, 7, -WHEEL_R + 7, paw);
    }
    /* hearts and speed */
    for (int i = 0; i < s_lives; i++) draw_heart(fb, 8 + i * 18, VIEW_H + 9, 2, gfx_color(0xE8506E));
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", (int)(s_v * 6));
    font_draw(fb, GFX_W - 8 - font_width(buf, 3), VIEW_H + 7, 3, gfx_color(s_boost > 0 ? 0xF7D23C : 0xFFFFFF), buf);
}

/* Countdown: the new track's map, drawn from start to finish in the first 1.5 s */
static void draw_map(uint16_t *fb)
{
    const float X0 = 14, Y0 = 44, W = 212, H = 118; /* room above for the finish flag */
    gfx_clear(fb, gfx_color(k_looks[s_season].map_bg));
    float lo_x = 1e9f, hi_x = -1e9f, lo_y = 1e9f, hi_y = -1e9f;
    for (int i = 0; i < MAP_PTS; i++) {
        lo_x = fminf(lo_x, s_map_x[i]), hi_x = fmaxf(hi_x, s_map_x[i]);
        lo_y = fminf(lo_y, s_map_y[i]), hi_y = fmaxf(hi_y, s_map_y[i]);
    }
    float k = fminf(W / fmaxf(1, hi_x - lo_x), H / fmaxf(1, hi_y - lo_y));
    float ox = X0 + (W - (hi_x - lo_x) * k) / 2 - lo_x * k, oy = Y0 + (H - (hi_y - lo_y) * k) / 2 - lo_y * k;
    int n = (int)(MAP_PTS * clampf((3 - s_count) / 1.5f, 0, 1));
    const uint16_t road = gfx_color(0x8A8A94), line = gfx_color(0xF2F2F2);
    for (int pass = 0; pass < 2; pass++) /* grey road, then a thin white centre line on top */
        for (int i = 1; i < n; i++) {
            float x0 = ox + s_map_x[i - 1] * k, y0 = oy + s_map_y[i - 1] * k, x1 = ox + s_map_x[i] * k, y1 = oy + s_map_y[i] * k;
            float len = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)), hw = pass ? 0.5f : 2.5f;
            gfx_fill_rot_box(fb, x0, y0, atan2f(y1 - y0, x1 - x0), -hw, -hw, len + hw, hw, pass ? line : road);
        }
    float sx = ox + s_map_x[0] * k, sy = oy + s_map_y[0] * k; /* the grid: us and the rivals */
    gfx_fill_box(fb, sx - 7, sy - 7, sx - 2, sy - 2, gfx_color(0xD8302A), 0);
    for (int r = 0; r < RIVALS; r++) gfx_fill_box(fb, sx - 1 + r * 4, sy - 9 + (r & 1) * 3, sx + 2 + r * 4, sy - 6 + (r & 1) * 3, gfx_color(s_riv[r].color), 0);
    if (n >= MAP_PTS) { /* chequered flag at the finish */
        float fx = ox + s_map_x[MAP_PTS - 1] * k, fy = oy + s_map_y[MAP_PTS - 1] * k;
        gfx_fill_box(fb, fx, fy - 16, fx + 1, fy, gfx_color(0xF2F2F2), 0);
        for (int cy = 0; cy < 2; cy++)
            for (int cx = 0; cx < 3; cx++)
                gfx_fill_box(fb, fx + 1 + cx * 3, fy - 16 + cy * 3, fx + 4 + cx * 3, fy - 13 + cy * 3, gfx_color((cx + cy) & 1 ? 0x101010 : 0xF2F2F2), 0);
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "%s  %s %s", k_levels[s_level].name, k_looks[s_season].name, k_weather_name[s_weather]);
    font_draw(fb, 12, 6, 2, gfx_color(0xFFFFFF), buf);
    snprintf(buf, sizeof(buf), "%d TURNS", s_turns);
    font_draw(fb, 12, 22, 2, gfx_color(0xB0B0B0), buf);
    snprintf(buf, sizeof(buf), "%d", (int)ceilf(s_count));
    font_draw_centered(fb, 276, 60, 8, gfx_color(0xFFFFFF), buf);
}

static void draw(uint16_t *fb, uint32_t now)
{
    char buf[32];
    const uint16_t white = gfx_color(0xFFFFFF), gray = gfx_color(0x909090);
    if (s_state == SELECT) {
        gfx_clear(fb, GFX_HEX(0x000000));
        font_draw_centered(fb, GFX_W / 2, 10, 3, white, "RACE");
        font_draw_centered(fb, GFX_W / 2, 30, 1 + 1, gray, "TILT TO STEER. HOLD TO BRAKE");
        static const uint32_t tile[3] = {0x2E6A3A, 0x7A4A2A, 0x2A2E5A};
        for (int i = 0; i < 3; i++) {
            int x = 8 + i * 102;
            gfx_fill_rect(fb, x, 46, 98, 104, gfx_color(tile[i]));
            snprintf(buf, sizeof(buf), "%d", i + 1);
            font_draw_centered(fb, x + 49, 58, 6, white, buf);
            font_draw_centered(fb, x + 49, 100, 2, white, k_levels[i].name);
            if (s_best[i]) {
                snprintf(buf, sizeof(buf), "%d.%d S", (int)s_best[i] / 10, (int)s_best[i] % 10);
                font_draw_centered(fb, x + 49, 126, 2, gfx_color(0xF7D23C), buf);
            }
        }
        draw_dashboard(fb);
        return;
    }
    float shake_x = s_shake > 0 ? 4 * sinf(s_t * 60) : 0;
    draw_world(fb, shake_x);
    draw_weather(fb);
    if (s_state == VICTORY_LAP || (s_state == FINISHED && s_vh > 150)) { /* third person: our car slides away at 3/4 */
        float k = clampf(s_win_t / WIN_DRIVE_S, 0, 1), z = 2.6f + 24 * k * k;
        int y = (int)row_for(z);
        if (k < 1 && y > Y_H && y < VIEW_H) { /* still driving off, also under the result banner */
            float sc = s_row_sc[y], slip = 0.15f * sinf(3.1416f * clampf(k / 0.4f, 0, 1)); /* a short slide out, mid-road from then on */
            float cx = s_row_cx[y] + slip * HALF_W * sc, w = 0.9f * HALF_W * sc * 1.6f;
            float yaw = 0.25f + 0.6f * ease_out2(clampf(k * 3, 0, 1)); /* turns from straight behind to three quarters */
            car_view_t v = {0, (float)y, w, cosf(yaw), sinf(yaw)};
            float lo = 1e9f, hi = -1e9f; /* centre the projected footprint on the road, whatever the yaw */
            for (int c = 0; c < 4; c++) {
                float px, py;
                car_pt(&v, (float)(c & 1), (c & 2) ? CAR_W : 0, 0, &px, &py);
                lo = fminf(lo, px), hi = fmaxf(hi, px);
            }
            v.ox = cx - (lo + hi) / 2;
            float x0, y0, x1, y1, dens = 0.75f * (1 - k / 0.75f); /* tyre smoke from both rear wheels */
            car_pt(&v, 0.2f, -0.05f, 0, &x0, &y0);
            car_pt(&v, 0.1f, CAR_W, 0, &x1, &y1);
            if (k < 0.75f) smoke(fb, x1 - w * 0.12f, y1, w * 0.4f, dens); /* far wheel: the cloud trails behind the car */
            draw_car34(fb, &v);
            if (k < 0.75f) smoke(fb, x0 - w * 0.04f, y0 + w * 0.04f, w * 0.18f, dens); /* near wheel: a small puff at the tyre */
            for (int r = 0; r < RIVALS; r++) { /* rivals nearer to the camera than our car go on top */
                float rz = s_riv[r].s - s_s;
                if (rz <= 1 || rz >= z) continue;
                int ry = (int)row_for(rz);
                if (ry <= Y_H || ry >= VIEW_H) continue;
                float rsc = s_row_sc[ry];
                sprite(fb, k_car, 6, s_row_cx[ry] + s_riv[r].x * HALF_W * rsc, ry, CAR_DRAW_W * HALF_W * rsc, s_riv[r].color);
            }
        }
        if (s_state == VICTORY_LAP) return;
    }
    if (s_state != VICTORY_LAP && !(s_state == FINISHED && s_vh > 150)) {
        int damage = LIVES - s_lives;
        if (damage > 0 && s_state != SELECT) smoke(fb, 160 + 30 * sinf(s_t * 0.5f), VIEW_H - 2, 34 + 10 * damage, 0.18f * damage); /* from the bonnet */
        if (s_state == RACE && s_invuln > 1.0f && fmodf(s_invuln, 0.2f) < 0.1f) gfx_fill_box_dither_sparse(fb, 0, 0, GFX_W, VIEW_H, gfx_color(0xFF4040));
        cracks(fb);
        if (s_state == CRASHED_OUT) shatter(fb);
        draw_dashboard(fb);
    }
    if (s_state == COUNTDOWN && s_count >= 0.3f) {
        draw_map(fb);
        return;
    }
    const level_t *L = &k_levels[s_level];
    gfx_fill_box(fb, 0, 0, GFX_W * clampf(s_s / L->length, 0, 1), 3, gfx_color(0xF7D23C), 0); /* progress */
    snprintf(buf, sizeof(buf), "%d.%d", (int)s_t, (int)(s_t * 10) % 10);
    font_draw(fb, GFX_W - 6 - font_width(buf, 2), 8, 2, white, buf);
    snprintf(buf, sizeof(buf), "POS %d/%d", s_state == FINISHED ? s_place : position(), RIVALS + 1);
    font_draw(fb, 6, 8, 2, white, buf);
    if (s_state == COUNTDOWN) {
        font_draw_centered(fb, GFX_W / 2, 50, 8, white, "GO"); /* the map shows the count before this */
    }
    if (s_state == RACE && s_t < 0.8f) font_draw_centered(fb, GFX_W / 2, 50, 8, white, "GO");
    if (s_state == FINISHED) { /* the rivals arrive behind this banner */
        for (int i = 0; i < CONFETTI; i++) /* confetti falls from the sky */
            gfx_fill_box(fb, s_cf[i][0], s_cf[i][1], s_cf[i][0] + 4, s_cf[i][1] + 4, gfx_color(k_cf_colors[i % 5]), 0);
        gfx_fill_box_dither(fb, 0, 26, GFX_W, 104, gfx_color(0x000000)); /* keep the text readable */
        static const char *k_place[4] = {"1ST PLACE!", "2ND PLACE", "3RD PLACE", "4TH PLACE"};
        font_draw_centered(fb, GFX_W / 2, 34, 4, white, k_place[(int)clampf(s_place, 1, 4) - 1]);
        snprintf(buf, sizeof(buf), "TIME %d.%d S", (int)s_t, (int)(s_t * 10) % 10);
        font_draw_centered(fb, GFX_W / 2, 66, 2, white, buf);
        if (now - s_end_at > 800) font_draw_centered(fb, GFX_W / 2, 92, 2, gray, s_level < 2 ? "TAP: NEXT LEVEL" : "TAP: LEVELS");
    }
    if (s_state == CRASHED_OUT) {
        gfx_fill_box_dither(fb, 0, 32, GFX_W, 96, gfx_color(0x000000));
        font_draw_centered(fb, GFX_W / 2, 40, 4, white, "CRASHED!");
        if (now - s_end_at > 800) font_draw_centered(fb, GFX_W / 2, 80, 2, white, "TAP TO RETRY");
    }
}

static bool menu_locked(void) { return s_state == COUNTDOWN || s_state == RACE; }

const scene_t scene_race = {enter, update, draw, menu_locked};
