#include "cs122_app.h"
#include "spi_display.h"
#include <pico/time.h>

namespace ucr { namespace bcoe { namespace cs { namespace cs122 {
    CS122_App::CS122_App(SPIDisplay *spi_disp, lv_display_flush_cb_t fcallback, lv_tick_get_cb_t tcallback) :
        spi_display(spi_disp), flush_callback(fcallback), tick_callback(tcallback) {
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

    lv_obj_t* CS122_App::create_label(lv_obj_t* parent, const char* text, lv_align_t align, int x, int y)
    {
        lv_obj_t* label = lv_label_create(parent);
        lv_label_set_text(label, text);
        lv_obj_align(label, align,x,y); 
        return label;
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

        state_label = create_label(top_bar, "DISARMED", LV_ALIGN_LEFT_MID,10, 0);
        time_label = create_label(top_bar, "14:03", LV_ALIGN_CENTER, 0, 0);
        alert_label = create_label(top_bar, "Alerts: 0", LV_ALIGN_RIGHT_MID, -10, 0);
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
                lv_obj_set_pos(btn, -10 + (95 * i), -10);

                lv_obj_t *label = lv_label_create(btn);
                lv_label_set_text(label, names[i]);
                lv_obj_center(label);
            }
    }

    void CS122_App::show_dashboard()
    {
        lv_obj_clean(content_area);
        dashboard_view = lv_obj_create(content_area);

        lv_obj_set_size(dashboard_view, 460, 170);
        lv_obj_center(dashboard_view);

        //Dashboard Title
        create_label(dashboard_view, "System Overview", LV_ALIGN_TOP_MID, 0, 5);

        //Zone Title
        create_label( dashboard_view, "Zones", LV_ALIGN_TOP_LEFT, 10, 35 );

        //Zone Summary (DYNAMIC)
        zone_status_label = create_label(dashboard_view, "OFFLINE", LV_ALIGN_TOP_LEFT, 20, 60);
    }


    void CS122_App::update_zone_status(bool online)
    {
        lv_label_set_text(
            zone_status_label,
            online ? "ONLINE" : "OFFLINE"
        );
    }
    void CS122_App::update_door_status(bool open)
    {
        lv_label_set_text(
            door_status_label,
            open ? "OPEN" : "CLOSED"
        );
    }

    void CS122_App::update_motion_status(bool detected)
    {
        lv_label_set_text(
            motion_status_label,
            detected ? "MOTION" : "CLEAR"
        );
    }
    void CS122_App::update_temperature(float temperature)
    {
        char buffer[32];

        snprintf(buffer, sizeof(buffer),"%.1f C", temperature);

        lv_label_set_text(temperature_label, buffer);
    }

    void CS122_App::update_alert_count(uint32_t count)
    {
        char buffer[32];

        snprintf(
            buffer,
            sizeof(buffer),
            "Alerts: %lu",
            count
        );

        lv_label_set_text(
            alert_count_label,
            buffer
        );
    }

    void CS122_App::update_system_state(SystemState state)
    {
        switch(state)
        {
            case DISARMED:
                lv_label_set_text(system_state_label, "DISARMED");
                break;

            case ARMED:
                lv_label_set_text(system_state_label, "ARMED");
                break;

            case ALERT:
                lv_label_set_text(system_state_label, "ALERT");
                break;
        }
    }

    void CS122_App::refresh_dashboard(const ZoneStatus& zone, SystemState state, uint32_t alertCount)
    {
        update_system_state(state);

        update_zone_status(zone.online);

        update_door_status(zone.doorOpen);

        update_motion_status(zone.motionDetected);

        update_temperature(zone.temperature);

        update_alert_count(alertCount);
    }

    void CS122_App::init()
    {
        create_ui();
        show_dashboard();
    }

    void CS122_App::update()
    {
        lv_timer_handler();
    }

}}}}