#ifndef _CS122_APP_H_
#define _CS122_APP_H_

#include "lv_conf.h"
#include <lvgl.h>

namespace ucr { namespace bcoe { 
    class SPIDisplay;
    namespace cs { namespace cs122{
    class CS122_App {
    public:
        CS122_App(SPIDisplay *spi_disp, lv_display_flush_cb_t fcallback, lv_tick_get_cb_t tcallback);
        void update();
        void init();

        //Update Functions
        void update_dashboard_zone(bool online);
        void update_zone_status(bool online);
        void update_door_status(bool open);
        void update_motion_status(bool detected);
        void update_temperature(float temp);
        void update_system_state(SystemState state);
    
        private:
        SPIDisplay *spi_display;
        uint8_t *framebuffer;
        lv_display_t *display;
        lv_display_flush_cb_t flush_callback;
        lv_tick_get_cb_t tick_callback;

        //Permanent UI
        lv_obj_t* top_bar;
        lv_obj_t* content_area;
        lv_obj_t* nav_bar;

        //Dashboard
        lv_obj_t* dashboard_view;
        lv_obj_t* system_state_label;
        lv_obj_t* zone_status_label;
        lv_obj_t* door_status_label;
        lv_obj_t* motion_status_label;
        lv_obj_t* temperature_label;
        lv_obj_t* alert_count_label;
        
        //Screens
        lv_obj_t* zone_view;

        void create_ui();
        void create_top_bar();
        void create_content_area();
        void create_nav_bar();

        void show_dashboard();
        void refresh_dashboard(const ZoneStatus& zone, SystemState state, uint32_t alertCount);
        
        lv_obj_t* create_label(lv_obj_t* parent, const char* text, lv_align_t align, int x, int y);

    };
}}}}

#endif