#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "input.h"

/* Shared layout */
#define GROUND_Y 170.0f /* feet line for full-size animations */

typedef struct {
    void (*enter)(uint32_t now_ms);
    void (*update)(const input_t *in, uint32_t now_ms, float dt);
    void (*draw)(uint16_t *fb, uint32_t now_ms);
    /* Optional: true while the long-press menu gesture must be ignored (drag-controlled play). */
    bool (*menu_locked)(void);
} scene_t;

extern const scene_t scene_idle, scene_menu, scene_runner, scene_catch, scene_pet, scene_tower, scene_pong, scene_pool, scene_race;

void app_switch(const scene_t *scene, uint32_t now_ms);
int32_t app_load(const char *key, int32_t def);
void app_save(const char *key, int32_t value);
uint32_t app_uptime_ms(void); /* time of the current frame, ms since boot */

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
/* GSAP naming: power2 is cubic. */
static inline float ease_out2(float p) { p = clampf(p, 0, 1); float q = 1 - p; return 1 - q * q * q; }
static inline float ease_in2(float p) { p = clampf(p, 0, 1); return p * p * p; }
static inline float ease_inout2(float p) { p = clampf(p, 0, 1); return p < 0.5f ? 4 * p * p * p : 1 - 4 * (1 - p) * (1 - p) * (1 - p); }

/* Pseudo-random helpers (xorshift, seeded by the platform). */
void rng_seed(uint32_t seed);
uint32_t rng_next(void);
float rng_float(float lo, float hi);
