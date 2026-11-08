#pragma once
#include <stdbool.h>

#include "input.h"

/* "Earthquake": the mascot is thrown around the screen by the board's jolts, then falls and settles.
 * x is the body centre (px); y is the height above the floor (0 on the floor, negative = up). */
typedef struct {
    float x, y, vx, vy, kick_in;
} toss_t;

void toss_start(toss_t *t, float x);
void toss_update(toss_t *t, const input_t *in, float dt, float x_min, float x_max, float y_min);
bool toss_settled(const toss_t *t);
