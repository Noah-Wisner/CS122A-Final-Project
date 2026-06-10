// ============================================================
//  main.cpp
//
//  Master Security Controller
//
//  Owns:
//    - MCP2515 CAN Receive
//    - Event Queue (with EVENT_ZONE_CLEARED)
//    - Security State Machine
//    - Heartbeat Watchdog
//    - Logger
//    - UI Stub
//    - Touch Stub
//    - Buzzer Task
//    - Scheduler Task Table
//
// ============================================================

#include "scheduler.h"
#include "camera_task.h"

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

#define SPI_PORT spi1   // was spi0
#define PIN_MISO 8      // was 16
#define PIN_CS   9      // was 17
#define PIN_SCK  10     // was 18
#define PIN_MOSI 11     // was 19
#define PIN_INT  12     // was 20
#define PIN_BUZZER 14

// Zone 0 is mapped to the sensor node on CAN_ID_FLAGS(0).
// Extend NUM_ZONES and add nodes for additional zones.
#define HEARTBEAT_TIMEOUT_MS    3000u   // zone offline if HB silent this long

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

#define CAN_ID_BUTTON       0x050
#define CAN_ID_FLAGS(z)     (0x100 + ((z) * 0x10))  // flag byte frame per zone
#define CAN_ID_HB(z)        (0x1A0 + ((z) * 0x10))  // dedicated heartbeat per zone

// Flag bits — must match the transmitter node exactly
#define FLAG_DOOR_OPEN  (1 << 0)
#define FLAG_MOTION     (1 << 1)
#define FLAG_HIGH_TEMP  (1 << 2)
#define FLAG_LOW_TEMP   (1 << 3)
#define FLAG_HIGH_HUM   (1 << 4)

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

// --------------------------------------------------------
// Fault reason — travels with the zone it describes
// --------------------------------------------------------

enum FaultType
{
    FAULT_NONE,
    FAULT_HEARTBEAT_TIMEOUT,
    FAULT_SENSOR_LOSS
};

struct ZoneStatus
{
    bool      door_open      = false;
    bool      motion         = false;
    bool      high_temp      = false;
    bool      low_temp       = false;
    bool      high_hum       = false;
    bool      alive          = true;
    bool      everSeen       = false;   // true once the first HB/flags frame arrives
    uint32_t  lastHeartbeat  = 0;
    FaultType fault          = FAULT_NONE;  // why it went offline
};

// --------------------------------------------------------
// Log entry — defined here so SecuritySystem can own the array
// --------------------------------------------------------

struct LogEntry
{
    uint32_t timestamp;
    char     message[64];
};

struct SecuritySystem
{
    SystemState state = DISARMED;

    bool alarmActive = false;

    ZoneStatus zones[NUM_ZONES];

    LogEntry eventLog[MAX_LOG_ENTRIES];  // owned here; AddLog() writes into it
    uint8_t  logIndex = 0;
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
    EVENT_ZONE_CLEARED,         // motion gone / door closed — zone back to normal

    EVENT_HEARTBEAT_TIMEOUT,
    EVENT_ZONE_RECOVERED,       // faulted zone came back online

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

// NOTE: LogEntry / g_logs removed — log is owned by SecuritySystem.eventLog.
//       AddLog() writes there directly so the UI/logger tasks have one
//       authoritative source to read from.

void AddLog(const char* msg)
{
    uint8_t idx = g_system.logIndex;

    g_system.eventLog[idx].timestamp = scheduler_millis();

    snprintf(
        g_system.eventLog[idx].message,
        sizeof(g_system.eventLog[idx].message),
        "%s",
        msg
    );

    g_system.logIndex++;

    if (g_system.logIndex >= MAX_LOG_ENTRIES)
        g_system.logIndex = 0;
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


// Per-zone raw flag shadow — one byte per zone, mirrors last received flags frame.
// Useful for debug; g_system.zones[] is the authoritative state for logic.
static uint8_t g_zoneFlags[NUM_ZONES] = {0};

// Helper: bring a zone online, handling first-boot vs recovery
static void zone_mark_online(uint8_t z)
{
    ZoneStatus& zone = g_system.zones[z];

    zone.everSeen = true;   // mark seen on every contact

    if (!zone.alive)
    {
        zone.alive = true;

        if (zone.fault != FAULT_NONE)
        {
            zone.fault = FAULT_NONE;
            PushEvent({ EVENT_ZONE_RECOVERED, z });

            char buf[64];
            snprintf(buf, sizeof(buf), "Zone %u recovered", z);
            AddLog(buf);
        }
        else
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "Zone %u online", z);
            AddLog(buf);
        }
    }
}

static void can_process_frame(uint8_t sidh, uint8_t sidl,
                              uint8_t dlc,  uint8_t base_reg)
{
    uint16_t id = ((uint16_t)sidh << 3) | (sidl >> 5);
    dlc &= 0x0F;

    uint8_t d[8] = {0};
    for (int i = 0; i < dlc; i++)
        d[i] = mcp_read_reg(base_reg + i);

    // --------------------------------------------------------
    // CAN_ID_BUTTON (0x050)
    //   Byte 0: 0x01 = pressed, 0x00 = released
    //   Press toggles ARM <-> DISARM. Only rising edge acts.
    // --------------------------------------------------------
    if (id == CAN_ID_BUTTON && dlc >= 1)
    {
        bool pressed = (d[0] == 0x01);

        printf("Button: %s\n", pressed ? "PRESSED" : "released");

        if (pressed)
        {
            if (g_system.state == DISARMED)
                PushEvent({ EVENT_ARM, 0 });
            else if (g_system.state == ARMED || g_system.state == ALERT)
                PushEvent({ EVENT_DISARM, 0 });
        }
        return;
    }

    // --------------------------------------------------------
    // CAN_ID_FLAGS(z)  (0x100, 0x110, 0x120, ...)
    //   Byte 0: flag bitmask
    //     Bit 0 FLAG_DOOR_OPEN  — door sensor
    //     Bit 1 FLAG_MOTION     — ultrasonic threshold crossed
    //     Bit 2 FLAG_HIGH_TEMP  — temp > threshold
    //     Bit 3 FLAG_LOW_TEMP   — temp < threshold
    //     Bit 4 FLAG_HIGH_HUM   — humidity > threshold
    //
    //   The sensor node pre-computes these; master just reacts.
    //   NOTE: flags frame does NOT reset the heartbeat timer —
    //         only the dedicated HB frame does.
    // --------------------------------------------------------
    for (uint8_t z = 0; z < NUM_ZONES; z++)
    {
        if (id == CAN_ID_FLAGS(z) && dlc >= 1)
        {
            uint8_t      flags    = d[0];
            ZoneStatus&  zone     = g_system.zones[z];

            zone_mark_online(z);

            // --- Cache raw flags for debug ---
            g_zoneFlags[z] = flags;

            // --- Door ---
            bool doorNow  = (flags & FLAG_DOOR_OPEN) != 0;
            bool prevDoor = zone.door_open;
            zone.door_open = doorNow;

            if (doorNow && !prevDoor)
            {
                PushEvent({ EVENT_ZONE_TRIGGERED, z });
                char buf[64];
                snprintf(buf, sizeof(buf), "Zone %u: door opened", z);
                AddLog(buf);
            }
            else if (!doorNow && prevDoor)
            {
                PushEvent({ EVENT_ZONE_CLEARED, z });
                char buf[64];
                snprintf(buf, sizeof(buf), "Zone %u: door closed", z);
                AddLog(buf);
            }

            // --- Motion ---
            bool motionNow  = (flags & FLAG_MOTION) != 0;
            bool prevMotion = zone.motion;
            zone.motion = motionNow;

            if (motionNow && !prevMotion)
            {
                PushEvent({ EVENT_ZONE_TRIGGERED, z });
                char buf[64];
                snprintf(buf, sizeof(buf), "Zone %u: motion detected", z);
                AddLog(buf);
            }
            else if (!motionNow && prevMotion)
            {
                PushEvent({ EVENT_ZONE_CLEARED, z });
                char buf[64];
                snprintf(buf, sizeof(buf), "Zone %u: motion cleared", z);
                AddLog(buf);
            }

            // --- Environmental flags (threshold crossed — informational) ---
            zone.high_temp = (flags & FLAG_HIGH_TEMP) != 0;
            zone.low_temp  = (flags & FLAG_LOW_TEMP)  != 0;
            zone.high_hum  = (flags & FLAG_HIGH_HUM)  != 0;

            // --- Throttled debug ---
            static uint32_t last_print[NUM_ZONES] = {0};
            uint32_t now = scheduler_millis();
            if (now - last_print[z] >= 1000)
            {
                printf("Z%u flags=0x%02X | Door:%s Motion:%s HiTemp:%s LoTemp:%s HiHum:%s\n",
                       z, flags,
                       (flags & FLAG_DOOR_OPEN) ? "Y" : "n",
                       (flags & FLAG_MOTION)    ? "Y" : "n",
                       (flags & FLAG_HIGH_TEMP) ? "Y" : "n",
                       (flags & FLAG_LOW_TEMP)  ? "Y" : "n",
                       (flags & FLAG_HIGH_HUM)  ? "Y" : "n");
                last_print[z] = now;
            }
            return;
        }
    }

    // --------------------------------------------------------
    // CAN_ID_HB(z)  (0x1A0, 0x1B0, 0x1C0, ...)
    //   Dedicated heartbeat frame sent every ~1 s by the node.
    //   Payload is a single 0x00 byte — only the ID matters.
    //   This is the ONLY frame that resets lastHeartbeat.
    // --------------------------------------------------------
    for (uint8_t z = 0; z < NUM_ZONES; z++)
    {
        if (id == CAN_ID_HB(z))
        {
            zone_mark_online(z);
            g_system.zones[z].lastHeartbeat = scheduler_millis();
            printf("HB received: Zone %u  t=%lu ms\n",
                   z, (unsigned long)scheduler_millis());
            return;
        }
    }

    // --------------------------------------------------------
    // Unknown frame — log ID so we can diagnose mismatches
    // --------------------------------------------------------
    printf("Unknown CAN ID: 0x%03X  dlc=%u\n", id, dlc);
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
// TASK: Heartbeat Watchdog
//
// Runs every WATCHDOG_PERIOD_MS. For every zone that has ever
// come online, checks whether lastHeartbeat (stamped only by
// the dedicated CAN_ID_HB frame) is stale beyond
// HEARTBEAT_TIMEOUT_MS. Pushes EVENT_HEARTBEAT_TIMEOUT once
// per fault — the zone.fault guard prevents repeat fires.
// ============================================================

enum WatchdogTaskState { WD_INIT, WD_RUN };

int TickWatchdog(int state)
{
    switch (state)
    {
        case WD_INIT:
            state = WD_RUN;
            break;

        case WD_RUN:
        {
            uint32_t now = scheduler_millis();

            for (uint8_t z = 0; z < NUM_ZONES; z++)
            {
                ZoneStatus& zone = g_system.zones[z];

                // Only watch zones that have checked in at least once
                if (!zone.everSeen)
                    continue;

                bool timedOut = (now - zone.lastHeartbeat) > HEARTBEAT_TIMEOUT_MS;

                if (timedOut && zone.fault == FAULT_NONE)
                {
                    // First detection — record fault reason on the zone itself
                    zone.alive  = false;
                    zone.fault  = FAULT_HEARTBEAT_TIMEOUT;

                    PushEvent({ EVENT_HEARTBEAT_TIMEOUT, z });

                    char buf[64];
                    snprintf(buf, sizeof(buf),
                             "Zone %u: heartbeat timeout", z);
                    AddLog(buf);
                }
            }
            break;
        }

        default:
            state = WD_INIT;
            break;
    }

    return state;
}



enum SystemTaskState
{
    SYSTEM_INIT,
    SYSTEM_RUN
};

// Helper: returns true if any zone still has an active trigger
static bool AnyZoneTriggered()
{
    for (uint8_t z = 0; z < NUM_ZONES; z++)
    {
        if (g_system.zones[z].motion ||
            g_system.zones[z].door_open)
            return true;
    }
    return false;
}

// Helper: returns true if at least one zone is currently faulted
static bool AnyZoneFaulted()
{
    for (uint8_t z = 0; z < NUM_ZONES; z++)
    {
        if (g_system.zones[z].fault != FAULT_NONE)
            return true;
    }
    return false;
}

int TickSystem(int state)
{
    Event event;
    char  buf[64];

    switch(state)
    {
        case SYSTEM_INIT:

            g_system.state       = DISARMED;
            g_system.alarmActive = false;

            AddLog("System Initialized");

            state = SYSTEM_RUN;
            break;

        case SYSTEM_RUN:

            while(PopEvent(&event))
            {
                switch(event.type)
                {
                    // ----------------------------------------
                    case EVENT_ARM:

                        if (g_system.state == DISARMED)
                        {
                            g_system.state = ARMED;
                            AddLog("System Armed");
                        }
                        break;

                    // ----------------------------------------
                    case EVENT_DISARM:

                        g_system.state       = DISARMED;
                        g_system.alarmActive = false;

                        // Clear all zone trigger flags on disarm
                        for (uint8_t z = 0; z < NUM_ZONES; z++)
                        {
                            g_system.zones[z].motion    = false;
                            g_system.zones[z].door_open = false;
                        }

                        AddLog("System Disarmed");
                        break;

                    // ----------------------------------------
                    case EVENT_ZONE_TRIGGERED:

                        if (g_system.state == ARMED)
                        {
                            g_system.state       = ALERT;
                            g_system.alarmActive = true;

                            snprintf(buf, sizeof(buf),
                                     "ALERT: Zone %u triggered", event.zone);
                            AddLog(buf);
                        }
                        else if (g_system.state == DISARMED)
                        {
                            // Log quietly — no alarm while disarmed
                            snprintf(buf, sizeof(buf),
                                     "Zone %u activity (disarmed)", event.zone);
                            AddLog(buf);
                        }
                        break;

                    // ----------------------------------------
                    case EVENT_ZONE_CLEARED:

                        snprintf(buf, sizeof(buf),
                                 "Zone %u cleared", event.zone);
                        AddLog(buf);

                        // If in ALERT and ALL zones are now clear, step back
                        // to ARMED so a fresh trigger can be distinguished.
                        // Alarm stays active until the user ACKs.
                        if (g_system.state == ALERT && !AnyZoneTriggered())
                        {
                            g_system.state = ARMED;
                            AddLog("All zones clear — returning to ARMED");
                        }
                        break;

                    // ----------------------------------------
                    case EVENT_HEARTBEAT_TIMEOUT:

                        // Zone is already marked offline by TickWatchdog.
                        // Only escalate to system FAULT if every zone is down;
                        // a single zone fault leaves the rest of the system
                        // operational.
                        snprintf(buf, sizeof(buf),
                                 "FAULT: Zone %u heartbeat lost", event.zone);
                        AddLog(buf);

                        {
                            bool allFaulted = true;
                            for (uint8_t z = 0; z < NUM_ZONES; z++)
                            {
                                if (g_system.zones[z].alive)
                                {
                                    allFaulted = false;
                                    break;
                                }
                            }
                            if (allFaulted)
                            {
                                g_system.state = FAULT;
                                AddLog("All zones faulted — system FAULT");
                            }
                        }
                        break;

                    // ----------------------------------------
                    case EVENT_ZONE_RECOVERED:

                        snprintf(buf, sizeof(buf),
                                 "Zone %u recovered", event.zone);
                        AddLog(buf);

                        // If the system was in FAULT and no zones are faulted
                        // anymore, recover to DISARMED. Force disarm so the
                        // operator makes a deliberate decision to re-arm.
                        if (g_system.state == FAULT && !AnyZoneFaulted())
                        {
                            g_system.state       = DISARMED;
                            g_system.alarmActive = false;

                            for (uint8_t z = 0; z < NUM_ZONES; z++)
                            {
                                g_system.zones[z].motion    = false;
                                g_system.zones[z].door_open = false;
                            }

                            AddLog("Fault cleared — system DISARMED");
                        }
                        break;

                    // ----------------------------------------
                    case EVENT_ALERT_ACK:

                        g_system.alarmActive = false;

                        if (g_system.state == ALERT)
                            g_system.state = ARMED;

                        AddLog("Alert Acknowledged");
                        break;

                    // ----------------------------------------
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
// TASK: Buzzer  (Output Layer)
//
// Drives PIN_BUZZER (GPIO 14, active buzzer) based on
// g_system.state and g_system.alarmActive:
//
//   DISARMED             → silent
//   ARMED (just armed)   → three short confirmation beeps, then silent
//   ALERT + alarmActive  → rapid beep every 200 ms (alarm sounding)
//   ALERT (alarm acked)  → silent (alarmActive is false after ACK)
//   FAULT                → slow beep every 1000 ms (maintenance alert)
// ============================================================

enum BuzzerTaskState
{
    BUZZER_INIT,
    BUZZER_RUN
};

// Confirmation beep sequence: 3 short beeps played once when ARMED.
// Each beep is 100 ms on / 100 ms off. Driven by a small counter so
// TickBuzzer doesn't block the scheduler.
static struct
{
    bool        active     = false;  // sequence in progress
    uint8_t     step       = 0;      // 0-5: on0,off0,on1,off1,on2,off2
    uint32_t    nextChange = 0;
} g_armBeep;

int TickBuzzer(int state)
{
    switch(state)
    {
        case BUZZER_INIT:

            gpio_init(PIN_BUZZER);
            gpio_set_dir(PIN_BUZZER, GPIO_OUT);
            gpio_put(PIN_BUZZER, 0);

            state = BUZZER_RUN;
            break;

        case BUZZER_RUN:
        {
            uint32_t    now          = scheduler_millis();
            SystemState sysState     = g_system.state;
            bool        alarmActive  = g_system.alarmActive;

            // --------------------------------------------------
            // Track state transitions to trigger confirmation beep
            // --------------------------------------------------
            static SystemState prevSysState = DISARMED;

            if (sysState == ARMED && prevSysState == DISARMED)
            {
                // Just armed — kick off confirmation beep sequence
                g_armBeep.active     = true;
                g_armBeep.step       = 0;
                g_armBeep.nextChange = now;
            }

            prevSysState = sysState;

            // --------------------------------------------------
            // ALERT: rapid beep every 200 ms while alarm active
            // --------------------------------------------------
            if (sysState == ALERT && alarmActive)
            {
                g_armBeep.active = false;   // cancel any pending arm beep

                static uint32_t lastAlertToggle = 0;
                if (now - lastAlertToggle >= 200)
                {
                    gpio_put(PIN_BUZZER, !gpio_get(PIN_BUZZER));
                    lastAlertToggle = now;
                }
                break;
            }

            // --------------------------------------------------
            // FAULT: slow beep every 1000 ms
            // --------------------------------------------------
            if (sysState == FAULT)
            {
                g_armBeep.active = false;   // cancel any pending arm beep

                static uint32_t lastFaultToggle = 0;
                if (now - lastFaultToggle >= 1000)
                {
                    gpio_put(PIN_BUZZER, !gpio_get(PIN_BUZZER));
                    lastFaultToggle = now;
                }
                break;
            }

            // --------------------------------------------------
            // ARMED confirmation beep sequence (3 beeps × 100 ms on/off)
            // --------------------------------------------------
            if (g_armBeep.active)
            {
                if (now >= g_armBeep.nextChange)
                {
                    // Even steps → buzzer ON, odd steps → buzzer OFF
                    gpio_put(PIN_BUZZER, (g_armBeep.step % 2 == 0) ? 1 : 0);
                    g_armBeep.step++;
                    g_armBeep.nextChange = now + 100;

                    if (g_armBeep.step >= 6)     // 3 on/off pairs complete
                    {
                        g_armBeep.active = false;
                        gpio_put(PIN_BUZZER, 0);
                    }
                }
                break;
            }

            // --------------------------------------------------
            // All other states (DISARMED, ARMED+idle) → silent
            // --------------------------------------------------
            gpio_put(PIN_BUZZER, 0);
            break;
        }

        default:
            state = BUZZER_INIT;
            break;
    }

    return state;
}



// ============================================================
// Timing
// ============================================================

static const uint32_t GCD_PERIOD_MS = 10;

static const uint32_t CAN_PERIOD_MS      = 10;
static const uint32_t SYSTEM_PERIOD_MS   = 10;
static const uint32_t WATCHDOG_PERIOD_MS = 10;
static const uint32_t LOGGER_PERIOD_MS   = 50;
static const uint32_t TOUCH_PERIOD_MS    = 20;
static const uint32_t UI_PERIOD_MS       = 50;
static const uint32_t BUZZER_PERIOD_MS   = 10;
static const uint32_t CAMERA_PERIOD_MS   = 3000;
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
        { CAN_INIT,      CAN_PERIOD_MS,      0, TickCAN      },
        { SYSTEM_INIT,   SYSTEM_PERIOD_MS,   0, TickSystem   },
        { WD_INIT,       WATCHDOG_PERIOD_MS, 0, TickWatchdog },
        { LOGGER_INIT,   LOGGER_PERIOD_MS,   0, TickLogger   },
        { TOUCH_INIT,    TOUCH_PERIOD_MS,    0, TickTouch    },
        { UI_INIT,       UI_PERIOD_MS,       0, TickUI       },
        { BUZZER_INIT,   BUZZER_PERIOD_MS,   0, TickBuzzer   },
        
    }; //{ CAMERA_INIT,   CAMERA_PERIOD_MS,   0, TickCamera   }, 

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