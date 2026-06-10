#include "cs122_app.h"
#include "spi_display.h"
#include "lvgl_touch.h"
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

    lv_obj_t* CS122_App::create_label(lv_obj_t* parent,const char* text,lv_align_t align,int x,int y)
    {
        lv_obj_t* label = lv_label_create(parent);
        lv_label_set_text(label, text);
        lv_obj_align(label, align, x, y);
        return label;
    }

    void CS122_App::create_ui()
    {
        lv_obj_set_style_bg_color(lv_screen_active(),lv_color_hex(0x003a57),LV_PART_MAIN);

        create_top_bar();
        create_content_area();
    }

    void CS122_App::create_top_bar()
    {
        top_bar = lv_obj_create(lv_screen_active());

        lv_obj_set_size(top_bar, 480, 40);
        lv_obj_set_pos(top_bar, 0, 0);

        system_state_label =
            create_label(
                top_bar,
                "SYSTEM: DISARMED",
                LV_ALIGN_LEFT_MID,
                10,
                0
            );

        alert_count_label =
            create_label(
                top_bar,
                "Alerts: 0",
                LV_ALIGN_RIGHT_MID,
                -10,
                0
            );
    }

    void CS122_App::create_content_area()
    {
        content_area = lv_obj_create(lv_screen_active());
        lv_obj_clear_flag(content_area,LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_size(content_area, 480, 232);
        lv_obj_set_pos(content_area, 0, 40);
    }

    void CS122_App::show_dashboard()
    {
        lv_obj_clean(content_area);

        dashboard_view = lv_obj_create(content_area);

        lv_obj_clear_flag(dashboard_view,LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_size(
            dashboard_view,
            470,
            220
        );

        lv_obj_center(dashboard_view);

        //
        // Alarm Status
        //
        create_label(
            dashboard_view,
            "ALARM:",
            LV_ALIGN_TOP_RIGHT,
            -120,
            10
        );

        alarm_status_label =
            create_label(
                dashboard_view,
                "OFF",
                LV_ALIGN_TOP_RIGHT,
                -40,
                10
            );

        //
        // Zone Panel
        //
        lv_obj_t* zone_panel = lv_obj_create(dashboard_view);

        lv_obj_set_size(zone_panel, 210, 110);
        lv_obj_set_pos(zone_panel, 10, 35);

        lv_obj_clear_flag(zone_panel,LV_OBJ_FLAG_SCROLLABLE);

        create_label(
            zone_panel,
            "ZONE 1",
            LV_ALIGN_TOP_MID,
            0,
            5
        );

        create_label(
            zone_panel,
            "CONNECTION:",
            LV_ALIGN_TOP_LEFT,
            10,
            30
        );

        zone_status_label =
            create_label(
                zone_panel,
                "ONLINE",
                LV_ALIGN_TOP_LEFT,
                90,
                30
            );

        create_label(
            zone_panel,
            "DOOR:",
            LV_ALIGN_TOP_LEFT,
            10,
            50
        );

        door_status_label =
            create_label(
                zone_panel,
                "CLOSED",
                LV_ALIGN_TOP_LEFT,
                90,
                50
            );

        create_label(
            zone_panel,
            "MOTION:",
            LV_ALIGN_TOP_LEFT,
            10,
            70
        );

        motion_status_label =
            create_label(
                zone_panel,
                "CLEAR",
                LV_ALIGN_TOP_LEFT,
                90,
                70
            );

        create_label(
            zone_panel,
            "ENV:",
            LV_ALIGN_TOP_LEFT,
            10,
            90
        );

        environment_label =
            create_label(
                zone_panel,
                "NORMAL",
                LV_ALIGN_TOP_LEFT,
                90,
                90
            );

        //
        // Event Log
        //
        lv_obj_t* log_panel = lv_obj_create(dashboard_view);

        lv_obj_set_size(log_panel, 220, 160);
        lv_obj_set_pos(log_panel, 235, 35);

        create_label(
            log_panel,
            "RECENT EVENTS",
            LV_ALIGN_TOP_MID,
            0,
            5
        );

        for(int i = 0; i < 5; i++)
        {
            event_labels[i] =
                create_label(
                    log_panel,
                    "---",
                    LV_ALIGN_TOP_LEFT,
                    10,
                    30 + (22 * i)
                );
        }
    }

    void CS122_App::update_alarm_status(bool active)
    {
        lv_label_set_text(
            alarm_status_label,
            active ? "ACTIVE" : "OFF"
        );
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
            detected ? "DETECTED" : "CLEAR"
        );
    }

    void CS122_App::update_environment_status(
        bool highTemp,
        bool lowTemp,
        bool highHum
    )
    {
        if(highTemp)
        {
            lv_label_set_text(
                environment_label,
                "HIGH TEMP"
            );
        }
        else if(lowTemp)
        {
            lv_label_set_text(
                environment_label,
                "LOW TEMP"
            );
        }
        else if(highHum)
        {
            lv_label_set_text(
                environment_label,
                "HIGH HUM"
            );
        }
        else
        {
            lv_label_set_text(
                environment_label,
                "NORMAL"
            );
        }
    }

    void CS122_App::update_event_log(
        const char* events[5]
    )
    {
        for(int i = 0; i < 5; i++)
        {
            lv_label_set_text(
                event_labels[i],
                events[i]
            );
        }
    }

    void CS122_App::update_system_state(
        SystemState state
    )
    {
        switch(state)
        {
            case DISARMED:

                lv_label_set_text(
                    system_state_label,
                    "SYSTEM: DISARMED"
                );

                break;

            case ARMED:

                lv_label_set_text(
                    system_state_label,
                    "SYSTEM: ARMED"
                );

                break;

            case ALERT:

                lv_label_set_text(
                    system_state_label,
                    "SYSTEM: ALERT"
                );

                break;

            case FAULT:

                lv_label_set_text(
                    system_state_label,
                    "SYSTEM: FAULT"
                );

                break;
        }
    }

    void CS122_App::update_alert_count(uint32_t count)
    {
        char buffer[32];

        snprintf(
            buffer,
            sizeof(buffer),
            "Alerts: %lu",
            (unsigned long)count
        );

        lv_label_set_text(
            alert_count_label,
            buffer
        );
    }

    void CS122_App::refresh_dashboard(
        const ZoneStatus& zone,
        SystemState state,
        uint32_t alertCount
    )
    {
        update_system_state(state);

        update_zone_status(zone.alive);

        update_door_status(zone.door_open);

        update_motion_status(zone.motion);

        update_environment_status(
            zone.high_temp,
            zone.low_temp,
            zone.high_hum
        );

        update_alert_count(alertCount);

        update_alarm_status(
            state == ALERT
        );
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