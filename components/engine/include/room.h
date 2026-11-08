#pragma once
#include <stdint.h>

/* Home: black wall, wooden floor, a door on the left and a window on the right. */
#define FLOOR_Y 150.0f
#define DOOR_X0 6.0f
#define DOOR_X1 46.0f
#define DOOR_Y0 76.0f
#define DOOR_CX ((DOOR_X0 + DOOR_X1) / 2)
#define CLOCK_Y 38.0f    /* wall clock over the door (Tiden logo dial in a wooden rim, 36 px), centred on DOOR_CX */

/* Window on the right wall: two by two panes split by 3 px bars. */
#define WIN_X0 222.0f
#define WIN_X1 294.0f
#define WIN_Y0 14.0f
#define WIN_Y1 66.0f

/* The Tiden logo, TIDEN_N x TIDEN_N cells: 'b' blue disc, 'g' green wave, '.' outside (tools/logo_clock.py). */
#define TIDEN_N 28
extern const char *const tiden_logo[TIDEN_N];

/* Cabinet with a flower pot under the window. */
#define PLANT_X 258.0f   /* pot centre */
#define POT_TOP 96.0f

/* 0 fresh .. 1 wilted; drawn by room_draw. */
void room_set_plant(float thirst);

/* daylight: 0 night (moon) .. 1 day (sun); door_open: 0 closed .. 1 open. */
void room_draw(uint16_t *fb, float daylight, float door_open, float t);
/* A wooden door in a wall at (x0, y0)-(x1, y1); `outside` is what shows through when open. */
void door_draw(uint16_t *fb, float x0, float y0, float x1, float y1, float open, uint32_t outside);
uint32_t sky_color(float daylight, int band); /* band 0 top .. 2 horizon */
