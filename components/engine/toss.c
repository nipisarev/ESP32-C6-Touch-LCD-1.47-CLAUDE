#include "toss.h"

#include <math.h>

#include "scenes.h"

#define PUSH 5000.0f   /* px/s^2 per g of jolt (inertia: the mascot lags behind the board) */
#define GRAVITY 700.0f
#define KICK 520.0f    /* random kicks keep the motion lively whatever the IMU axes are */

void toss_start(toss_t *t, float x)
{
    t->x = x;
    t->y = t->vx = t->vy = t->kick_in = 0;
}

void toss_update(toss_t *t, const input_t *in, float dt, float x_min, float x_max, float y_min)
{
    /* Push with the full jolt (any axis); direction from the screen-plane part, random if it is mostly z. */
    float jolt = sqrtf(in->jx * in->jx + in->jy * in->jy + in->jz * in->jz), plane = sqrtf(in->jx * in->jx + in->jy * in->jy);
    float dx, dy;
    if (plane > 0.3f * jolt && plane > 0.05f) {
        dx = -in->jx / plane;
        dy = -in->jy / plane;
    } else {
        float a = rng_float(0, 6.2832f);
        dx = cosf(a);
        dy = sinf(a);
    }
    t->vx += dx * jolt * PUSH * dt;
    t->vy += (dy * jolt * PUSH + GRAVITY) * dt;
    if ((t->kick_in -= dt) <= 0 && in->shake_level > 1.0f) {
        float a = rng_float(0, 6.2832f), k = KICK * fminf(in->shake_level, 3);
        t->vx += k * cosf(a);
        t->vy -= k * fabsf(sinf(a)); /* kicks go mostly upwards */
        t->kick_in = 0.1f;
    }
    float damp = expf(-0.6f * dt);
    t->vx *= damp;
    t->vy *= damp;
    t->x += t->vx * dt;
    t->y += t->vy * dt;
    if (t->x < x_min) { t->x = x_min; t->vx = fabsf(t->vx) * 0.8f; }
    if (t->x > x_max) { t->x = x_max; t->vx = -fabsf(t->vx) * 0.8f; }
    if (t->y < y_min) { t->y = y_min; t->vy = fabsf(t->vy) * 0.7f; }
    if (t->y >= 0) { /* floor: bounce, then friction */
        t->y = 0;
        t->vy = t->vy > 40 ? -t->vy * 0.6f : 0;
        t->vx *= expf(-5 * dt);
    }
}

bool toss_settled(const toss_t *t) { return t->y >= 0 && t->vy == 0 && fabsf(t->vx) < 25; }
