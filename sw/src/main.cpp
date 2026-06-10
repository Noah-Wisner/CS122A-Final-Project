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

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "spi_display.h"
#include "lv_conf.h"
#include "cs122_app.h"
#include "lvgl_touch.h"
#include <lvgl.h>

#include "system_security.h"

#include <pico/cyw43_arch.h>
#include <hardware/spi.h>

// ============================================================
// MCP2515 / CAN Definitions
// ============================================================

#define SPI_PORT spi1
#define PIN_MISO 8
#define PIN_CS   9
#define PIN_SCK  10
#define PIN_MOSI 11
#define PIN_INT  12
#define PIN_BUZZER 13

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

// Defining Display
ucr::bcoe::SPIDisplay* g_display = nullptr;
ucr::bcoe::cs::cs122::CS122_App* g_app = nullptr;

// ============================================================
// Security System Definitions
// ============================================================
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

void AddLog(const char *msg) {
    uint8_t idx = g_system.logIndex;
    g_system.eventLog[idx].timestamp = scheduler_millis();
    snprintf(g_system.eventLog[idx].message, sizeof(g_system.eventLog[idx].message), "%s", msg);
    g_system.logIndex = (idx + 1) % MAX_LOG_ENTRIES;   // circular wrap
    if (g_system.logCount < MAX_LOG_ENTRIES) g_system.logCount++;
}

uint8_t GetLastLogEntries(LogEntry *out, uint8_t max_count) {
    uint8_t count = (g_system.logCount < max_count) ? g_system.logCount : max_count;
    if (count == 0) return 0;
    // start from newest: (logIndex-1) mod MAX_LOG_ENTRIES
    int16_t idx = (g_system.logIndex - 1 + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;
    for (uint8_t i = 0; i < count; i++) {
        out[i] = g_system.eventLog[idx];
        idx = (idx - 1 + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;
    }
    return count;
}



// ============================================================
// MCP2515 Helpers
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

// Per-zone raw flag shadow — one byte per zone, mirrors last received flags frame.
// Useful for debug; g_system.zones[] is the authoritative state for logic.
static uint8_t g_zoneFlags[NUM_ZONES] = {0};

// Helper: bring a zone online, handling first-boot vs recovery
static void zone_mark_online(uint8_t z)
{
    ZoneStatus& zone = g_system.zones[z];

    if (!zone.alive)
    {
        zone.alive = true;

        if (zone.fault != FAULT_NONE)
        {
            zone.fault = FAULT_NONE;
            PushEvent({ EVENT_ZONE_RECOVERED, z });

            char buf[64];
            snprintf(buf, sizeof(buf), "Zone %u recovered", z);
        }
        else
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "Zone %u online", z);
        }
    }
}

static void can_process_frame(uint8_t sidh, uint8_t sidl, uint8_t dlc,  uint8_t base_reg)
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
            }
            else if (!doorNow && prevDoor)
            {
                PushEvent({ EVENT_ZONE_CLEARED, z });
                char buf[64];
                snprintf(buf, sizeof(buf), "Zone %u: door closed", z);
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
            }
            else if (!motionNow && prevMotion)
            {
                PushEvent({ EVENT_ZONE_CLEARED, z });
                char buf[64];
                snprintf(buf, sizeof(buf), "Zone %u: motion cleared", z);
            }

            // --- Throttled debug ---
            static uint32_t last_print[NUM_ZONES] = {0};
            uint32_t now = scheduler_millis();
            if (now - last_print[z] >= 1000)
            {
                printf("Z%u flags=0x%02X | Door:%s Motion:%s HiTemp:%s LoTemp:%s HiHum:%s\n", z, flags,
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
                if (!zone.alive)
                    continue;

                bool timedOut = (now - zone.lastHeartbeat) > HEARTBEAT_TIMEOUT_MS;

                // Inside TickWatchdog, case WD_RUN:
                if (timedOut && zone.fault == FAULT_NONE)
                {
                    zone.alive = false;
                    zone.fault = FAULT_HEARTBEAT_TIMEOUT;
                    PushEvent({ EVENT_HEARTBEAT_TIMEOUT, z });
                    // Log inside the event action, not here
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

// Returns true if at least one zone has fault != FAULT_NONE
static bool AnyZoneFaulted()
{
    for (uint8_t z = 0; z < NUM_ZONES; z++)
        if (g_system.zones[z].fault != FAULT_NONE)
            return true;
    return false;
}

// Clear fault and alive flags for all zones (used when entering DISARMED)
static void ClearAllZoneFaults()
{
    for (uint8_t z = 0; z < NUM_ZONES; z++)
    {
        g_system.zones[z].fault = FAULT_NONE;
        g_system.zones[z].alive = true;     // Assume recovered
        g_system.zones[z].lastHeartbeat = scheduler_millis();
        // Do NOT clear door_open or motion – those are sensor state,
        // they will be updated by next CAN flags frame.
    }
}

static SystemState StateTransition(SystemState current, Event event, uint8_t zone)
{
    (void)zone;  // zone index not needed for transition decisions in this simplified design

    switch (current)
    {
        case DISARMED:
            switch (event.type)
            {
                case EVENT_ARM:                 return ARMED;
                case EVENT_HEARTBEAT_TIMEOUT:   return FAULT;
                default:                        return DISARMED;
            }

        case ARMED:
            switch (event.type)
            {
                case EVENT_DISARM:              return DISARMED;
                case EVENT_ZONE_TRIGGERED:      return ALERT;
                case EVENT_HEARTBEAT_TIMEOUT:   return FAULT;
                default:                        return ARMED;
            }

        case ALERT:
            switch (event.type)
            {
                case EVENT_DISARM:              return DISARMED;
                case EVENT_ALERT_ACK:           return ARMED;
                case EVENT_HEARTBEAT_TIMEOUT:   return FAULT;
                default:                        return ALERT;
            }

        case FAULT:
            switch (event.type)
            {
                case EVENT_ZONE_RECOVERED:      return DISARMED;
                default:                        return FAULT;
            }

        default:
            return DISARMED;
    }
}

static void StateActions(SystemState new_state, Event event, uint8_t zone)
{
    char buf[64];

    // --- Log zone events only once (first switch) ---
    switch (event.type) {
        case EVENT_ZONE_TRIGGERED:
            snprintf(buf, sizeof(buf), "Zone %u triggered", zone);
            AddLog(buf);
            break;
        case EVENT_ZONE_CLEARED:
            snprintf(buf, sizeof(buf), "Zone %u cleared", zone);
            AddLog(buf);
            break;
        case EVENT_HEARTBEAT_TIMEOUT:
            snprintf(buf, sizeof(buf), "Zone %u offline", zone);
            AddLog(buf);
            break;
        case EVENT_ZONE_RECOVERED:
            snprintf(buf, sizeof(buf), "Zone %u recovered", zone);
            AddLog(buf);
            break;
        default: break;
    }

    // --- Actions (no logging here except state transition logs are already done in TickSystem) ---
    switch (event.type)
    {
        case EVENT_DISARM:
        case EVENT_ALERT_ACK:
            g_system.alarmActive = false;
            break;
        default: break;
    }

    // --- New state actions (no logging) ---
    switch (new_state)
    {
        case DISARMED:
            g_system.alarmActive = false;
            ClearAllZoneFaults();
            for (uint8_t z = 0; z < NUM_ZONES; z++) {
                g_system.zones[z].door_open = false;
                g_system.zones[z].motion = false;
            }
            break;
        case ALERT:
            g_system.alarmActive = true;
            break;
        case FAULT:
            g_system.alarmActive = false;
            break;
        default: break;
    }
}

int TickSystem(int state)
{
    static SystemState current_state = DISARMED;
    Event event;

    switch (state)
    {
        case SYSTEM_INIT:
            current_state = DISARMED;
            g_system.state = DISARMED;
            g_system.alarmActive = false;
            ClearAllZoneFaults();
            AddLog("System Initialized");
            return SYSTEM_RUN;

        case SYSTEM_RUN:
            while (PopEvent(&event))
            {
                SystemState next_state = StateTransition(current_state, event, event.zone);
                if (next_state != current_state)
                {
                    // Log the new state
                    const char* state_msg = "";
                    switch (next_state) 
                    {
                        case DISARMED: state_msg = "System Disarmed"; break;
                        case ARMED:    state_msg = "System Armed"; break;
                        case ALERT:    state_msg = "ALERT Activated"; break;
                        case FAULT:    state_msg = "System Fault"; break;
                    }
                    AddLog(state_msg);
                    
                    StateActions(next_state, event, event.zone);
                    current_state = next_state;
                    g_system.state = current_state;
                } 
                else 
                {
                    StateActions(current_state, event, event.zone);
                }
            }
            break;

        default:
            return SYSTEM_INIT;
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

// enum TouchTaskState
// {
//     TOUCH_INIT,
//     TOUCH_RUN
// };

// int TickTouch(int state)
// {
//     switch(state)
//     {
//         case TOUCH_INIT:

//             state = TOUCH_RUN;
//             break;

//         case TOUCH_RUN:

//             /*
//                 TODO:

//                 ARM button
//                 DISARM button
//                 ACK button

//                 PushEvent(EVENT_ARM)
//                 PushEvent(EVENT_DISARM)
//             */

//             break;
//     }

//     return state;
// }

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
            g_app->init();
            state = UI_RUN;
            break;

        case UI_RUN:
            g_app->refresh_dashboard(g_system.zones[0], g_system.state, 0);
            g_app->update();
            break;
    }

    return state;
}

// ============================================================
// TASK: Buzzer  (Output Layer)
//
// Drives PIN_BUZZER based on g_system.alarmActive and state.
//
//   ALERT + alarmActive  -> continuous tone (GPIO high)
//   FAULT                -> slow 500 ms blink to distinguish from alarm
//   anything else        -> silent (GPIO low)
// ============================================================

enum BuzzerTaskState
{
    BUZZER_INIT,
    BUZZER_RUN
};

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
            if (g_system.alarmActive &&
                g_system.state == ALERT)
            {
                // Continuous on during active alarm
                gpio_put(PIN_BUZZER, 1);
            }
            else if (g_system.state == FAULT)
            {
                // Slow blink: toggle every 500 ms
                static uint32_t lastToggle = 0;
                uint32_t now = scheduler_millis();
                if (now - lastToggle >= 500)
                {
                    gpio_put(PIN_BUZZER, !gpio_get(PIN_BUZZER)); 
                    lastToggle = now;
                }
            }
            else
            {
                gpio_put(PIN_BUZZER, 0);
            }
            break;
        }

        default:
            state = BUZZER_INIT;
            break;
    }

    return state;
}

// ============================================================
// TASK: DEBUG
// ============================================================

enum DebugState
{
    DEBUG_INIT,
    DEBUG_RUN
};

int TickDebug(int state)
{
    switch(state)
    {
        case DEBUG_INIT:

            printf(
                "\n"
                "====================================\n"
                " Debug Commands\n"
                "====================================\n"
                "a = ARM\n"
                "d = DISARM\n"
                "t = TRIGGER ZONE 0\n"
                "c = CLEAR ZONE 0\n"
                "k = ACK ALERT\n"
                "h = HEARTBEAT FAULT\n"
                "r = RECOVER ZONE\n"
                "====================================\n"
            );

            state = DEBUG_RUN;
            break;

        case DEBUG_RUN:
        {
            int ch = getchar_timeout_us(0);

            if(ch == PICO_ERROR_TIMEOUT)
                break;

            switch(ch)
            {
                // ----------------------------------------
                // ARM
                // ----------------------------------------
                case 'a':
                    printf("[DEBUG] Inject EVENT_ARM\n");
                    PushEvent({ EVENT_ARM, 0 });
                    break;

                // ----------------------------------------
                // DISARM
                // ----------------------------------------
                case 'd':
                    printf("[DEBUG] Inject EVENT_DISARM\n");
                    PushEvent({ EVENT_DISARM, 0 });
                    break;

                // ----------------------------------------
                // ZONE TRIGGER
                // Mimics sensor activity
                // ----------------------------------------
                case 't':
                    printf("[DEBUG] Inject EVENT_ZONE_TRIGGERED\n");

                    g_system.zones[0].motion = true;

                    PushEvent(
                    {
                        EVENT_ZONE_TRIGGERED,
                        0
                    });

                    break;

                // ----------------------------------------
                // ZONE CLEAR
                // Mimics sensor clearing
                // ----------------------------------------
                case 'c':
                    printf("[DEBUG] Inject EVENT_ZONE_CLEARED\n");

                    g_system.zones[0].motion = false;
                    g_system.zones[0].door_open = false;

                    PushEvent(
                    {
                        EVENT_ZONE_CLEARED,
                        0
                    });

                    break;

                // ----------------------------------------
                // ACK ALERT
                // ----------------------------------------
                case 'k':
                    printf("[DEBUG] Inject EVENT_ALERT_ACK\n");

                    PushEvent(
                    {
                        EVENT_ALERT_ACK,
                        0
                    });

                    break;

                // ----------------------------------------
                // FORCE HEARTBEAT FAILURE
                // Simulates watchdog timeout
                // ----------------------------------------
                case 'h':
                    printf("[DEBUG] Inject EVENT_HEARTBEAT_TIMEOUT\n");

                    for(uint8_t z = 0; z < NUM_ZONES; z++)
                    {
                        g_system.zones[z].alive = false;
                        g_system.zones[z].fault =
                            FAULT_HEARTBEAT_TIMEOUT;
                    }

                    PushEvent(
                    {
                        EVENT_HEARTBEAT_TIMEOUT,
                        0
                    });

                    break;

                // ----------------------------------------
                // RECOVER ZONE
                // Simulates heartbeat returning
                // ----------------------------------------
                case 'r':
                    printf("[DEBUG] Inject EVENT_ZONE_RECOVERED\n");

                    for(uint8_t z = 0; z < NUM_ZONES; z++)
                    {
                        g_system.zones[z].alive = true;
                        g_system.zones[z].fault = FAULT_NONE;
                    }

                    PushEvent(
                    {
                        EVENT_ZONE_RECOVERED,
                        0
                    });

                    break;

                default:
                    break;
            }
        }
        break;

        default:
            state = DEBUG_INIT;
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
static const uint32_t WATCHDOG_PERIOD_MS = 100;
static const uint32_t LOGGER_PERIOD_MS   = 50;
// static const uint32_t TOUCH_PERIOD_MS    = 200;
static const uint32_t UI_PERIOD_MS       = 100;
static const uint32_t BUZZER_PERIOD_MS   = 100;
static const uint32_t DEBUG_MS           = 100;

// ============================================================
// Main
// ============================================================

int main()
{
    stdio_init_all();
    cyw43_arch_init();
    adc_init();

    while(!stdio_usb_connected())
    {
        sleep_ms(100);
    }

    printf("Booting...\n");

    // ========================================================
    // Hardware Init
    // ========================================================

    spi_init(SPI_PORT, 1000000);

    
    g_display = new ucr::bcoe::SPIDisplay(480,272,10000000,20);

    g_display->begin();
    g_display->clear();

    g_app = new ucr::bcoe::cs::cs122::CS122_App(g_display,cs122_flush_cb_partial,cs122_get_millis);

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
        //{ CAN_INIT,      CAN_PERIOD_MS,      0, TickCAN      },
        { SYSTEM_INIT,   SYSTEM_PERIOD_MS,   0, TickSystem   },
        //{ WD_INIT,       WATCHDOG_PERIOD_MS, 0, TickWatchdog },
        { LOGGER_INIT,   LOGGER_PERIOD_MS,   0, TickLogger   },
        //{ TOUCH_INIT,    TOUCH_PERIOD_MS,    0, TickTouch    },
        { UI_INIT,       UI_PERIOD_MS,       0, TickUI       },
        { BUZZER_INIT,   BUZZER_PERIOD_MS,   0, TickBuzzer   },
        { DEBUG_INIT,    DEBUG_MS,           0, TickDebug    }
    };

    const size_t NUM_TASKS = sizeof(tasks) / sizeof(tasks[0]);

    scheduler_init(GCD_PERIOD_MS);

    printf("Scheduler running - %u tasks\n",(unsigned)NUM_TASKS);

    while(true)
    {
        scheduler_run(tasks, NUM_TASKS);
    }
}
