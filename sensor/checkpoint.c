#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// Ultrasonic
#define TRIG 2
#define ECHO 3

// Button
#define BUTTON 0

// DHT22
#define DATA 4

// CAN (MCP2515 via SPI0)
#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19
#define PIN_INT  20

#define MCP_RESET    0xC0
#define MCP_WRITE    0x02
#define MCP_READ     0x03
#define MCP_BITMOD   0x05
#define MCP_RTS_TXB0 0x81

#define CANCTRL  0x0F
#define CANSTAT  0x0E
#define CNF3     0x28
#define CNF2     0x29
#define CNF1     0x2A
#define CANINTE  0x2B
#define TXB0CTRL 0x30
#define TXB0SIDH 0x31
#define TXB0SIDL 0x32
#define TXB0DLC  0x35
#define TXB0D0   0x36

// Zone identity — change ZONE_ID per node (0=A, 1=B, 2=C)
#define ZONE_ID      0
#define CAN_ID_FLAGS (0x100 + (ZONE_ID * 0x10))
#define CAN_ID_HB    (0x1A0 + (ZONE_ID * 0x10))

// Flag bits
#define FLAG_DOOR_OPEN (1 << 0)
#define FLAG_MOTION    (1 << 1)
#define FLAG_HIGH_TEMP (1 << 2)
#define FLAG_LOW_TEMP  (1 << 3)
#define FLAG_HIGH_HUM  (1 << 4)

// Thresholds
#define THRESH_DIST_CM   25
#define THRESH_TEMP_HIGH 300    // 30.0 C * 10
#define THRESH_TEMP_LOW  100    // 10.0 C * 10
#define THRESH_HUM_HIGH  800    // 80.0 % * 10

// Heartbeat interval: 10 ticks * 100ms GCD = 1000ms
#define HB_TICKS 10

// Cached DHT22 values, updated every 2s by TEMP task, read every 100ms by DIST task
static int16_t  cached_temp_x10 = 0;
static uint16_t cached_hum_x10  = 0;

// Shared door state — written by DOOR task, read by DIST task
static bool door_open = false;

// CAN TX state
static uint8_t prev_flags = 0xFF;  // 0xFF forces a send on first tick
static uint8_t hb_counter = 0;

// Task scheduler (from CS120B)
typedef struct task {
    signed   char state;
    unsigned long period;
    unsigned long elapsedTime;
    int (*TickFct)(int);
} task;

const unsigned long TASK1_PERIOD = 200;
const unsigned long TASK2_PERIOD = 100;
const unsigned long TASK3_PERIOD = 2000;
const unsigned long GCD_PERIOD   = 100;

// --- MCP2515 helpers ---
void cs_select()   { gpio_put(PIN_CS, 0); }
void cs_deselect() { gpio_put(PIN_CS, 1); }

void mcp_reset() {
    uint8_t cmd = MCP_RESET;
    cs_select();
    spi_write_blocking(SPI_PORT, &cmd, 1);
    cs_deselect();
    sleep_ms(10);
}

uint8_t mcp_read(uint8_t addr) {
    uint8_t tx[3] = {MCP_READ, addr, 0x00};
    uint8_t rx[3] = {0};
    cs_select();
    spi_write_read_blocking(SPI_PORT, tx, rx, 3);
    cs_deselect();
    return rx[2];
}

void mcp_write(uint8_t addr, uint8_t data) {
    uint8_t tx[3] = {MCP_WRITE, addr, data};
    cs_select();
    spi_write_blocking(SPI_PORT, tx, 3);
    cs_deselect();
}

void mcp_bit_modify(uint8_t addr, uint8_t mask, uint8_t data) {
    uint8_t tx[4] = {MCP_BITMOD, addr, mask, data};
    cs_select();
    spi_write_blocking(SPI_PORT, tx, 4);
    cs_deselect();
}

void mcp_init_500kbps_8mhz() {
    mcp_reset();
    mcp_write(CNF1, 0x00);
    mcp_write(CNF2, 0xD0);
    mcp_write(CNF3, 0x02);
    mcp_write(CANINTE, 0x00);
    mcp_bit_modify(CANCTRL, 0xE0, 0x00); // normal mode
    sleep_ms(10);
}

void can_send(uint16_t id, uint8_t *data, uint8_t len) {
    uint8_t cmd = MCP_RTS_TXB0;
    mcp_write(TXB0CTRL, 0x00);
    mcp_write(TXB0SIDH, id >> 3);
    mcp_write(TXB0SIDL, (id & 0x07) << 5);
    mcp_write(TXB0DLC, len);
    for (int i = 0; i < len; i++)
        mcp_write(TXB0D0 + i, data[i]);
    cs_select();
    spi_write_blocking(SPI_PORT, &cmd, 1);
    cs_deselect();
}

// --- Sensors ---
int getDistance() {
    gpio_put(TRIG, 0);
    sleep_us(2);
    gpio_put(TRIG, 1);
    sleep_us(10);
    gpio_put(TRIG, 0);

    uint32_t t = time_us_32();
    while (!gpio_get(ECHO) && (time_us_32() - t) < 30000);
    uint32_t startTime = time_us_32();
    while (gpio_get(ECHO)  && (time_us_32() - startTime) < 30000);
    uint32_t endTime = time_us_32();

    return (endTime - startTime) / 58;
}

// Reads DHT22 — fills temp_x10 (signed, temp*10) and hum_x10 (humidity*10)
// Returns true on valid checksum
bool readDHT(int16_t *temp_x10, uint16_t *hum_x10) {
    uint8_t data[5] = {0};

    gpio_set_dir(DATA, GPIO_OUT);
    gpio_put(DATA, 0);
    sleep_ms(18);         // DHT11 needs >= 18ms, not 1-2ms
    gpio_put(DATA, 1);
    sleep_us(20);
    gpio_set_dir(DATA, GPIO_IN);
    gpio_pull_up(DATA);
    sleep_us(10);

    uint32_t t;

    t = time_us_32();
    while (gpio_get(DATA) == 1) {
        if (time_us_32() - t > 200) {
            printf("DHT timeout: waiting for response low\n");
            return false;
        }
    }
    t = time_us_32();
    while (gpio_get(DATA) == 0) {
        if (time_us_32() - t > 200) {
            printf("DHT timeout: waiting for response high\n");
            return false;
        }
    }
    t = time_us_32();
    while (gpio_get(DATA) == 1) {
        if (time_us_32() - t > 200) {
            printf("DHT timeout: waiting for data start\n");
            return false;
        }
    }

    for (int i = 0; i < 40; i++) {
        t = time_us_32();
        while (gpio_get(DATA) == 0) {
            if (time_us_32() - t > 100) {
                printf("DHT timeout: bit %d low preamble\n", i);
                return false;
            }
        }
        sleep_us(40);
        data[i / 8] <<= 1;
        if (gpio_get(DATA)) data[i / 8] |= 1;

        t = time_us_32();
        while (gpio_get(DATA) == 1) {
            if (time_us_32() - t > 100) {
                printf("DHT timeout: bit %d high pulse\n", i);
                return false;
            }
        }
    }

    printf("DHT raw: %02X %02X %02X %02X  checksum byte: %02X  expected: %02X\n",
           data[0], data[1], data[2], data[3], data[4],
           (data[0] + data[1] + data[2] + data[3]) & 0xFF);

    if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
        printf("DHT checksum failed\n");
        return false;
    }

    // DHT11: byte 0 = humidity integer, byte 2 = temp integer, no decimals
    *hum_x10  = (uint16_t)(data[0] * 10);
    *temp_x10 = (int16_t)(data[2] * 10);
    return true;
}
// --- State machine declarations ---
enum DOOR_States     { DOOR_START, IDLE, PRESS }      door_state;
enum DISTANCE_States { DIST_START, DIST_IDLE, FRONT } dist_states;
enum TEMP_States     { TEMP_START, TEMP_READ }        temp_states;

int TickFct_DOOR(int state);
int TickFct_DIST(int state);
int TickFct_TEMP(int state);

task tasks[3] = {
    { DOOR_START, TASK1_PERIOD, TASK1_PERIOD, &TickFct_DOOR },
    { DIST_START, TASK2_PERIOD, TASK2_PERIOD, &TickFct_DIST },
    { TEMP_START, TASK3_PERIOD, TASK3_PERIOD, &TickFct_TEMP },
};

int main() {
    stdio_init_all();
    sleep_ms(2000);

    gpio_init(TRIG); gpio_set_dir(TRIG, GPIO_OUT);
    gpio_init(ECHO); gpio_set_dir(ECHO, GPIO_IN);

    gpio_init(BUTTON); gpio_set_dir(BUTTON, GPIO_IN);
    gpio_pull_down(BUTTON);

    gpio_init(DATA); gpio_set_dir(DATA, GPIO_IN);

    spi_init(SPI_PORT, 1000000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_CS);  gpio_set_dir(PIN_CS,  GPIO_OUT); cs_deselect();
    gpio_init(PIN_INT); gpio_set_dir(PIN_INT, GPIO_IN);  gpio_pull_up(PIN_INT);

    mcp_init_500kbps_8mhz();

    printf("Zone %d CAN Node Starting...\n", ZONE_ID);
    printf("FLAGS ID: 0x%03X  HB ID: 0x%03X\n", CAN_ID_FLAGS, CAN_ID_HB);
    printf("CANSTAT = 0x%02X\n", mcp_read(CANSTAT));
    printf("CANCTRL = 0x%02X\n", mcp_read(CANCTRL));

    while (true) {
        for (unsigned int i = 0; i < 3; i++) {
            if (tasks[i].elapsedTime == tasks[i].period) {
                tasks[i].state = tasks[i].TickFct(tasks[i].state);
                tasks[i].elapsedTime = 0;
            }
            tasks[i].elapsedTime += GCD_PERIOD;
        }
        sleep_ms(GCD_PERIOD);
    }
}

// Updates shared door_open — no longer sends CAN directly
int TickFct_DOOR(int state) {
    bool press = gpio_get(BUTTON);

    switch (state) {
        case DOOR_START: door_state = IDLE;                 break;
        case IDLE:       door_state = press ? PRESS : IDLE; break;
        case PRESS:      door_state = press ? PRESS : IDLE; break;
        default: break;
    }

    door_open = (door_state == PRESS);
    return door_state;
}

// Owns all CAN TX — computes flags, sends on change and heartbeat
int TickFct_DIST(int state) {
    int distance = getDistance();

    switch (state) {
        case DIST_START: dist_states = DIST_IDLE;                                                        break;
        case DIST_IDLE:  dist_states = (distance > 0 && distance < THRESH_DIST_CM) ? FRONT : DIST_IDLE; break;
        case FRONT:      dist_states = (distance > 0 && distance < THRESH_DIST_CM) ? FRONT : DIST_IDLE; break;
        default: break;
    }

    uint8_t flags = 0;
    if (door_open)                           flags |= FLAG_DOOR_OPEN;
    if (dist_states == FRONT)                flags |= FLAG_MOTION;
    if (cached_temp_x10 > THRESH_TEMP_HIGH)  flags |= FLAG_HIGH_TEMP;
    if (cached_temp_x10 < THRESH_TEMP_LOW)   flags |= FLAG_LOW_TEMP;
    if (cached_hum_x10  > THRESH_HUM_HIGH)   flags |= FLAG_HIGH_HUM;

    // Print distance every 500 ms (~5 ticks at 100 ms GCD)
    static uint8_t print_counter = 0;
    if (++print_counter >= 5)
    {
        printf("Dist: %d cm  [%s]  flags=0x%02X\n",
               distance,
               dist_states == FRONT ? "MOTION" : "clear ",
               flags);
        print_counter = 0;
    }

    if (flags != prev_flags) {
        can_send(CAN_ID_FLAGS, &flags, 1);
        prev_flags = flags;
        hb_counter = 0;
    }

    hb_counter++;
    if (hb_counter >= HB_TICKS) {
        uint8_t hb = 0x00;
        can_send(CAN_ID_HB, &hb, 1);
        hb_counter = 0;
    }

    return dist_states;
}

int TickFct_TEMP(int state) {
    switch (state) {
        case TEMP_START: temp_states = TEMP_READ; break;
        case TEMP_READ:  temp_states = TEMP_READ; break;
        default: break;
    }

    if (temp_states == TEMP_READ) {
        int16_t  t = 0;
        uint16_t h = 0;
        if (readDHT(&t, &h)) {
            cached_temp_x10 = t;
            cached_hum_x10  = h;
            printf("Temp: %.1fC  Humidity: %.1f%%\n", t / 10.0f, h / 10.0f);
        }
    }

    return temp_states;
}