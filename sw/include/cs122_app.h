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

        lv_obj_t* state_label;
        lv_obj_t* time_label;
        lv_obj_t* alert_label;

        //Dashboard
        lv_obj_t* dashboard_view;

        //Screens
        lv_obj_t* zone_view;

        void create_ui();
        void create_top_bar();
        void create_content_area();
        void create_nav_bar();

        void show_dashboard();
        void show_zones();
        void show_log();
        void show_faults();

        lv_obj_t* create_label(lv_obj_t* parent, const char* text, lv_align_t align, int x, int y);

    };
}}}}

#endif