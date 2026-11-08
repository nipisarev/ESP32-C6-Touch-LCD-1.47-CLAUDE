#include "app.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gfx.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "platform_esp.h"

static const char *TAG = "main";

#define FRAME_MS 33
#define BACKLIGHT_PERCENT 70

static nvs_handle_t s_nvs;

static int32_t store_load(const char *key, int32_t def)
{
    int32_t v = def;
    if (s_nvs) nvs_get_i32(s_nvs, key, &v);
    return v;
}

static void store_save(const char *key, int32_t value)
{
    if (!s_nvs) return;
    nvs_set_i32(s_nvs, key, value);
    nvs_commit(s_nvs);
}

static const app_storage_t k_storage = {store_load, store_save};

static void storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK || nvs_open("mascot", NVS_READWRITE, &s_nvs) != ESP_OK) {
        ESP_LOGW(TAG, "NVS unavailable, scores will not persist");
        s_nvs = 0;
    }
}

void app_main(void)
{
    storage_init();
    ESP_ERROR_CHECK(platform_init());
    platform_set_backlight(BACKLIGHT_PERCENT);
    uint16_t *fb = platform_framebuffer();
    app_init(esp_random(), &k_storage, platform_millis());
    ESP_LOGI(TAG, "start: free heap %u, VBAT %.2f V", (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT), platform_vbat());

    uint32_t frames = 0, busy_ms = 0, last_report = platform_millis(), last_vbat = last_report;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        uint32_t t0 = platform_millis();
        int tx = 0, ty = 0;
        bool touched = platform_touch_read(&tx, &ty);
        float ax = 0, ay = 0, az = 1;
        bool accel = platform_accel(&ax, &ay, &az);
        app_frame(fb, touched, tx, ty, accel, ax, ay, az, platform_button_down(), t0);
        platform_present(fb);
        busy_ms += platform_millis() - t0;
        frames++;
#ifdef IMU_CALIBRATION_LOG
        static uint32_t last_acc;
        if (accel && t0 - last_acc >= 500) {
            ESP_LOGI(TAG, "acc %.2f %.2f %.2f", ax, ay, az);
            last_acc = t0;
        }
#endif

        uint32_t now = platform_millis();
        if (now - last_report >= 10000) {
            ESP_LOGI(TAG, "fps %.1f, frame %.1f ms, free heap %u, peak jolt %.2f g, shakes %d, boot presses %d",
                     frames * 1000.0f / (now - last_report), (float)busy_ms / frames, (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                     app_take_peak_jolt(), app_shake_count(), app_button_count());
            frames = busy_ms = 0;
            last_report = now;
        }
        if (now - last_vbat >= 60000) {
            ESP_LOGI(TAG, "VBAT %.2f V", platform_vbat());
            last_vbat = now;
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(FRAME_MS));
    }
}
