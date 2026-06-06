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


    uint32_t CS122_App::run() 
    {
        create_ui();

        show_dashboard();

        return loop();
    }

    void CS122_App::create_ui()
    {
        lv_obj_set_style_bg_color(
        lv_screen_active(),
        lv_color_hex(0x003a57),
        LV_PART_MAIN
    );

        create_top_bar();
        create_content_area();
        create_nav_bar();
    }


    void CS122_App::create_top_bar()
    {
        top_bar = lv_obj_create(lv_screen_active());

        lv_obj_set_size(top_bar, 480, 40);
        lv_obj_set_pos(top_bar, 0, 0);

        state_label = lv_label_create(top_bar);
        lv_label_set_text(state_label, "DISARMED");
        lv_obj_align(state_label, LV_ALIGN_LEFT_MID, 10, 0);

        time_label = lv_label_create(top_bar);
        lv_label_set_text(time_label, "14:03");
        lv_obj_align(time_label, LV_ALIGN_CENTER, 0, 0);

        alert_label = lv_label_create(top_bar);
        lv_label_set_text(alert_label, "Alerts: 0");
        lv_obj_align(alert_label, LV_ALIGN_RIGHT_MID, -10, 0);
    }

    void CS122_App::create_content_area()
    {
        content_area = lv_obj_create(lv_screen_active());

        lv_obj_set_size(content_area, 480, 190);
        lv_obj_set_pos(content_area, 0, 40);
    }

    void CS122_App::create_nav_bar()
    {
        nav_bar = lv_obj_create(lv_screen_active());

        lv_obj_set_size(nav_bar, 480, 42);
        lv_obj_set_pos(nav_bar, 0, 230);

        const char *names[] = 
        {
            "DASH",
            "ZONE",
            "LOGS",
            "FULT",
            "CTRL"
        };

         for(int i = 0; i < 5; i++)
            {
                lv_obj_t *btn = lv_button_create(nav_bar);

                lv_obj_set_size(btn, 85, 30);
                lv_obj_set_pos(btn, 5 + (95 * i), 5);

                lv_obj_t *label = lv_label_create(btn);
                lv_label_set_text(label, names[i]);
                lv_obj_center(label);
            }
    }

    void CS122_App::show_dashboard()
        {
            lv_obj_t *title = lv_label_create(content_area);

            lv_label_set_text(title, "System Overview");

            lv_obj_center(title);
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