#include <stdio.h>

#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "task.h"

#include "tasks.h"
#include "queues.h"
#include "spi_fpga.h"

int main() {

    stdio_init_all();

    queues_init();

    spi_fpga_init();

    xTaskCreate(
        SensorTask,
        "SensorTask",
        256,
        NULL,
        1,
        NULL
    );

    xTaskCreate(StateTask,"StateTask",256,NULL,2,NULL);

    vTaskStartScheduler();

    while (1);
}