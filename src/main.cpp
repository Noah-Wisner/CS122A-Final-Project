#include <stdio.h>
#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "task.h"
#include "menu_manager.h"


#define LED_PIN 16

void ledTask(void *pvParameters)
{
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    while (true) {
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
    // SPI communication code would go here
    while (true) {
        // Simulate SPI work
        printf("SPI task running\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
void canTask(void *pvParameters)
{
    // CAN communication code would go here
    while (true) {
        // Simulate CAN work
        printf("CAN task running\n");
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}
void UIrenderTask(void *pvParameters)
{
    menuManager_init();

    while (true) {
        MenuState state = menuManager_getState();

        // Stub: just log — replace with lv_label_set_text() etc. later
        printf("[UI] Selected: %s  (open=%d)\n",
               menuManager_getItemLabel(state.selectedItem),
               state.isOpen);

        // TODO: call lv_task_handler() here once LVGL is wired up

        vTaskDelay(pdMS_TO_TICKS(33)); // ~30fps refresh budget
    }
}
void sensorTask(void *pvParameters)
{
    // Sensor reading code would go here
    while (true) {
        // Simulate sensor reading work
        printf("Sensor task running\n");
        vTaskDelay(pdMS_TO_TICKS(2500));
    }
}

void prvSetupHardware(void)
{
    stdio_init_all();
}

int main()
{
    prvSetupHardware();

    auto result = xTaskCreate(ledTask, "LED Task", 1024, NULL, 1, NULL);
    if (result != pdPASS) {
        printf("Failed to create LED task\n");
        return 1;
    }
    result = xTaskCreate(spiTask, "SPI Task", 1024, NULL, 3, NULL);
    if (result != pdPASS) {
        printf("Failed to create SPI task\n");
        return 1;
    }
    result = xTaskCreate(canTask, "CAN Task", 1024, NULL, 4, NULL);
    if (result != pdPASS) {
        printf("Failed to create CAN task\n");
        return 1;
    }
    result = xTaskCreate(UIrenderTask, "UI Render Task", 1024, NULL, 2, NULL);
    if (result != pdPASS) {
        printf("Failed to create UI render task\n");
        return 1;
    }
    result = xTaskCreate(sensorTask, "Sensor Task", 1024, NULL, 3, NULL);
    if (result != pdPASS) {
        printf("Failed to create sensor task\n");
        return 1;
    }
    vTaskStartScheduler(); // Does not return

    for (;;);
    return 0;
}