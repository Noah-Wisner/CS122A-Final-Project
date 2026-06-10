// ============================================================
//  camera_task.h
//
//  OV7670 Camera — Scheduler Task
//
//  Integrates the OV7670 grayscale frame-capture driver as a
//  cooperative task compatible with the existing scheduler in
//  main.cpp.
//
//  Pin assignments (choose pins not used by CAN/SPI):
//    SDA_PIN   GP8   (I2C1 SDA)
//    SCL_PIN   GP9   (I2C1 SCL)
//    XCLK_PIN  GP10  (PWM clock output to camera)
//    VSYNC_PIN GP11
//    HREF_PIN  GP12
//    PCLK_PIN  GP13
//    DATA[0]   GP14 … DATA[7] GP21  ← 8-bit parallel data bus
//
//  NOTE: Adjust pin numbers below to match your PCB if different.
//
//  Usage in main.cpp:
//    1. #include "camera_task.h"
//    2. Add to task table:
//         { CAMERA_INIT, CAMERA_PERIOD_MS, 0, TickCamera },
//    3. Add period constant (e.g. 3000 ms between captures):
//         static const uint32_t CAMERA_PERIOD_MS = 3000;
//
// ============================================================

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"

// ============================================================
// Pin Definitions — adjust if needed
// ============================================================

#define CAM_SDA_PIN   8
#define CAM_SCL_PIN   9
#define CAM_XCLK_PIN  10
#define CAM_VSYNC_PIN 11
#define CAM_HREF_PIN  12
#define CAM_PCLK_PIN  13
#define CAM_DATA_BASE 14   // D0=GP14 … D7=GP21

// ============================================================
// OV7670 Constants
// ============================================================

#define OV7670_ADDR  0x21
#define CAM_WIDTH    160
#define CAM_HEIGHT   120

// ============================================================
// Frame Buffer (owned by this module)
// ============================================================

static uint8_t g_cameraFrame[CAM_WIDTH * CAM_HEIGHT];

// ============================================================
// Frame Statistics (readable by UI / Logger tasks)
// ============================================================

struct CameraStats
{
    uint8_t  minPixel;
    uint8_t  maxPixel;
    uint8_t  avgPixel;
    bool     frameReady;   // true after first successful capture
    uint32_t captureCount;
};

static CameraStats g_cameraStats = { 0, 0, 0, false, 0 };

// ============================================================
// Low-level helpers
// ============================================================

static void cam_start_xclk()
{
    gpio_set_function(CAM_XCLK_PIN, GPIO_FUNC_PWM);

    uint slice   = pwm_gpio_to_slice_num(CAM_XCLK_PIN);
    uint channel = pwm_gpio_to_channel(CAM_XCLK_PIN);

    // ~6.25 MHz XCLK from 125 MHz sys-clock:  125 / (8 * 2) / 1 = ~7.8 MHz
    // Adjust pwm_set_clkdiv / wrap to tune for your crystal.
    pwm_set_clkdiv(slice, 8.0f);
    pwm_set_wrap(slice, 1);
    pwm_set_chan_level(slice, channel, 1);
    pwm_set_enabled(slice, true);
}

static bool cam_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t data[2] = { reg, val };
    int ret = i2c_write_timeout_us(i2c1, OV7670_ADDR, data, 2, false, 10000);
    return ret == 2;
}

static bool cam_read_reg(uint8_t reg, uint8_t* value)
{
    if (i2c_write_timeout_us(i2c1, OV7670_ADDR, &reg, 1, false, 10000) != 1)
        return false;

    sleep_ms(1);

    if (i2c_read_timeout_us(i2c1, OV7670_ADDR, value, 1, false, 10000) != 1)
        return false;

    return true;
}

static uint8_t cam_read_bus()
{
    return (gpio_get_all() >> CAM_DATA_BASE) & 0xFF;
}

static void cam_wait_pclk_rising()
{
    while ( gpio_get(CAM_PCLK_PIN));
    while (!gpio_get(CAM_PCLK_PIN));
}

// ============================================================
// OV7670 Initialisation
// ============================================================

static void cam_ov7670_init()
{
    cam_write_reg(0x12, 0x80);   // soft reset
    sleep_ms(200);

    cam_write_reg(0x12, 0x00);   // YUV mode (Y = luma, used for grayscale)
    cam_write_reg(0x11, 0x01);   // internal clock pre-scaler ÷2
    cam_write_reg(0x0C, 0x04);
    cam_write_reg(0x3E, 0x19);
    cam_write_reg(0x70, 0x3A);
    cam_write_reg(0x71, 0x35);
    cam_write_reg(0x72, 0x11);
    cam_write_reg(0x73, 0xF1);
    cam_write_reg(0xA2, 0x02);
}

// ============================================================
// Frame Capture  (blocking — runs inside the CAMERA_RUN tick)
//
// The frame is small (160×120 = 19,200 bytes).  At PCLK ~1 MHz
// the full capture takes ~40 ms, comfortably within a 3 s task
// period.  If a faster PCLK is used, consider moving capture to
// a dedicated core (core1) and using a flag to signal completion.
// ============================================================

static void cam_capture_frame()
{
    int index = 0;

    // Wait for the rising edge of VSYNC (start of new frame)
    while (!gpio_get(CAM_VSYNC_PIN));
    while ( gpio_get(CAM_VSYNC_PIN));

    for (int y = 0; y < CAM_HEIGHT; y++)
    {
        while (!gpio_get(CAM_HREF_PIN));  // wait for active line

        for (int x = 0; x < CAM_WIDTH; x++)
        {
            cam_wait_pclk_rising();
            uint8_t y_byte = cam_read_bus();  // luma byte

            cam_wait_pclk_rising();
            cam_read_bus();                   // discard Cb/Cr byte

            g_cameraFrame[index++] = y_byte;
        }

        while (gpio_get(CAM_HREF_PIN));   // wait for line to end
    }
}

// ============================================================
// Stats Computation
// ============================================================

static void cam_compute_stats()
{
    uint8_t  mn  = 255, mx = 0;
    uint32_t sum = 0;

    for (int i = 0; i < CAM_WIDTH * CAM_HEIGHT; i++)
    {
        uint8_t p = g_cameraFrame[i];
        if (p < mn) mn = p;
        if (p > mx) mx = p;
        sum += p;
    }

    g_cameraStats.minPixel    = mn;
    g_cameraStats.maxPixel    = mx;
    g_cameraStats.avgPixel    = (uint8_t)(sum / (CAM_WIDTH * CAM_HEIGHT));
    g_cameraStats.frameReady  = true;
    g_cameraStats.captureCount++;
}

// ============================================================
// TASK: Camera
//
//  States
//  -------
//  CAMERA_INIT  — one-shot hardware setup
//  CAMERA_RUN   — capture a frame then sleep until next tick
// ============================================================

enum CameraTaskState
{
    CAMERA_INIT,
    CAMERA_RUN
};

int TickCamera(int state)
{
    switch (state)
    {
        // --------------------------------------------------
        case CAMERA_INIT:
        {
            // I2C bus for SCCB (OV7670 control)
            i2c_init(i2c1, 100000);
            gpio_set_function(CAM_SDA_PIN, GPIO_FUNC_I2C);
            gpio_set_function(CAM_SCL_PIN, GPIO_FUNC_I2C);
            gpio_pull_up(CAM_SDA_PIN);
            gpio_pull_up(CAM_SCL_PIN);

            // XCLK must be running before I2C comms to the sensor
            cam_start_xclk();
            sleep_ms(50);  // allow camera PLL to lock

            // Parallel data bus (D0–D7)
            for (int pin = CAM_DATA_BASE; pin < CAM_DATA_BASE + 8; pin++)
            {
                gpio_init(pin);
                gpio_set_dir(pin, GPIO_IN);
            }

            // Sync signals
            gpio_init(CAM_VSYNC_PIN); gpio_set_dir(CAM_VSYNC_PIN, GPIO_IN);
            gpio_init(CAM_HREF_PIN);  gpio_set_dir(CAM_HREF_PIN,  GPIO_IN);
            gpio_init(CAM_PCLK_PIN);  gpio_set_dir(CAM_PCLK_PIN,  GPIO_IN);

            // Verify sensor is present
            uint8_t pid = 0, ver = 0;
            bool pid_ok = cam_read_reg(0x0A, &pid);
            bool ver_ok = cam_read_reg(0x0B, &ver);

            if (pid_ok && ver_ok)
                printf("[Camera] OV7670 found  PID=0x%02X  VER=0x%02X\n", pid, ver);
            else
                printf("[Camera] WARNING: OV7670 not responding (PID=%s VER=%s)\n",
                       pid_ok ? "ok" : "FAIL", ver_ok ? "ok" : "FAIL");

            cam_ov7670_init();
            printf("[Camera] Initialised — %ux%u grayscale\n",
                   CAM_WIDTH, CAM_HEIGHT);

            state = CAMERA_RUN;
            break;
        }

        // --------------------------------------------------
        case CAMERA_RUN:
        {
            printf("[Camera] Capturing frame %lu...\n",
                   (unsigned long)g_cameraStats.captureCount + 1);

            cam_capture_frame();
            cam_compute_stats();

            printf("[Camera] Frame %lu  Min=%u  Max=%u  Avg=%u\n",
                   (unsigned long)g_cameraStats.captureCount,
                   g_cameraStats.minPixel,
                   g_cameraStats.maxPixel,
                   g_cameraStats.avgPixel);

            break;
        }

        // --------------------------------------------------
        default:
            state = CAMERA_INIT;
            break;
    }

    return state;
}
