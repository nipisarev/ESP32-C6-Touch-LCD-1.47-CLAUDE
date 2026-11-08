#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int32_t (*load)(const char *key, int32_t def);
    void (*save)(const char *key, int32_t value);
} app_storage_t;

void app_init(uint32_t seed, const app_storage_t *storage, uint32_t now_ms);
/* Diagnostics: shakes and BOOT presses recognised so far, and the strongest jolt (g) since the last call. */
int app_shake_count(void);
int app_button_count(void);
float app_take_peak_jolt(void);

/* accel_ok: the accelerometer sample (g) is valid this frame; button: the BOOT button is held. */
void app_frame(uint16_t *fb, bool touch_down, int tx, int ty, bool accel_ok, float ax, float ay, float az, bool button, uint32_t now_ms);
