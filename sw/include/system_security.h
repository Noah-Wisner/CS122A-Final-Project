#pragma once
#include <stdint.h>


#define NUM_ZONES 2
#define EVENT_QUEUE_SIZE 16
#define MAX_LOG_ENTRIES 20

enum SystemState
{
    DISARMED,
    ARMED,
    ALERT,
    FAULT
};

enum FaultType
{
    FAULT_NONE,
    FAULT_HEARTBEAT_TIMEOUT,
    FAULT_SENSOR_LOSS
};

struct ZoneStatus
{
    bool door_open = false;
    bool motion = false;
    bool high_temp = false;
    bool low_temp = false;
    bool high_hum = false;
    bool alive = true;
    uint32_t lastHeartbeat = 0;
    FaultType fault = FAULT_NONE;
};

struct LogEntry
{
    uint32_t timestamp;
    char message[64];
};

struct SecuritySystem
{
    SystemState state;

    bool alarmActive = false;

    ZoneStatus zones[NUM_ZONES];

    LogEntry eventLog[MAX_LOG_ENTRIES];

    uint8_t logIndex;

    uint8_t logCount = 0;

    uint8_t alertCount = 0;
    
};

uint8_t GetLastLogEntries(LogEntry *out, uint8_t max_count);