/* Host harness: drives the real app with scripted touches and saves contact sheets.
 *   ./scenes <out_dir> [name filter]
 * FRAMES=from:to:step (ms) also writes every step-th frame in that window as <out_dir>/<name>_NNNNN.ppm (for GIFs). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "gfx.h"

#define COLS 4
#define ROWS 3
#define GAP 4
#define SHEET_W (COLS * GFX_W + (COLS + 1) * GAP)
#define SHEET_H (ROWS * GFX_H + (ROWS + 1) * GAP)

typedef struct {
    uint32_t from, to;
    int x0, y0, x1, y1; /* finger moves linearly while down */
    uint32_t still;     /* ms the finger rests at (x0, y0) before it starts moving */
} touch_t;

typedef struct {
    const char *name;
    touch_t touches[16];
    uint32_t shots[COLS * ROWS];
    uint32_t shake_from, shake_to; /* synthetic accelerometer shaking */
    uint32_t tilt_from, tilt_to;   /* synthetic tilt: right edge down ~45 degrees */
    uint32_t boot[4];              /* BOOT button presses (held 100 ms) */
} session_t;

static uint8_t sheet[SHEET_H][SHEET_W][3];
static uint16_t fb[GFX_W * GFX_H];

static struct { char key[16]; int32_t v; } store[8];
static int32_t load(const char *k, int32_t d)
{
    for (int i = 0; i < 8; i++) if (!strcmp(store[i].key, k)) return store[i].v;
    return d;
}
static void save(const char *k, int32_t v)
{
    for (int i = 0; i < 8; i++)
        if (!store[i].key[0] || !strcmp(store[i].key, k)) { snprintf(store[i].key, 16, "%s", k); store[i].v = v; return; }
}
static const app_storage_t k_store = {load, save};

static void blit(int cell)
{
    int col = cell % COLS, row = cell / COLS;
    for (int y = 0; y < GFX_H; y++)
        for (int x = 0; x < GFX_W; x++) {
            uint16_t c = GFX_SWAP16(fb[y * GFX_W + x]);
            uint8_t *px = sheet[GAP + row * (GFX_H + GAP) + y][GAP + col * (GFX_W + GAP) + x];
            px[0] = ((c >> 11) & 0x1F) * 255 / 31;
            px[1] = ((c >> 5) & 0x3F) * 255 / 63;
            px[2] = (c & 0x1F) * 255 / 31;
        }
}

static void write_frame(const char *dir, const char *name, int n)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s_%05d.ppm", dir, name, n);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", GFX_W, GFX_H);
    for (int i = 0; i < GFX_W * GFX_H; i++) {
        uint16_t c = GFX_SWAP16(fb[i]);
        uint8_t px[3] = {(uint8_t)(((c >> 11) & 0x1F) * 255 / 31), (uint8_t)(((c >> 5) & 0x3F) * 255 / 63), (uint8_t)((c & 0x1F) * 255 / 31)};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

static void write_bmp(const char *path)
{
    FILE *f = fopen(path, "wb");
    int stride = (SHEET_W * 3 + 3) & ~3, size = 54 + stride * SHEET_H, w = SHEET_W, h = SHEET_H;
    uint8_t hd[54] = {'B', 'M'};
    memcpy(hd + 2, &size, 4);
    hd[10] = 54; hd[14] = 40; hd[26] = 1; hd[28] = 24;
    memcpy(hd + 18, &w, 4);
    memcpy(hd + 22, &h, 4);
    fwrite(hd, 1, 54, f);
    static uint8_t row[SHEET_W * 3 + 4];
    for (int y = SHEET_H - 1; y >= 0; y--) {
        for (int x = 0; x < SHEET_W; x++) {
            row[x * 3] = sheet[y][x][2]; row[x * 3 + 1] = sheet[y][x][1]; row[x * 3 + 2] = sheet[y][x][0];
        }
        fwrite(row, 1, stride, f);
    }
    fclose(f);
}

#define LONG(t) {t, t + 1400, 160, 86, 160, 86}
#define TAP(t, x, y) {t, t + 90, x, y, x, y}
static const session_t k_sessions[] = {
    {"menu", {LONG(500)}, {400, 1800, 2200, 3000}},
    {"show", {{0, 0, 0, 0, 0, 0}}, {6000, 12000, 18000, 24000, 30000, 36000, 42000, 48000, 54000, 60000, 66000, 72000}},
    {"act", {{0, 0, 0, 0, 0, 0}}, {2500, 3500, 4300, 5500, 6400, 7400, 8200, 9300, 10300, 11500, 12800, 14000}},
    {"walk", {{0, 0, 0, 0, 0, 0}}, {3300, 4300, 5600, 7000, 10000, 15000, 20000, 23500, 24600, 25300, 26000, 27200}},
    {"menu2", {LONG(500), {2200, 2600, 260, 80, 60, 80}}, {2000, 2350, 2500, 2700, 3000, 3400}},
    {"race", {LONG(500), {2200, 2600, 260, 80, 60, 80}, TAP(3200, 240, 120), TAP(4000, 58, 100)},
     {3600, 6500, 10800, 11300, 11700, 12000, 35500, 36300, 37000, 43800, 44600, 45400}, 0, 0, 9000, 12000},
    {"race3", {LONG(500), {2200, 2600, 260, 80, 60, 80}, TAP(3200, 240, 120), TAP(4000, 262, 100)},
     {7400, 9400, 11400, 12600, 12800, 13000, 13200, 13400, 13600, 15400, 16400, 18400}, 0, 0, 8000, 9500},
    {"racefin", {LONG(500), {2200, 2600, 260, 80, 60, 80}, TAP(3200, 240, 120), TAP(4000, 58, 100)},
     {49300, 49700, 50100, 50400, 50700, 51000, 51400, 51800, 52300, 52900, 53600, 55000}},
    {"racecrash", {LONG(500), {2200, 2600, 260, 80, 60, 80}, TAP(3200, 240, 120), TAP(4000, 160, 100)},
     {8300, 8450, 8600, 8750, 8900, 9050, 9200, 9350, 9500, 9650, 9800, 10000}},
    /* four races in a row (EASY, EASY, MEDIUM, HARD), back to the menu with BOOT: three countdown frames each */
    {"racemaps", {LONG(500), {2200, 2600, 260, 80, 60, 80}, TAP(3200, 240, 120), TAP(4000, 58, 100), TAP(8300, 240, 120), TAP(9100, 58, 100),
                  TAP(13400, 240, 120), TAP(14200, 160, 100), TAP(18500, 240, 120), TAP(19300, 262, 100)},
     {4500, 5700, 6600, 9600, 10800, 11700, 14700, 15900, 16800, 19800, 21000, 21900}, 0, 0, 0, 0, {7500, 12600, 17700}},
    /* one MEDIUM race: the map, then the road; run with RACE_SEASON=summer|autumn|winter RACE_WEATHER=clear|rain|fog|snow RACE_AUTOPILOT=1 */
    {"raceseason", {LONG(500), {2200, 2600, 260, 80, 60, 80}, TAP(3200, 240, 120), TAP(4000, 160, 100)},
     {5600, 8000, 9500, 11000, 12500, 14000, 15500, 17000, 18500, 20000, 21500, 23000}},
    {"raceoffroad", {LONG(500), {2200, 2600, 260, 80, 60, 80}, TAP(3200, 240, 120), TAP(4000, 58, 100)},
     {7600, 8600, 9400, 35200, 35600, 36000, 36400, 36800, 37200, 37800, 38600, 39400}, 0, 0, 8000, 38000},
    {"racerocks", {LONG(500), {2200, 2600, 260, 80, 60, 80}, TAP(3200, 240, 120), TAP(4000, 262, 100)},
     {11200, 11500, 11800, 12100, 12300, 12500, 12700, 12900, 13100, 13300, 13500, 13800}},
    {"race2", {LONG(500), {2200, 2600, 260, 80, 60, 80}, TAP(3200, 240, 120), TAP(4000, 160, 100)},
     {18200, 18800, 19400, 19900, 20300, 38600, 39200, 39800, 40300, 40700, 50000, 54000}},
    {"pool", {LONG(500), {2200, 2600, 260, 80, 60, 80}, TAP(3200, 80, 120), TAP(9000, 160, 100)}, {3000, 4500, 5500, 6200, 7000, 8200, 9300, 9800, 11000, 13000, 15000, 17000}, 12500, 13500, 4500, 8500},
    {"dizzy", {{0, 0, 0, 0, 0, 0}}, {4200, 4500, 4800, 5100, 5400, 5700, 6000, 6400, 6900, 7600, 8400, 9500}, 4000, 6000},
    {"petshake", {LONG(500), TAP(2500, 240, 120)}, {3900, 4300, 4700, 5100, 5500, 6200, 6800, 7600, 8400, 9200, 9800, 10400}, 3800, 5600},
    {"wake", {{0, 0, 0, 0, 0, 0}}, {7000, 9900, 10100, 10300, 10500, 10700, 11200, 12000, 12800, 13600, 14200, 15000}, 10000, 10900},
    {"showlong", {{0, 0, 0, 0, 0, 0}}, {300000, 600000, 900000, 1200000, 1500000, 1800000, 2100000, 2400000, 2700000, 3000000, 3300000, 3600000}}, /* act mix over 60 min */
    {"parkball", {{0, 0, 0, 0, 0, 0}}, {9000, 9700, 10200, 10700, 11300, 11900, 12600, 13300, 14000, 15000, 16200, 17500}, 9000, 10200}, /* SHOW_FORCE=park SHOW_DAY=1 */
    {"parkstorm", {{0, 0, 0, 0, 0, 0}}, {9000, 9900, 12500, 15600, 17500, 18900, 19200, 19500, 19800, 20100, 20400, 21000}, 9000, 10200}, /* SHOW_FORCE=park */
    {"flower", {TAP(3000, 262, 80)}, {2900, 3300, 3700, 4200, 4800, 5400, 6000, 6600, 7200, 7800, 8600, 10000}},
    {"wiltsleep", {TAP(12000, 262, 80)}, {11000, 12100, 13000, 15000, 17000, 19000, 21000, 23000, 25000, 27000, 29000, 31000}}, /* SHOW_FORCE=sleep */
    {"sleep", {TAP(16000, 160, 100)}, {3000, 4200, 5000, 5600, 6400, 9000, 10500, 16700, 17300, 17800, 18700, 19400}},
    {"runner",
     {LONG(500), TAP(2500, 160, 43), TAP(3500, 160, 100), TAP(5200, 160, 100), TAP(6900, 160, 100)},
     {2400, 3000, 3700, 3850, 4100, 5000, 5400, 6500, 9000, 12000, 15000, 20000}},
    {"catch",
     {LONG(500), TAP(2500, 80, 120), TAP(3500, 160, 100), {4000, 9000, 160, 120, 40, 120}, {9500, 14000, 40, 120, 290, 120}},
     {3000, 3800, 5000, 6500, 8000, 10000, 12000, 13500, 30000, 49000, 50000, 51000}},
    {"pet",
     {LONG(500), TAP(2500, 240, 120), TAP(3500, 160, 120), TAP(5000, 160, 120), TAP(7000, 90, 40), TAP(8000, 160, 120),
      TAP(11000, 160, 120), TAP(14000, 300, 160)},
     {3000, 3600, 3800, 4200, 5300, 6000, 7300, 8300, 9000, 11500, 12500, 14300}},
    /* PET: head, left hand, feet, body, then three quick taps (tickling) */
    {"petgest",
     {LONG(500), TAP(2500, 240, 120), TAP(3500, 160, 92), TAP(5000, 116, 116), TAP(7000, 144, 160), TAP(9000, 160, 125),
      TAP(12000, 160, 120), TAP(12300, 165, 120), TAP(12600, 160, 125), TAP(12900, 165, 120)},
     {3700, 3900, 5300, 5600, 7200, 7500, 9300, 10000, 12700, 13000, 13300, 13800}},
    {"petstroke",
     {LONG(500), TAP(2500, 240, 120), {3500, 4100, 120, 100, 200, 100}, {4300, 4900, 200, 110, 120, 110},
      {5100, 5700, 120, 100, 200, 100}, {5900, 6500, 200, 110, 120, 110}},
     {3700, 3900, 4200, 4500, 4700, 5300, 5600, 6000, 6300, 6600, 7000, 7500}},
    /* hold Claude, lift it up and fling it to the right */
    {"petgrab",
     {LONG(500), TAP(2500, 240, 120), {3500, 4300, 160, 120, 250, 50, 600}},
     {3800, 4100, 4200, 4350, 4500, 4700, 5000, 5400, 6000, 7000, 8600, 9400}},
    /* shaken (ruffled), then the comb along the head and both sides */
    {"petcomb",
     {LONG(500), TAP(2500, 240, 120), TAP(10500, 21, 107), {11200, 12200, 108, 96, 215, 96}, {12400, 13200, 117, 86, 117, 150},
      {13400, 14200, 203, 86, 203, 150}},
     {9000, 10700, 11400, 11700, 12000, 12600, 13000, 13600, 14000, 14300, 14700, 15200}, 3800, 5600},
    {"petcoffee",
     {LONG(500), TAP(2500, 240, 120), TAP(3500, 21, 143), TAP(4500, 100, 80), TAP(7000, 186, 75), TAP(9500, 272, 85)},
     {4000, 5200, 5800, 6400, 8300, 9000, 10800, 12000, 12800, 13150, 13650, 15500}},
    {"tower",
     {LONG(500), {2200, 2600, 260, 80, 60, 80}, TAP(3200, 80, 50), TAP(4500, 160, 100), TAP(5800, 160, 100), TAP(7100, 160, 100),
      TAP(8300, 160, 100), TAP(9400, 160, 100), TAP(10600, 160, 100), TAP(11500, 160, 100), TAP(12300, 160, 100)},
     {4000, 4700, 5900, 6100, 7200, 8400, 9500, 10700, 11600, 12400, 13500, 14500}},
    {"pong",
     {LONG(500), {2200, 2600, 260, 80, 60, 80}, TAP(3200, 240, 50), TAP(4500, 160, 100), {5000, 10000, 30, 30, 30, 150},
      {10000, 15000, 30, 150, 30, 20}, {15000, 23000, 30, 20, 30, 160}, {23000, 31000, 30, 160, 30, 60}},
     {4000, 4700, 5500, 6500, 8000, 10000, 12000, 15000, 19000, 23000, 27000, 31000}},
};

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    for (unsigned si = 0; si < sizeof(k_sessions) / sizeof(k_sessions[0]); si++) {
        const session_t *s = &k_sessions[si];
        if (argc > 2 && !strstr(s->name, argv[2])) continue;
        memset(store, 0, sizeof(store));
        memset(sheet, 40, sizeof(sheet));
        app_init(12345 + si, &k_store, 0);
        printf("session %s\n", s->name);
        int shot = 0, last_shot = 0, nframe = 0;
        for (int i = 0; i < COLS * ROWS && s->shots[i]; i++) last_shot = (int)s->shots[i];
        unsigned f_from = 0, f_to = 0, f_step = 0, f_next = 0;
        if (getenv("FRAMES") && sscanf(getenv("FRAMES"), "%u:%u:%u", &f_from, &f_to, &f_step) == 3) f_next = f_from;
        for (uint32_t t = 0; t <= (uint32_t)last_shot; t += 33) {
            bool down = false;
            int x = 0, y = 0;
            for (int k = 0; k < 16 && s->touches[k].to; k++) {
                const touch_t *tc = &s->touches[k];
                if (t >= tc->from && t <= tc->to) {
                    float p = t < tc->from + tc->still ? 0 : (float)(t - tc->from - tc->still) / (tc->to - tc->from - tc->still);
                    down = true;
                    x = tc->x0 + (int)((tc->x1 - tc->x0) * p);
                    y = tc->y0 + (int)((tc->y1 - tc->y0) * p);
                }
            }
            /* IMU frame: upright reads +X (see input.c); right edge down reads (0.7, -0.7, 0) */
            float ax = 1, ay = 0, az = 0;
            if (t >= s->tilt_from && t < s->tilt_to) { ax = 0.71f; ay = -0.71f; }
            if (t >= s->shake_from && t < s->shake_to) {
                ax += 2.0f * sinf(t * 0.0377f); /* ~6 Hz */
                ay += 1.2f * cosf(t * 0.0377f);
            }
            bool boot = false;
            for (int k = 0; k < 4 && s->boot[k]; k++) boot |= t >= s->boot[k] && t < s->boot[k] + 100;
            app_frame(fb, down, x, y, true, ax, ay, az, boot, t);
            if (f_step && t >= f_next && t <= f_to) {
                write_frame(out, s->name, nframe++);
                f_next += f_step;
            }
            while (shot < COLS * ROWS && s->shots[shot] && t + 33 > s->shots[shot]) blit(shot++);
        }
        char path[512];
        snprintf(path, sizeof(path), "%s/scene_%s.bmp", out, s->name);
        write_bmp(path);
        printf("%s: %d shots -> %s\n", s->name, shot, path);
    }
    return 0;
}
