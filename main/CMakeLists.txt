idf_component_register(SRCS "main.c" "platform_esp.c"
                       INCLUDE_DIRS "."
                       REQUIRES engine esp_lcd_jd9853 esp_lcd_touch_axs5106 esp_lcd esp_driver_spi esp_driver_i2c
                                esp_driver_ledc esp_driver_gpio esp_adc esp_timer nvs_flash)
