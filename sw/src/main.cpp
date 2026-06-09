
#include "spi_display.h"
#include "lv_conf.h"
#include "cs122_app.h"
#include "lvgl_touch.h"

#include <lvgl.h>

#include <stdio.h>
#include <pico/stdlib.h>
#include <pico/binary_info.h>
#include <pico/time.h>
#include <hardware/spi.h>
#include <pico/cyw43_arch.h>

/*Return the elapsed milliseconds since startup.
 *It needs to be implemented by the user*/
uint32_t cs122_get_millis(void) {
    return to_ms_since_boot(get_absolute_time());
}

static uint8_t buffer[OLEDRGB_WIDTH * OLEDRGB_HEIGHT / 10];

/*Copy the rendered image to the screen. */
void cs122_flush_cb_direct(lv_display_t * disp, const lv_area_t * area, uint8_t * px_buf) {
    ucr::bcoe::SPIDisplay *spi_display = reinterpret_cast<ucr::bcoe::SPIDisplay *>(lv_display_get_user_data(disp));
	uint32_t i = 0;
	for (uint32_t y = area->y1; y <= area->y2; y++) {
		for(uint32_t x = area->x1; x <= area->x2; x++) {
			uint32_t px_buf_idx = x * 2 + y * (spi_display->getWidth() * 2);
		    buffer[i++] =  (px_buf[px_buf_idx+1] & 0xE0) | ((px_buf[px_buf_idx+1] & 0x7) << 2) | (px_buf[px_buf_idx] & 0x1f) >> 3;
		}
	}

    /*Show the rendered image on the display*/
    spi_display->drawBitmap(area->x1, area->y1, area->x2, area->y2, buffer);

    /*Indicate that the buffer is available.
     *If DMA were used, call in the DMA complete interrupt*/
    lv_display_flush_ready(disp);
}

/*It needs to be implemented by the user*/
void cs122_flush_cb_partial(lv_display_t * disp, const lv_area_t * area, uint8_t * px_buf) {
	uint32_t size = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);

    /*Show the rendered image on the display*/
    ucr::bcoe::SPIDisplay *spi_display = reinterpret_cast<ucr::bcoe::SPIDisplay *>(lv_display_get_user_data(disp));
    spi_display->drawBitmap(2 * area->x1, area->y1, 2 * area->x2+1, area->y2, px_buf);

    /*Indicate that the buffer is available.
     *If DMA were used, call in the DMA complete interrupt*/
    lv_display_flush_ready(disp);
}

typedef struct task {
    signed char state;
    unsigned long period;
    unsigned long elapsedTime;
    int (*TickFct)(int);
} task;

const unsigned long GCD_PERIOD = 50;

enum UI_States {UI_START,UI_RUN};

int TickFct_UI(int state);

task tasks[1] = {
    { UI_START, 50, 50, &TickFct_UI }
};

ucr::bcoe::SPIDisplay* g_display = nullptr;
ucr::bcoe::cs::cs122::CS122_App* g_app = nullptr;

int main()
{
    stdio_init_all();
    cyw43_arch_init();
    adc_init();

    g_display = new ucr::bcoe::SPIDisplay(480,272,10000000,20);

    g_display->begin();
    g_display->clear();

    g_app = new ucr::bcoe::cs::cs122::CS122_App(g_display,cs122_flush_cb_partial,cs122_get_millis);

    while(true)
    {
        for(unsigned int i = 0; i < 1; i++)
        {
            if(tasks[i].elapsedTime >= tasks[i].period)
            {
                tasks[i].state =
                    tasks[i].TickFct(tasks[i].state);

                tasks[i].elapsedTime = 0;
            }

            tasks[i].elapsedTime += GCD_PERIOD;
        }
        sleep_ms(GCD_PERIOD);
    }
}

int TickFct_UI(int state)
{
    switch(state)
    {
        case UI_START:
            g_app->init();
            state = UI_RUN;
            break;

        case UI_RUN:
            g_app->update();
            break;
    }

    return state;
}