#ifndef _CS122_APP_H_
#define _CS122_APP_H_

#include "lv_conf.h"
#include "system_security.h"

#include <stdio.h>
#include <lvgl.h>

namespace ucr { namespace bcoe {

class SPIDisplay;

namespace cs { namespace cs122 {

class CS122_App
{
public:

    CS122_App(
        SPIDisplay *spi_disp,
        lv_display_flush_cb_t fcallback,
        lv_tick_get_cb_t tcallback
    );

    void update();
    void init();

    void update_zone_status(bool online);
    void update_door_status(bool open);
    void update_motion_status(bool detected);
    void update_environment_status(
        bool highTemp,
        bool lowTemp,
        bool highHum
    );

    void update_alert_count(uint32_t count);

    void update_system_state(SystemState state);

    void update_alarm_status(bool active);

    void update_event_log(const char* events[5]);

    void refresh_dashboard(
        const ZoneStatus& zone,
        SystemState state,
        uint32_t alertCount
    );

private:

    SPIDisplay *spi_display;

    uint8_t *framebuffer;

    lv_display_t *display;

    lv_display_flush_cb_t flush_callback;
    lv_tick_get_cb_t tick_callback;

    //
    // Permanent UI
    //
    lv_obj_t* top_bar = nullptr;
    lv_obj_t* content_area = nullptr;

    //
    // Dashboard
    //
    lv_obj_t* dashboard_view = nullptr;

    lv_obj_t* system_state_label = nullptr;
    lv_obj_t* alert_count_label = nullptr;
    lv_obj_t* alarm_status_label = nullptr;

    lv_obj_t* zone_status_label = nullptr;
    lv_obj_t* door_status_label = nullptr;
    lv_obj_t* motion_status_label = nullptr;
    lv_obj_t* environment_label = nullptr;

    lv_obj_t* event_labels[5] = {nullptr};

    void create_ui();
    void create_top_bar();
    void create_content_area();

    void show_dashboard();

    lv_obj_t* create_label(
        lv_obj_t* parent,
        const char* text,
        lv_align_t align,
        int x,
        int y
    );
};

}}}}

#endif