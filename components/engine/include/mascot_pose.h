#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Parametric mascot built from the base Walking pose (viewBox 107x86, feet at y=86). Used by games. */
#define MASCOT_W 107.0f
#define MASCOT_H 86.0f
#define MASCOT_RGB 0xDD775B

typedef struct {
    float eye_dx, eye_dy; /* look direction, viewBox units (about +-4) */
    float eye_open;       /* 1 open .. 0 closed */
    bool happy;           /* ^ ^ eyes */
    float leg_lift[4];    /* 0 planted .. 1 lifted (scaleY 0.45 about the hip) */
    float body_dy;        /* body offset; legs stretch or squash to stay on the ground */
    float hands_dy;       /* extra offset for both hands */
    float hand_dy[2];     /* per-hand extra offset: [0] left, [1] right */
    float hand_dx[2];     /* per-hand horizontal offset (towards the centre to hold things in front) */
    bool spiral;          /* dizzy: spinning spiral eyes */
    float spiral_t;       /* spin clock, seconds */
} mascot_pose_t;

#define MASCOT_POSE_REST ((mascot_pose_t){0, 0, 1, false, {0, 0, 0, 0}, 0, 0, {0, 0}, {0, 0}, false, 0})

/* (ox, oy) is the screen position of the viewBox origin; feet land at oy + 86*s. */
void mascot_pose_draw(uint16_t *fb, float ox, float oy, float s, const mascot_pose_t *p);
/* The two layers of mascot_pose_draw, so props can go between the body and the hands. */
void mascot_pose_draw_body(uint16_t *fb, float ox, float oy, float s, const mascot_pose_t *p);
void mascot_pose_draw_hands(uint16_t *fb, float ox, float oy, float s, const mascot_pose_t *p);
/* Screen rectangle of a hand: i = 0 left, 1 right. */
void mascot_hand_rect(float ox, float oy, float s, const mascot_pose_t *p, int i, float *x0, float *y0, float *x1, float *y1);
/* Three Claude sparks orbiting above a head whose top is at head_y (dizzy). fade 0..1 shrinks the orbit. */
void draw_spark_orbit(uint16_t *fb, float cx, float head_y, float t, float fade);
/* Pixel heart (7x6 cells) with its top-left at (x, y). */
void draw_heart(uint16_t *fb, float x, float y, float cell, uint16_t color);
