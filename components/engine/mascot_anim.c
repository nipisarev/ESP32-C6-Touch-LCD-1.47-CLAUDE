#include "mascot_anim.h"

#include <math.h>
#include <string.h>

#include "gfx.h"

#define MAX_NODES 1024
#define MAX_TRACKS 64

typedef struct {
    float a, b, c, d, e, f; /* x' = a x + c y + e, y' = b x + d y + f */
} mat_t;

static mat_t s_world[MAX_NODES];
static gfx_clip_t s_clip[MAX_NODES];
static uint8_t s_hidden[MAX_NODES];
static int16_t s_first_track[MAX_NODES];
static int16_t s_next_track[MAX_TRACKS];
static float s_track_val[MAX_TRACKS];

static mat_t mat_mul(mat_t m, mat_t n)
{
    return (mat_t){m.a * n.a + m.c * n.b, m.b * n.a + m.d * n.b, m.a * n.c + m.c * n.d,
                   m.b * n.c + m.d * n.d, m.a * n.e + m.c * n.f + m.e, m.b * n.e + m.d * n.f + m.f};
}

static float ease(int id, float p)
{
    float q = 1.0f - p;
    switch (id) {
    case 1: return p * p;
    case 2: return 1.0f - q * q;
    case 3: return p < 0.5f ? 2 * p * p : 1.0f - 2 * q * q;
    case 4: return p * p * p;
    case 5: return 1.0f - q * q * q;
    case 6: return p < 0.5f ? 4 * p * p * p : 1.0f - 4 * q * q * q;
    case 7: return p * p * p * p;
    case 8: return 1.0f - q * q * q * q;
    case 9: return p < 0.5f ? 8 * p * p * p * p : 1.0f - 8 * q * q * q * q;
    case 10: return 1.0f - cosf(p * (float)M_PI / 2);
    case 11: return sinf(p * (float)M_PI / 2);
    case 12: return -(cosf((float)M_PI * p) - 1.0f) / 2;
    default: return p;
    }
}

static float track_eval(const manim_track_t *tr, float tau)
{
    const manim_seg_t *s = &mascot_segs[tr->seg_start];
    if (tau < s[0].t0) return s[0].v0;
    int i = tr->seg_count - 1;
    while (i > 0 && s[i].t0 > tau) i--;
    const manim_seg_t *g = &s[i];
    if (tau >= g->t1) return g->v1;
    return g->v0 + (g->v1 - g->v0) * ease(g->ease, (tau - g->t0) / (g->t1 - g->t0));
}

static void eval_tracks(const manim_anim_t *an, float t)
{
    for (int i = 0; i < an->node_count; i++) s_first_track[i] = -1;
    /* Tracks are sorted by timeline start; link them per node so later timelines override. */
    for (int k = an->track_count - 1; k >= 0; k--) {
        const manim_track_t *tr = &mascot_tracks[an->track_start + k];
        const manim_tl_t *tl = &mascot_tls[an->tl_start + tr->tl];
        float tau = t - tl->delay;
        s_next_track[k] = s_first_track[tr->node];
        s_first_track[tr->node] = (int16_t)k;
        if (tau < 0) {
            s_track_val[k] = NAN; /* timeline not started: keep base value */
            continue;
        }
        if (tl->repeat < 0) tau = fmodf(tau, tl->period);
        else if (tau > tl->period) tau = tl->period;
        s_track_val[k] = track_eval(tr, tau);
    }
}

void manim_draw(uint16_t *fb, int anim, float t, float ox, float oy, float scale)
{
    const manim_anim_t *an = &mascot_anims[anim];
    eval_tracks(an, t);
    const mat_t screen = {scale, 0, 0, scale, ox, oy};

    for (int i = 0; i < an->node_count; i++) {
        const manim_node_t *n = &mascot_nodes[an->node_start + i];
        float p[MP_COUNT];
        memcpy(p, n->base, sizeof(p));
        for (int k = s_first_track[i]; k >= 0; k = s_next_track[k]) {
            const manim_track_t *tr = &mascot_tracks[an->track_start + k];
            if (!isnan(s_track_val[k])) p[tr->prop] = s_track_val[k];
        }

        mat_t parent = n->parent < 0 ? screen : s_world[n->parent];
        gfx_clip_t clip = n->parent < 0 ? (gfx_clip_t){0, 0, GFX_W, GFX_H} : s_clip[n->parent];
        s_hidden[i] = (n->parent >= 0 && s_hidden[n->parent]) || p[MP_DISPLAY] < 0.5f;
        if (s_hidden[i]) continue;

        /* GSAP: translate(x,y) * rotate/scale about svgOrigin; then the element's own transform attribute. */
        float rad = p[MP_ROT] * (float)M_PI / 180.0f, cs = cosf(rad), sn = sinf(rad);
        mat_t gs = {cs, sn, -sn * p[MP_SY], cs * p[MP_SY], 0, 0};
        gs.e = p[MP_X] + p[MP_OX] - (gs.a * p[MP_OX] + gs.c * p[MP_OY]);
        gs.f = p[MP_Y] + p[MP_OY] - (gs.b * p[MP_OX] + gs.d * p[MP_OY]);
        mat_t attr = {p[MP_MA], p[MP_MB], p[MP_MC], p[MP_MD], p[MP_ME], p[MP_MF]};
        mat_t m = mat_mul(mat_mul(parent, gs), attr);

        if (!n->is_rect) {
            s_world[i] = m;
            if (n->has_clip) { /* groups are only translated/scaled where clips are used */
                float x0 = m.a * n->clip.x + m.e, y0 = m.d * n->clip.y + m.f;
                float x1 = m.a * (n->clip.x + n->clip.w) + m.e, y1 = m.d * (n->clip.y + n->clip.h) + m.f;
                gfx_clip_t c = {(int)floorf(fminf(x0, x1)), (int)floorf(fminf(y0, y1)), (int)ceilf(fmaxf(x0, x1)), (int)ceilf(fmaxf(y0, y1))};
                if (c.x0 > clip.x0) clip.x0 = c.x0;
                if (c.y0 > clip.y0) clip.y0 = c.y0;
                if (c.x1 < clip.x1) clip.x1 = c.x1;
                if (c.y1 < clip.y1) clip.y1 = c.y1;
            }
            s_clip[i] = clip;
            continue;
        }

        float rx[2] = {n->r.x, n->r.x + n->r.w}, ry[2] = {p[MP_ATTR_Y], p[MP_ATTR_Y] + n->r.h};
        uint16_t color = gfx_color(n->color);
        if (fabsf(m.b) < 1e-4f && fabsf(m.c) < 1e-4f) {
            float x0 = m.a * rx[0] + m.e, x1 = m.a * rx[1] + m.e, y0 = m.d * ry[0] + m.f, y1 = m.d * ry[1] + m.f;
            gfx_fill_box(fb, fminf(x0, x1), fminf(y0, y1), fmaxf(x0, x1), fmaxf(y0, y1), color, &clip);
        } else if (fabsf(m.a) < 1e-4f && fabsf(m.d) < 1e-4f) { /* 90-degree rotation: still a box */
            float x0 = m.c * ry[0] + m.e, x1 = m.c * ry[1] + m.e, y0 = m.b * rx[0] + m.f, y1 = m.b * rx[1] + m.f;
            gfx_fill_box(fb, fminf(x0, x1), fminf(y0, y1), fmaxf(x0, x1), fmaxf(y0, y1), color, &clip);
        } else {
            const float cx[4] = {rx[0], rx[1], rx[1], rx[0]}, cy[4] = {ry[0], ry[0], ry[1], ry[1]};
            float xs[4], ys[4];
            for (int k = 0; k < 4; k++) {
                xs[k] = m.a * cx[k] + m.c * cy[k] + m.e;
                ys[k] = m.b * cx[k] + m.d * cy[k] + m.f;
            }
            gfx_fill_quad(fb, xs, ys, color, &clip);
        }
    }
}
