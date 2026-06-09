// ============================================================
//  main.cpp
//
//  Master Security Controller
//
//  Owns:
//    - MCP2515 CAN Receive
//    - Event Queue
//    - Security State Machine
//    - Logger
//    - UI Stub
//    - Touch Stub
//    - Scheduler Task Table
//
// ============================================================

#include "scheduler.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <pico/stdlib.h>
#include <pico/time.h>
#include <hardware/spi.h>

// ============================================================
// MCP2515 / CAN Definitions
// ============================================================

#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19
#define PIN_INT  20

#define MCP_RESET  0xC0
#define MCP_WRITE  0x02
#define MCP_READ   0x03
#define MCP_BITMOD 0x05

#define CANCTRL  0x0F
#define CANSTAT  0x0E
#define CNF3     0x28
#define CNF2     0x29
#define CNF1     0x2A
#define CANINTE  0x2B
#define CANINTF  0x2C
#define RXB0CTRL 0x60
#define RXB0SIDH 0x61
#define RXB0SIDL 0x62
#define RXB0DLC  0x65
#define RXB0D0   0x66
#define RXB1CTRL 0x70
#define RXB1SIDH 0x71
#define RXB1SIDL 0x72
#define RXB1DLC  0x75
#define RXB1D0   0x76

#define CAN_ID_BUTTON  0x050
#define CAN_ID_SENSORS 0x100

// ============================================================
// Security System Definitions
// ============================================================

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

struct ZoneStatus
{
    bool online = false;

    bool motionDetected = false;

    bool doorOpen = false;

    float temperature = 0.0f;

    uint32_t lastHeartbeat = 0;
};

struct SecuritySystem
{
    SystemState state = DISARMED;

    bool alarmActive = false;

    ZoneStatus zones[NUM_ZONES];
};

SecuritySystem g_system;

// ============================================================
// Event Queue
// ============================================================

enum EventType
{
    EVENT_NONE,

    EVENT_ARM,
    EVENT_DISARM,

    EVENT_ZONE_TRIGGERED,

    EVENT_HEARTBEAT_TIMEOUT,

    EVENT_ALERT_ACK
};

struct Event
{
    EventType type;

    uint8_t zone;
};

static Event g_eventQueue[EVENT_QUEUE_SIZE];

static uint8_t g_eventHead = 0;
static uint8_t g_eventTail = 0;

bool PushEvent(Event e)
{
    uint8_t next =
        (g_eventTail + 1) % EVENT_QUEUE_SIZE;

    if (next == g_eventHead)
        return false;

    g_eventQueue[g_eventTail] = e;

    g_eventTail = next;

    return true;
}

bool PopEvent(Event* e)
{
    if (g_eventHead == g_eventTail)
        return false;

    *e = g_eventQueue[g_eventHead];

    g_eventHead =
        (g_eventHead + 1) % EVENT_QUEUE_SIZE;

    return true;
}

// ============================================================
// Logger
// ============================================================

struct LogEntry
{
    uint32_t timestamp;

    char message[64];
};

static LogEntry g_logs[MAX_LOG_ENTRIES];

static uint8_t g_logIndex = 0;

void AddLog(const char* msg)
{
    g_logs[g_logIndex].timestamp =
        scheduler_millis();

    snprintf(
        g_logs[g_logIndex].message,
        sizeof(g_logs[g_logIndex].message),
        "%s",
        msg
    );

    g_logIndex++;

    if (g_logIndex >= MAX_LOG_ENTRIES)
        g_logIndex = 0;
}



// ============================================================
// MCP2515 Helpers
//
// COPY YOUR EXISTING IMPLEMENTATIONS HERE
// ============================================================

static inline void cs_select()   { gpio_put(PIN_CS, 0); }
static inline void cs_deselect() { gpio_put(PIN_CS, 1); }

static void mcp_reset() {
    uint8_t cmd = MCP_RESET;
    cs_select();
    spi_write_blocking(SPI_PORT, &cmd, 1);
    cs_deselect();
    sleep_ms(10);
}

static uint8_t mcp_read_reg(uint8_t addr) {
    uint8_t tx[3] = {MCP_READ, addr, 0x00};
    uint8_t rx[3] = {0};
    cs_select();
    spi_write_read_blocking(SPI_PORT, tx, rx, 3);
    cs_deselect();
    return rx[2];
}

static void mcp_write_reg(uint8_t addr, uint8_t data) {
    uint8_t tx[3] = {MCP_WRITE, addr, data};
    cs_select();
    spi_write_blocking(SPI_PORT, tx, 3);
    cs_deselect();
}

static void mcp_bit_modify(uint8_t addr, uint8_t mask, uint8_t data) {
    uint8_t tx[4] = {MCP_BITMOD, addr, mask, data};
    cs_select();
    spi_write_blocking(SPI_PORT, tx, 4);
    cs_deselect();
}

static void mcp_init_500kbps_8mhz() {
    mcp_reset();
    mcp_write_reg(CNF1, 0x00);
    mcp_write_reg(CNF2, 0xD0);
    mcp_write_reg(CNF3, 0x02);
    mcp_write_reg(CANINTE, 0x00);
    mcp_write_reg(RXB0CTRL, 0x68);
    mcp_write_reg(RXB1CTRL, 0x60);
    mcp_bit_modify(CANCTRL, 0xE0, 0x00);
    sleep_ms(10);
}

// ============================================================
// CAN Frame Processing
//
// COPY YOUR EXISTING FUNCTION HERE
// ============================================================


struct CANData {
    uint16_t dist_cm      = 0;
    int16_t  temp_x10     = 0;
    uint16_t hum_x10      = 0;
    bool     button_state = false;
};
CANData g_can;

static void can_process_frame(uint8_t sidh, uint8_t sidl,
                              uint8_t dlc,  uint8_t base_reg)
{
    uint16_t id = ((uint16_t)sidh << 3) | (sidl >> 5);
    dlc &= 0x0F;

    uint8_t d[8] = {0};
    for (int i = 0; i < dlc; i++)
        d[i] = mcp_read_reg(base_reg + i);

    if (id == CAN_ID_BUTTON && dlc >= 1) {
        g_can.button_state = (d[0] == 0x01);
        printf("Button: %s\n", g_can.button_state ? "PRESSED" : "released");

    } else if (id == CAN_ID_SENSORS && dlc >= 6) {
        g_can.dist_cm  = ((uint16_t)d[0] << 8) | d[1];
        g_can.temp_x10 = (int16_t)(((uint16_t)d[2] << 8) | d[3]);
        g_can.hum_x10  = ((uint16_t)d[4] << 8) | d[5];

        // Throttle serial output to ~1 Hz (bus runs at 10 Hz)
        static uint32_t last_print = 0;
        uint32_t now = scheduler_millis();
        if (now - last_print >= 1000) {
            printf("Dist: %u cm  Temp: %.1f C  Hum: %.1f%%\n",
                   g_can.dist_cm,
                   g_can.temp_x10 / 10.0f,
                   g_can.hum_x10  / 10.0f);
            last_print = now;
        }
    }
}
// ============================================================
// TASK: CAN
// ============================================================


enum CANState { CAN_INIT, CAN_POLL };

int TickCAN(int state) {
    switch (state) {

    case CAN_INIT:
        // Hardware is already configured in main() before the
        // scheduler starts. Transition straight to polling.
        state = CAN_POLL;
        break;

    case CAN_POLL:
        {
            uint8_t intf = mcp_read_reg(CANINTF);

            if (intf & 0x01) {                          // RXB0 has a frame
                can_process_frame(mcp_read_reg(RXB0SIDH),
                                  mcp_read_reg(RXB0SIDL),
                                  mcp_read_reg(RXB0DLC),
                                  RXB0D0);
                mcp_bit_modify(CANINTF, 0x01, 0x00);    // clear RX0IF
            }
            if (intf & 0x02) {                          // RXB1 has a frame
                can_process_frame(mcp_read_reg(RXB1SIDH),
                                  mcp_read_reg(RXB1SIDL),
                                  mcp_read_reg(RXB1DLC),
                                  RXB1D0);
                mcp_bit_modify(CANINTF, 0x02, 0x00);    // clear RX1IF
            }
        }
        break;

    default:
        state = CAN_INIT;
        break;
    }
    return state;
}

// ============================================================
// TASK: System State Machine
// ============================================================

enum SystemTaskState
{
    SYSTEM_INIT,
    SYSTEM_RUN
};

int TickSystem(int state)
{
    Event event;

    switch(state)
    {
        case SYSTEM_INIT:

            g_system.state = DISARMED;

            g_system.alarmActive = false;

            AddLog("System Initialized");

            state = SYSTEM_RUN;

            break;

        case SYSTEM_RUN:

            while(PopEvent(&event))
            {
                switch(event.type)
                {
                    case EVENT_ARM:

                        g_system.state = ARMED;

                        AddLog("System Armed");

                        break;

                    case EVENT_DISARM:

                        g_system.state = DISARMED;

                        g_system.alarmActive = false;

                        AddLog("System Disarmed");

                        break;

                    case EVENT_ZONE_TRIGGERED:

                        if(g_system.state == ARMED)
                        {
                            g_system.state = ALERT;

                            g_system.alarmActive = true;

                            AddLog("Zone Triggered");
                        }

                        break;

                    case EVENT_HEARTBEAT_TIMEOUT:

                        g_system.state = FAULT;

                        AddLog("Heartbeat Timeout");

                        break;

                    case EVENT_ALERT_ACK:

                        g_system.alarmActive = false;

                        AddLog("Alert Acknowledged");

                        break;

                    default:
                        break;
                }
            }

            break;

        default:

            state = SYSTEM_INIT;
            break;
    }

    return state;
}

// ============================================================
// TASK: Logger
// ============================================================

enum LoggerTaskState
{
    LOGGER_INIT,
    LOGGER_RUN
};

int TickLogger(int state)
{
    switch(state)
    {
        case LOGGER_INIT:

            state = LOGGER_RUN;
            break;

        case LOGGER_RUN:

            /*
                Future:
                SD Card
                Flash Storage
                UART Export
            */

            break;

        default:

            state = LOGGER_INIT;
            break;
    }

    return state;
}

// ============================================================
// TASK: Touch
// ============================================================

enum TouchTaskState
{
    TOUCH_INIT,
    TOUCH_RUN
};

int TickTouch(int state)
{
    switch(state)
    {
        case TOUCH_INIT:

            state = TOUCH_RUN;
            break;

        case TOUCH_RUN:

            /*
                TODO:

                ARM button
                DISARM button
                ACK button

                PushEvent(EVENT_ARM)
                PushEvent(EVENT_DISARM)
            */

            break;
    }

    return state;
}

// ============================================================
// TASK: UI
// ============================================================

enum UIState
{
    UI_INIT,
    UI_RUN
};

int TickUI(int state)
{
    switch(state)
    {
        case UI_INIT:

            state = UI_RUN;
            break;

        case UI_RUN:

            /*
                TODO:

                Update LVGL widgets

                Read:

                g_system.state
                g_system.zones[]
                g_logs[]
            */

            break;
    }

    return state;
}

// ============================================================
// Timing
// ============================================================

static const uint32_t GCD_PERIOD_MS = 10;

static const uint32_t CAN_PERIOD_MS = 10;

static const uint32_t SYSTEM_PERIOD_MS = 10;

static const uint32_t LOGGER_PERIOD_MS = 50;

static const uint32_t TOUCH_PERIOD_MS = 20;

static const uint32_t UI_PERIOD_MS = 50;

// ============================================================
// Main
// ============================================================

int main()
{
    stdio_init_all();

    while(!stdio_usb_connected())
    {
        sleep_ms(100);
    }

    printf("Booting...\n");

    // ========================================================
    // Hardware Init
    // ========================================================

    spi_init(SPI_PORT, 1000000);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    

    cs_deselect();

    gpio_init(PIN_INT);
    gpio_set_dir(PIN_INT, GPIO_IN);

    gpio_pull_up(PIN_INT);

    mcp_init_500kbps_8mhz();

    printf("CANSTAT = 0x%02X\n", mcp_read_reg(CANSTAT));
    printf("CANCTRL = 0x%02X\n", mcp_read_reg(CANCTRL));

    // ========================================================
    // Task Table
    // ========================================================

    Task tasks[] =
    {
        { CAN_INIT,      CAN_PERIOD_MS,      0, TickCAN },

        { SYSTEM_INIT,   SYSTEM_PERIOD_MS,   0, TickSystem },

        { LOGGER_INIT,   LOGGER_PERIOD_MS,   0, TickLogger },

        { TOUCH_INIT,    TOUCH_PERIOD_MS,    0, TickTouch },

        { UI_INIT,       UI_PERIOD_MS,       0, TickUI }
    };

    const size_t NUM_TASKS =
        sizeof(tasks) / sizeof(tasks[0]);

    scheduler_init(GCD_PERIOD_MS);

    printf(
        "Scheduler running - %u tasks\n",
        (unsigned)NUM_TASKS
    );

    while(true)
    {
        scheduler_run(tasks, NUM_TASKS);

        /*
            Future:

            LVGL Tick

            app.run_once();
        */
    }
}