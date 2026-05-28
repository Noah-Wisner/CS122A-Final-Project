#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/adc.h"

#include "FreeRTOS.h"
#include "task.h"

//#include "menu_manager.h"

#include "spi_display.h"
#include "lv_conf.h"
#include "lvgl_clock.h"
#include "lvgl_touch.h"

#include <lvgl.h>

#include <pico/time.h>
#include <pico/cyw43_arch.h>

//LVGL Functions
static uint8_t buffer[OLEDRGB_WIDTH * OLEDRGB_HEIGHT / 10];

uint32_t cs122_get_millis(void)
{
    return to_ms_since_boot(get_absolute_time());
}

void cs122_flush_cb_partial(lv_display_t * disp,const lv_area_t * area,uint8_t * px_buf)
{
    ucr::bcoe::SPIDisplay *spi_display =reinterpret_cast<ucr::bcoe::SPIDisplay *>(lv_display_get_user_data(disp));

    spi_display->drawBitmap(2 * area->x1,area->y1,2 * area->x2 + 1,area->y2,px_buf);

    lv_display_flush_ready(disp);
}

//Tasks
#define LED_PIN 16

void ledTask(void *pvParameters)
{
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    while (true)
    {
        gpio_put(LED_PIN, 1);
        printf("LED on\n");

        vTaskDelay(pdMS_TO_TICKS(500));

        gpio_put(LED_PIN, 0);
        printf("LED off\n");

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void spiTask(void *pvParameters)
{
    while (true)
    {
        printf("SPI task running\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void canTask(void *pvParameters)
{
    while (true)
    {
        printf("CAN task running\n");
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

void sensorTask(void *pvParameters)
{
    while (true)
    {
        printf("Sensor task running\n");
        vTaskDelay(pdMS_TO_TICKS(2500));
    }
}

//UI Task for LVGL
void UIrenderTask(void *pvParameters)
{
    printf("Initializing UI...\n");

    adc_init();

    static ucr::bcoe::SPIDisplay spi_display(480,272,10000000,20);

    spi_display.begin();
    spi_display.clear();

    touch_init(26,21,27,22);

    static ucr::bcoe::cs::cs122::LVGL_Clock app(
        &spi_display,
        cs122_flush_cb_partial,
        cs122_get_millis
    );

    app.run();

    vTaskDelete(NULL);
}

void prvSetupHardware(void)
{
    stdio_init_all();

    if (cyw43_arch_init())
    {
        printf("CYW43 init failed\n");
    }
}

int main()
{
    prvSetupHardware();

    BaseType_t result;

    result = xTaskCreate(ledTask,"LED Task", 1024, NULL, 1, NULL);

    if (result != pdPASS)
    {
        printf("Failed to create LED task\n");
    }

    result = xTaskCreate(spiTask,"SPI Task",1024,NULL,3,NULL);

    if (result != pdPASS)
    {
        printf("Failed to create SPI task\n");
    }

    result = xTaskCreate(canTask,"CAN Task",1024,NULL,4,NULL);

    if (result != pdPASS)
    {
        printf("Failed to create CAN task\n");
    }

    result = xTaskCreate(sensorTask,"Sensor Task",1024,NULL,3,NULL);

    if (result != pdPASS)
    {
        printf("Failed to create sensor task\n");
    }

    // result = xTaskCreate(UIrenderTask,"UI Render Task",4096,NULL,2,NULL);

    // if (result != pdPASS)
    // {
    //     printf("Failed to create UI task\n");
    // }

    printf("Starting scheduler...\n");

    vTaskStartScheduler();

    while (true)
    {
    }

    return 0;
}