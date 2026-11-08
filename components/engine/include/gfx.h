#pragma once
#include <stdint.h>

#define GFX_W 320
#define GFX_H 172

#define GFX_RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define GFX_SWAP16(c) ((uint16_t)((((c) >> 8) & 0xFF) | (((c) & 0xFF) << 8)))
/* Framebuffer pixels are stored byte-swapped: the panel expects big-endian RGB565 over SPI. */
#define GFX_HEX(hex) GFX_SWAP16(GFX_RGB565(((hex) >> 16) & 0xFF, ((hex) >> 8) & 0xFF, (hex) & 0xFF))

/* The panel washes colors out toward white; colors from art (sRGB hex) pass through a
 * gamma + saturation correction before conversion to panel RGB565. */
#define GFX_GAMMA 1.4f      /* picked by the user on the device */
#define GFX_SATURATION 1.1f
void gfx_set_color_correction(float gamma, float saturation);
uint16_t gfx_color(uint32_t rgb);
uint16_t gfx_color_with(uint32_t rgb, float gamma, float saturation);

void gfx_clear(uint16_t *fb, uint16_t color);
void gfx_fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color);
typedef struct {
    int x0, y0, x1, y1; /* half-open pixel rectangle */
} gfx_clip_t;

/* Axis-aligned fill in float coordinates; edges round to pixel boundaries. */
void gfx_fill_box(uint16_t *fb, float x0, float y0, float x1, float y1, uint16_t color, const gfx_clip_t *clip);
/* Box (x0,y0)-(x1,y1) in local coordinates, rotated by `ang` radians about the pivot (px, py). */
void gfx_fill_rot_box(uint16_t *fb, float px, float py, float ang, float x0, float y0, float x1, float y1, uint16_t color);
/* Checkerboard fill: every other pixel, a cheap see-through layer (water). */
void gfx_fill_box_dither(uint16_t *fb, float x0, float y0, float x1, float y1, uint16_t color);
/* One pixel in four: a lighter see-through tint. */
void gfx_fill_box_dither_sparse(uint16_t *fb, float x0, float y0, float x1, float y1, uint16_t color);
/* Convex quad (corners in order); pixels whose centers fall inside are filled. */
void gfx_fill_quad(uint16_t *fb, const float xs[4], const float ys[4], uint16_t color, const gfx_clip_t *clip);

/* Edges are rounded independently so adjacent scaled rects share boundaries without gaps. */
void gfx_fill_rectf(uint16_t *fb, float x, float y, float w, float h, uint16_t color);
