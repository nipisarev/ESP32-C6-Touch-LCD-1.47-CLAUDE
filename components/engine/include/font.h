#pragma once
#include <stdint.h>

/* 3x5 pixel font: digits, A-Z, space and : ! - / . + */
int font_width(const char *s, int scale);
void font_draw(uint16_t *fb, int x, int y, int scale, uint16_t color, const char *s);
void font_draw_centered(uint16_t *fb, int cx, int y, int scale, uint16_t color, const char *s);
