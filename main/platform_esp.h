#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t platform_init(void);
uint16_t *platform_framebuffer(void);
/* Blocks until the frame has been fully clocked out by DMA, so the buffer can be reused. */
void platform_present(const uint16_t *fb);
void platform_set_backlight(uint8_t percent);
/* Returns true while touched; coordinates are in landscape screen space (0..GFX_W, 0..GFX_H). */
bool platform_touch_read(int *x, int *y);
float platform_vbat(void);
/* BOOT button (GPIO9, active low). */
bool platform_button_down(void);
/* Accelerometer in g (QMI8658A). Returns false if the IMU is missing or the read failed. */
bool platform_accel(float *ax, float *ay, float *az);
uint32_t platform_millis(void);
