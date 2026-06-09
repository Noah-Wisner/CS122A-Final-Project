#pragma once
#include <stdint.h>

typedef enum {
    SENSOR_TEMP,
    SENSOR_ULTRASONIC,
    SENSOR_BUTTON
} SensorType;

typedef struct {
    SensorType type;
    uint16_t value;
} SensorEvent;