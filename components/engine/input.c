#include "input.h"

#include <math.h>
#include <stdlib.h>

#define RELEASE_FRAMES 2

static uint32_t s_down_at;
static int s_miss;
static bool s_long_fired, s_moved;

void input_update(input_t *in, bool raw_down, int raw_x, int raw_y, uint32_t now_ms)
{
    in->pressed = in->tap = in->long_press = false;
    in->swipe = 0;
    if (raw_down) {
        s_miss = 0;
        if (!in->down) {
            in->down = in->pressed = true;
            in->start_x = raw_x;
            in->start_y = raw_y;
            s_down_at = now_ms;
            s_long_fired = s_moved = false;
        }
        in->x = raw_x;
        in->y = raw_y;
        if (abs(raw_x - in->start_x) > INPUT_SLOP_PX || abs(raw_y - in->start_y) > INPUT_SLOP_PX) s_moved = true;
        in->moved = s_moved;
        if (!s_long_fired && !s_moved && now_ms - s_down_at >= INPUT_LONG_PRESS_MS) {
            in->long_press = true;
            s_long_fired = true;
        }
        return;
    }
    if (in->down && ++s_miss >= RELEASE_FRAMES) {
        in->down = false;
        if (!s_long_fired && !s_moved && now_ms - s_down_at <= INPUT_TAP_MAX_MS + RELEASE_FRAMES * 40) in->tap = true;
        int dx = in->x - in->start_x, dy = in->y - in->start_y;
        if (s_moved && abs(dx) > 50 && abs(dx) > 2 * abs(dy) && now_ms - s_down_at < 1200) in->swipe = dx > 0 ? 1 : -1;
    }
}

/* Shake = >= 3 jolts above SHAKE_G within SHAKE_WINDOW_MS, then a cooldown. */
#define SHAKE_G 0.9f
#define SHAKE_WINDOW_MS 800
#define SHAKE_COOLDOWN_MS 2000
#define PEAK_GAP_MS 80

static float s_gx, s_gy, s_gz = 1;
static bool s_g_init;
static uint32_t s_peaks[8], s_cool_until, s_last_peak;
static int s_npeaks;

void input_motion(input_t *in, bool valid, float ax, float ay, float az, uint32_t now_ms, float dt)
{
    in->shake = false;
    if (!valid) {
        in->shake_level = in->jx = in->jy = in->jz = in->gix = in->gx = 0;
        in->gy = in->giy = 1; /* no IMU: gravity straight down */
        return;
    }
    if (!s_g_init) {
        s_gx = ax, s_gy = ay, s_gz = az;
        s_g_init = true;
    }
    const float k = fminf(1, dt * 2.5f); /* slow gravity estimate */
    s_gx += (ax - s_gx) * k;
    s_gy += (ay - s_gy) * k;
    s_gz += (az - s_gz) * k;
    float dx = ax - s_gx, dy = ay - s_gy, dz = az - s_gz, jolt = sqrtf(dx * dx + dy * dy + dz * dz);
    /* IMU -> screen, calibrated on the board: +X = screen up, +Y = screen right, -Z = out of the screen.
     * The accelerometer reads the "up" vector at rest, so gravity on screen is (-aY, aX). */
    in->jx = dy;
    in->jy = -dx;
    in->jz = -dz;
    in->gx = -s_gy;
    in->gy = s_gx;
    in->gix = -ay;
    in->giy = ax;
    in->shake_level = fmaxf(jolt, in->shake_level * expf(-dt * 6));
    if (jolt > SHAKE_G && now_ms - s_last_peak > PEAK_GAP_MS) {
        s_last_peak = now_ms;
        s_peaks[s_npeaks++ % 8] = now_ms;
        int recent = 0;
        for (int i = 0; i < 8 && i < s_npeaks; i++)
            if (now_ms - s_peaks[i] <= SHAKE_WINDOW_MS) recent++;
        if (recent >= 3 && now_ms >= s_cool_until) {
            in->shake = true;
            s_cool_until = now_ms + SHAKE_COOLDOWN_MS;
        }
    }
}

void input_reset(input_t *in)
{
    *in = (input_t){0};
    s_down_at = s_last_peak = s_cool_until = 0;
    s_miss = s_npeaks = 0;
    s_long_fired = s_moved = s_g_init = false;
}
