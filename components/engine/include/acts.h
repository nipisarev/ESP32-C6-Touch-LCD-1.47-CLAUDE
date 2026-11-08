#pragma once
#include <stdint.h>

/* Everyday activities with props for the SHOW director. Each starts and ends in the base pose. */
typedef enum { PACT_NOTEBOOK, PACT_PHONE, PACT_COFFEE, PACT_SNACK, PACT_WATER, PACT_PARK, PACT_COUNT } pact_t;

float pact_duration(pact_t a);
/* Call when activity `a` starts (resets the walk's one-off events). */
void pact_begin(pact_t a);
/* A shake during the walk in the park: by day (daylight > 0.5) a ball game, at night a thunderstorm and a run home.
 * Once per walk, only while out in the park. Returns the walk's new total duration, or 0 if ignored. */
float pact_park_shake(float t, float daylight);
/* Draws activity `a` at time t; the mascot's body centre is at cx, feet on `ground`.
 * PACT_PARK draws its own backgrounds (home and park) and must start with cx = PACT_PARK_START_X. */
void pact_draw(uint16_t *fb, pact_t a, float t, float cx, float ground, float daylight);
#define PACT_PARK_START_X 60.0f
#define PACT_WATER_X 176.0f    /* stands left of the cabinet */
#define PACT_WATER_POUR_T0 1.2f  /* the plant perks up while water flows */
#define PACT_WATER_POUR_T1 4.2f
