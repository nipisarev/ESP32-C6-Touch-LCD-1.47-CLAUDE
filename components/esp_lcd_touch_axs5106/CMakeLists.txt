idf_component_register(SRCS "esp_lcd_touch_axs5106.c"
                    INCLUDE_DIRS "include"
                    REQUIRES "esp_lcd" "driver" "espressif__esp_lcd_touch")
# Vendor driver keeps an unused write helper.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function)
