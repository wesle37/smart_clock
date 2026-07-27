#include <stdio.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "lvgl.h"
#include "hal/lcd_types.h"
#include "esp_lcd_types.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_ft6x36.h"

// #include "esp_err.h"
#include "esp_log.h"
#include "sys/lock.h"

#define LCD_HOST  SPI2_HOST

#define LCD_H_RES 480
#define LCD_V_RES 320
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ     (20 * 1000 * 1000)

#define LCD_SCLK 8
#define LCD_MOSI 10
#define LCD_MISO 9
#define LCD_DC 20
#define LCD_CS 2
#define LCD_RST 3

#define TP_PORT 0
#define TP_SDA 6
#define TP_SCL 7
#define TP_RST 4

#define DRAW_BUFFER_LINES LCD_V_RES * 0.1
#define DRAW_BUFFER_SIZE LCD_H_RES * DRAW_BUFFER_LINES * 2
#define LVGL_TICK_PERIOD_MS 100
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS 1000 / CONFIG_FREERTOS_HZ
#define LVGL_TASK_STACK_SIZE   (4 * 1024)
#define LVGL_TASK_PRIORITY     2

static const char *TAG = "example";

static _lock_t lvgl_api_lock;

extern void lvgl_ui(lv_disp_t *disp);

// static bool notify_lvgl_flush_ready(lv_display_t *display){
//     lv_display_flush_ready(display);
//     return false;
// }

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static void lvgl_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map){
    esp_lcd_panel_handle_t lcd_panel_handle = lv_display_get_user_data(display);
    int x1 = area->x1;
    int x2 = area->x2;
    int y1 = area->y1;
    int y2 = area->y2;
    lv_draw_sw_rgb565_swap(px_map, (x2 - x1 + 1) * (y2 - y1 + 1));
    esp_lcd_panel_draw_bitmap(lcd_panel_handle, x1, y1, x2 + 1, y2 + 1, px_map);
}

static void lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data){
    uint16_t touchpad_x[1] = {0};
    uint16_t touchpad_y[1] = {0};
    uint8_t touchpad_cnt = 0;

    esp_lcd_touch_handle_t tp_handle = lv_indev_get_user_data(indev);
    esp_err_t err = esp_lcd_touch_read_data(tp_handle);
    bool touch_pressed = esp_lcd_touch_get_coordinates(tp_handle, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 1);

    ESP_LOGI(TAG, "Touch at %d %d with error %s", touchpad_x[0], touchpad_y[0], err);
    if(touch_pressed && touchpad_cnt > 0){
        
        data->point.x = touchpad_x[0];
        data->point.y = touchpad_y[0];
        data->state = LV_INDEV_STATE_PRESSED;
    }else{
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void example_increase_lvgl_tick(void *arg){
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t time_till_next_ms = 0;
    while (1) {
        _lock_acquire(&lvgl_api_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        // in case of triggering a task watch dog time out
        time_till_next_ms = MAX(time_till_next_ms, LVGL_TASK_MIN_DELAY_MS);
        // in case of lvgl display not ready yet
        time_till_next_ms = MIN(time_till_next_ms, LVGL_TASK_MAX_DELAY_MS);
        usleep(1000 * time_till_next_ms);
    }
}

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

    // uint16_t fill_color = 0x001f;
    // uint16_t wire_color = (fill_color >> 8) | (fill_color << 8);

    // static uint16_t draw_buffer[LCD_H_RES];
    // for(int i = 0; i < LCD_H_RES; i++){
    //     draw_buffer[i] = wire_color;
    // }

    // for(int i = 0; i < LCD_V_RES; i++){
    //     esp_lcd_panel_draw_bitmap(lcd_panel_handle, 0, i, LCD_H_RES, i + 1, draw_buffer);
    // }
    
    ESP_LOGI(TAG, "Initialize I2C bus");

    // Create i2c master bus handle
    i2c_master_bus_config_t tp_bus_cfg = {
        .i2c_port = TP_PORT,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = TP_SDA,
        .scl_io_num = TP_SCL,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        }
    };
    i2c_master_bus_handle_t tp_bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&tp_bus_cfg, &tp_bus_handle));

    ESP_LOGI(TAG, "Initialize FT6336U driver");

    // Create touch panel io handle
    esp_lcd_panel_io_i2c_config_t tp_io_config = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_FT6x36_ADDRESS,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 8,
        .flags = {
            .disable_control_phase = 1,
        },
        .scl_speed_hz = 100000,
    };

    esp_lcd_panel_io_handle_t tp_io_handle;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(tp_bus_handle, &tp_io_config, &tp_io_handle));

    // Create touch handle
    esp_lcd_touch_config_t tp_config = {
        .x_max = LCD_V_RES,
        .y_max = LCD_H_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 1,
            .mirror_x = 1,
            .mirror_y = 0,
        },
    };

    gpio_config_t rst_conf = {
        .pin_bit_mask = 1ULL << TP_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&rst_conf);

    gpio_set_level(TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));   // hold reset, >5ms required
    gpio_set_level(TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(350));  // let firmware boot, >300ms required

    for (uint8_t addr = 0x03; addr < 0x78; addr++) {
        if (i2c_master_probe(tp_bus_handle, addr, 50) == ESP_OK) {
            ESP_LOGI(TAG, "Found device at 0x%02X", addr);
        }
    }

    esp_lcd_touch_handle_t tp_handle;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft6x36(tp_io_handle, &tp_config, &tp_handle));
    


    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);
    void *buf1 = spi_bus_dma_memory_alloc(LCD_HOST, DRAW_BUFFER_SIZE, 0);
    assert(buf1);
    void *buf2 = spi_bus_dma_memory_alloc(LCD_HOST, DRAW_BUFFER_SIZE, 0);
    assert(buf2);

    lv_display_set_buffers(display, buf1, buf2, DRAW_BUFFER_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(display, lcd_panel_handle);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, lvgl_flush_cb);

    ESP_LOGI(TAG, "Install LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    ESP_LOGI(TAG, "Register io panel event callback for LVGL flush ready notification");
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(lcd_io_handle, &cbs, display));

    //Create indev
    ESP_LOGI(TAG, "Create LVGL touch indev");
    static lv_indev_t *indev;
    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, display);
    lv_indev_set_user_data(indev, tp_handle);
    lv_indev_set_read_cb(indev, lvgl_touch_cb);

    ESP_LOGI(TAG, "Create LVGL task");
    xTaskCreate(lvgl_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "Display LVGL widget");
    _lock_acquire(&lvgl_api_lock);
    lvgl_ui(display);
    _lock_release(&lvgl_api_lock);
}
