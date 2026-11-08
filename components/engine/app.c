#include "app.h"

#include "input.h"
#include "scenes.h"

static const app_storage_t *s_storage;
static const scene_t *s_scene;
static input_t s_input;
static uint32_t s_last_ms;
static uint32_t s_rng = 0x9E3779B9u;
static int s_shakes, s_button_presses;

int app_button_count(void) { return s_button_presses; }
static float s_peak;

int app_shake_count(void) { return s_shakes; }

float app_take_peak_jolt(void)
{
    float p = s_peak;
    s_peak = 0;
    return p;
}

void rng_seed(uint32_t seed) { s_rng = seed ? seed : 0x9E3779B9u; }

uint32_t rng_next(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

float rng_float(float lo, float hi) { return lo + (hi - lo) * (rng_next() & 0xFFFFFF) / (float)0x1000000; }

void app_switch(const scene_t *sc, uint32_t now)
{
    s_scene = sc;
    s_scene->enter(now);
}

int32_t app_load(const char *key, int32_t def) { return s_storage ? s_storage->load(key, def) : def; }

void app_save(const char *key, int32_t value)
{
    if (s_storage) s_storage->save(key, value);
}

void app_init(uint32_t seed, const app_storage_t *storage, uint32_t now_ms)
{
    rng_seed(seed);
    input_reset(&s_input);
    s_storage = storage;
    s_last_ms = now_ms;
    app_switch(&scene_idle, now_ms);
}

uint32_t app_uptime_ms(void) { return s_last_ms; }

void app_frame(uint16_t *fb, bool touch_down, int tx, int ty, bool accel_ok, float ax, float ay, float az, bool button, uint32_t now_ms)
{
    float dt = (now_ms - s_last_ms) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;
    s_last_ms = now_ms;
    input_update(&s_input, touch_down, tx, ty, now_ms);
    input_motion(&s_input, accel_ok, ax, ay, az, now_ms, dt);
    if (s_input.shake) s_shakes++;
    if (s_input.shake_level > s_peak) s_peak = s_input.shake_level;
    /* BOOT button: debounced press edge; it always works, even where the long press is locked */
    static uint32_t btn_since;
    static bool btn_state, btn_was;
    if (button != btn_state) btn_since = now_ms, btn_state = button;
    bool btn = btn_state && now_ms - btn_since >= 30;
    bool btn_press = btn && !btn_was;
    btn_was = btn;
    if (btn_press) s_button_presses++;

    bool locked = s_scene->menu_locked && s_scene->menu_locked();
    if (btn_press || (s_input.long_press && !locked)) /* menu, or back to the show from the menu */
        app_switch(s_scene == &scene_menu ? &scene_idle : &scene_menu, now_ms);
    else
        s_scene->update(&s_input, now_ms, dt);
    s_scene->draw(fb, now_ms);
}
