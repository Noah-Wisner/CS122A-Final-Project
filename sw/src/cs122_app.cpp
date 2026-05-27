#include "cs122_app.h"
#include "spi_display.h"
#include <pico/time.h>

namespace ucr { namespace bcoe { namespace cs { namespace cs122 {
    CS122_App::CS122_App(SPIDisplay *spi_disp, lv_display_flush_cb_t fcallback, lv_tick_get_cb_t tcallback) :
        spi_display(spi_disp), flush_callback(fcallback), tick_callback(tcallback), running(false) {
        lv_init();

        lv_tick_set_cb(tick_callback);

        auto w = spi_display->getWidth();
        auto h = spi_display->getHeight();

        display = lv_display_create(w, h);

        /*LVGL will render to this 1/10 screen sized buffer for 2 bytes/pixel*/
        uint32_t size = w * h * 2 / 10;
        framebuffer = new uint8_t[size];

        lv_display_set_buffers(display, framebuffer, NULL, size, LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_user_data(display, spi_display);

        /*This callback will display the rendered image*/
        lv_display_set_flush_cb(display, flush_callback);
    }

    uint32_t CS122_App::run() {
        //background color
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x003a57), LV_PART_MAIN);

        //label
        lv_obj_t * label = lv_label_create(lv_screen_active());
        lv_label_set_text(label, "SECURITY SYSTEM");
        lv_obj_set_style_text_color(lv_screen_active(), lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_align(label, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);

        const int key_width = 30;
        const int key_height = 30;
        
        const int start_x = 10;
        const int start_y = 10;

        const int spacing = 5;

        int number = 1;

        //keypad
        for (int row = 0; row < 3; row++)
        {
            for (int col = 0; col < 3; col++)
            {
                lv_obj_t * btn = lv_button_create(lv_screen_active());
                lv_obj_set_size(btn, key_width, key_height);

                lv_obj_set_pos(btn, start_x + col * (key_width + spacing), start_y + row * (key_height + spacing));
            }
        }
        return loop();
    }


    uint32_t CS122_App::loop() {
        running = true;
        while(running) {
            lv_timer_handler();
            sleep_ms(5);  /*Wait 5 milliseconds before processing LVGL timer again*/
        }
        return 0;
    }
}}}} 