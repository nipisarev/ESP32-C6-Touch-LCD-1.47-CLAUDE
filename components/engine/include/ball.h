#pragma once
#include <stdint.h>

/* Inflatable see-through ball printed with the Tiden logo on both sides. */
#define BALL_R 17

/* Centre (cx, cy); roll spins it in the screen plane (rolling sideways), tumble about the horizontal axis
 * (the logo slides over the top and the other side comes round). Radians. */
void ball_draw(uint16_t *fb, float cx, float cy, float roll, float tumble);
