#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/lcd_types.h"
#include "esp_lcd_types.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
// #include "esp_err.h"
#include "esp_log.h"
#define LCD_HOST  SPI2_HOST

#define EXAMPLE_LCD_PIXEL_CLOCK_HZ     (20 * 1000 * 1000)

#define LCD_H_RES 480
#define LCD_V_RES 320

#define COLOR_SIZE LCD_H_RES * LCD_V_RES * 2

#define LCD_SCLK 8
#define LCD_MOSI 10
#define LCD_MISO 9
#define LCD_DC 20
#define LCD_CS 2
#define LCD_RST 3

static const char *TAG = "example";

void app_main(void)
{
    printf("Starting RTOS tasks...\n");
    
    ESP_LOGI(TAG, "Initialize SPI bus");
    // Attach the LCD to the SPI bus
    spi_bus_config_t lcd_buscfg = {
        .sclk_io_num = LCD_SCLK,
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 80 * sizeof(uint16_t), // transfer 80 lines of pixels (assume pixel is RGB565) at most in one SPI transaction
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &lcd_buscfg, SPI_DMA_CH_AUTO)); // Enable the DMA feature

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t lcd_io_handle = NULL;
    esp_lcd_panel_io_spi_config_t lcd_io_config = {
        .dc_gpio_num = LCD_DC,
        .cs_gpio_num = LCD_CS,
        .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .flags = {.lsb_first = 0}
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &lcd_io_config, &lcd_io_handle));
    
    esp_lcd_panel_handle_t lcd_panel_handle = NULL;
    esp_lcd_panel_dev_config_t lcd_panel_config = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_LOGI(TAG, "Install ST7796 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(lcd_io_handle, &lcd_panel_config, &lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(lcd_panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(lcd_panel_handle, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(lcd_panel_handle, false));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd_panel_handle, true));

    uint16_t fill_color = 0x001f;
    uint16_t wire_color = (fill_color >> 8) | (fill_color << 8);

    static uint16_t color[LCD_H_RES];
    for(int i = 0; i < LCD_H_RES; i++){
        color[i] = wire_color;
    }

    for(int i = 0; i < LCD_V_RES; i++){
        esp_lcd_panel_draw_bitmap(lcd_panel_handle, 0, i, LCD_H_RES, i + 1, color);
    }
    

}
