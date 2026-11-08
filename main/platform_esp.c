#include "platform_esp.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_jd9853.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_axs5106.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gfx.h"

static const char *TAG = "platform";

#define PIN_LCD_SCLK GPIO_NUM_1
#define PIN_LCD_MOSI GPIO_NUM_2
#define PIN_LCD_CS GPIO_NUM_14
#define PIN_LCD_DC GPIO_NUM_15
#define PIN_LCD_RST GPIO_NUM_22
#define PIN_LCD_BL GPIO_NUM_23
#define PIN_I2C_SDA GPIO_NUM_18
#define PIN_I2C_SCL GPIO_NUM_19
#define PIN_TP_RST GPIO_NUM_20
#define PIN_TP_INT GPIO_NUM_21
#define PIN_BOOT_BTN GPIO_NUM_9 /* schematic net BOOT = IO9, 10k pull-up, pressed = low */
#define LCD_HOST SPI2_HOST
#define LCD_PCLK_HZ (80 * 1000 * 1000)
#define LCD_NATIVE_W 172
#define LCD_NATIVE_H 320

/* Landscape orientation (same as Waveshare's 90° example). Flip these if the picture is mirrored or upside down. */
#define LCD_SWAP_XY true
#define LCD_MIRROR_X true
#define LCD_MIRROR_Y false
#define LCD_GAP_X 0
#define LCD_GAP_Y 34
#define TP_SWAP_XY 1
#define TP_MIRROR_X 0
#define TP_MIRROR_Y 0

#define BL_DUTY_RES LEDC_TIMER_10_BIT
#define BL_DUTY_MAX 1023

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_touch;
static SemaphoreHandle_t s_flush_done;
static uint16_t *s_fb;
static adc_oneshot_unit_handle_t s_adc;
static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_imu;

#define IMU_ADDR 0x6B
#define IMU_WHO_AM_I 0x00
#define IMU_CTRL1 0x02
#define IMU_CTRL2 0x03
#define IMU_CTRL7 0x08
#define IMU_AX_L 0x35
#define IMU_RESET 0x60
#define IMU_LSB_PER_G 8192.0f /* +-4 g */
static adc_cali_handle_t s_adc_cali;

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done, &woken);
    return woken == pdTRUE;
}

static esp_err_t display_init(void)
{
    const size_t fb_bytes = GFX_W * GFX_H * sizeof(uint16_t);
    spi_bus_config_t bus = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = fb_bytes,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = JD9853_PANEL_IO_SPI_CONFIG(PIN_LCD_CS, PIN_LCD_DC, on_color_done, NULL);
    io_cfg.pclk_hz = LCD_PCLK_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io), TAG, "panel io");

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9853(io, &panel_cfg, &s_panel), TAG, "panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, LCD_SWAP_XY), TAG, "swap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, LCD_MIRROR_X, LCD_MIRROR_Y), TAG, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, LCD_GAP_X, LCD_GAP_Y), TAG, "gap");

    s_flush_done = xSemaphoreCreateBinary();
    s_fb = heap_caps_malloc(fb_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s_fb, ESP_ERR_NO_MEM, TAG, "framebuffer");
    gfx_clear(s_fb, GFX_HEX(0x000000));
    platform_present(s_fb);
    return esp_lcd_panel_disp_on_off(s_panel, true);
}

static esp_err_t backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = BL_DUTY_RES,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc timer");
    ledc_channel_config_t ch = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .gpio_num = PIN_LCD_BL,
        .duty = 0,
    };
    return ledc_channel_config(&ch);
}

static esp_err_t touch_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = 0,
        .scl_io_num = PIN_I2C_SCL,
        .sda_io_num = PIN_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c), TAG, "i2c bus");

    i2c_master_dev_handle_t dev;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ESP_LCD_TOUCH_IO_I2C_AXS5106_ADDRESS,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &dev_cfg, &dev), TAG, "i2c dev");

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_NATIVE_W,
        .y_max = LCD_NATIVE_H,
        .rst_gpio_num = PIN_TP_RST,
        .int_gpio_num = PIN_TP_INT,
        .flags = {.swap_xy = TP_SWAP_XY, .mirror_x = TP_MIRROR_X, .mirror_y = TP_MIRROR_Y},
    };
    return esp_lcd_touch_new_i2c_axs5106(dev, &tp_cfg, &s_touch);
}

static esp_err_t imu_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_imu, buf, 2, 100);
}

/* The IMU is optional: the toy works without it, it just ignores shakes. */
static void imu_init(void)
{
    i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = IMU_ADDR, .scl_speed_hz = 400000};
    uint8_t reg = IMU_WHO_AM_I, id = 0;
    if (i2c_master_bus_add_device(s_i2c, &cfg, &s_imu) != ESP_OK ||
        i2c_master_transmit_receive(s_imu, &reg, 1, &id, 1, 100) != ESP_OK || id != 0x05) {
        ESP_LOGW(TAG, "QMI8658 not found (id 0x%02x): shake disabled", id);
        s_imu = NULL;
        return;
    }
    imu_write(IMU_RESET, 0xB0);
    vTaskDelay(pdMS_TO_TICKS(15));
    imu_write(IMU_CTRL1, 0x40); /* register auto-increment */
    imu_write(IMU_CTRL2, 0x15); /* accel +-4 g, ~250 Hz */
    imu_write(IMU_CTRL7, 0x01); /* accelerometer only */
    ESP_LOGI(TAG, "QMI8658 ready");
}

bool platform_accel(float *ax, float *ay, float *az)
{
    if (!s_imu) return false;
    uint8_t reg = IMU_AX_L, d[6];
    if (i2c_master_transmit_receive(s_imu, &reg, 1, d, 6, 20) != ESP_OK) return false;
    *ax = (int16_t)(d[0] | d[1] << 8) / IMU_LSB_PER_G;
    *ay = (int16_t)(d[2] | d[3] << 8) / IMU_LSB_PER_G;
    *az = (int16_t)(d[4] | d[5] << 8) / IMU_LSB_PER_G;
    return true;
}

static esp_err_t battery_init(void)
{
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit, &s_adc), TAG, "adc unit");
    adc_oneshot_chan_cfg_t ch = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, ADC_CHANNEL_0, &ch), TAG, "adc ch");
    adc_cali_curve_fitting_config_t cali = {
        .unit_id = ADC_UNIT_1, .chan = ADC_CHANNEL_0, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    if (adc_cali_create_scheme_curve_fitting(&cali, &s_adc_cali) != ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration unavailable, VBAT will read 0");
        s_adc_cali = NULL;
    }
    return ESP_OK;
}

esp_err_t platform_init(void)
{
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");
    ESP_RETURN_ON_ERROR(display_init(), TAG, "display");
    ESP_RETURN_ON_ERROR(touch_init(), TAG, "touch");
    imu_init();
    gpio_config_t btn = {.pin_bit_mask = 1ULL << PIN_BOOT_BTN, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    ESP_RETURN_ON_ERROR(gpio_config(&btn), TAG, "boot button");
    ESP_RETURN_ON_ERROR(battery_init(), TAG, "battery");
    return ESP_OK;
}

uint16_t *platform_framebuffer(void) { return s_fb; }

void platform_present(const uint16_t *fb)
{
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, 0, 0, GFX_W, GFX_H, fb));
    xSemaphoreTake(s_flush_done, portMAX_DELAY);
}

void platform_set_backlight(uint8_t percent)
{
    if (percent > 100) percent = 100;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * BL_DUTY_MAX / 100));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}

bool platform_touch_read(int *x, int *y)
{
    uint16_t tx, ty;
    uint8_t n = 0;
    if (esp_lcd_touch_read_data(s_touch) != ESP_OK) return false;
    if (!esp_lcd_touch_get_coordinates(s_touch, &tx, &ty, NULL, &n, 1) || n == 0) return false;
    *x = tx;
    *y = ty;
    return true;
}

float platform_vbat(void)
{
    int raw = 0, mv = 0;
    if (!s_adc_cali || adc_oneshot_read(s_adc, ADC_CHANNEL_0, &raw) != ESP_OK) return 0;
    adc_cali_raw_to_voltage(s_adc_cali, raw, &mv);
    return mv * 3 / 1000.0f; /* 200k/100k divider */
}

bool platform_button_down(void) { return gpio_get_level(PIN_BOOT_BTN) == 0; }

uint32_t platform_millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
