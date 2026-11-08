#pragma once
#include <stdbool.h>
#include <stdint.h>

#define INPUT_TAP_MAX_MS 400
#define INPUT_LONG_PRESS_MS 1200
#define INPUT_SLOP_PX 24

/* Gesture state for one frame. Release is debounced because the touch panel drops frames. */
typedef struct {
    bool down;       /* finger currently on the screen (debounced) */
    bool pressed;    /* went down this frame */
    bool tap;        /* short touch ended this frame */
    bool long_press; /* held long enough this frame (fires once) */
    bool moved;      /* the current/last touch moved beyond the slop (a drag, not a tap) */
    int swipe;       /* -1 left / +1 right: a horizontal drag released this frame */
    int x, y;        /* current (or last) position */
    int start_x, start_y;
    /* motion (accelerometer) */
    bool shake;        /* a shake gesture was recognised this frame */
    float shake_level; /* current shaking strength, g (decays quickly) */
    float jx, jy, jz;  /* instantaneous jolt (gravity removed), screen axes (x right, y down, z out), g */
    float gx, gy;      /* smoothed gravity in the screen plane (x right, y down), g */
    float gix, giy;    /* instantaneous gravity incl. jolts, screen plane, g */
} input_t;

void input_reset(input_t *in);
void input_motion(input_t *in, bool valid, float ax, float ay, float az, uint32_t now_ms, float dt);

void input_update(input_t *in, bool raw_down, int raw_x, int raw_y, uint32_t now_ms);
