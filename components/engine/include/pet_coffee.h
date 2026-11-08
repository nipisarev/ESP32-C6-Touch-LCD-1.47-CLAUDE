#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "input.h"

/* PET's coffee corner, first person: Claude's hands hold an empty mug; tap the coffee sachet, the milk and
 * the kettle to add each one, then Claude drinks it. */
void coffee_start(void);
/* true once the mug is empty and the screen has faded out */
bool coffee_update(const input_t *in, float dt);
void coffee_draw(uint16_t *fb, float t);
