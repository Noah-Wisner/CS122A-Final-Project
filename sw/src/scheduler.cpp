#include "scheduler.h"
#include <pico/stdlib.h>
#include <pico/time.h>

// ─────────────────────────────────────────────────────────────
//  Internal state
//  (Private to this translation unit, like _avr_timer_M)
// ─────────────────────────────────────────────────────────────
static uint32_t s_gcd_period_ms = 1;
static uint32_t s_last_tick_ms  = 0;

// ─────────────────────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────────────────────

void scheduler_init(uint32_t gcd_period_ms) {
    s_gcd_period_ms = gcd_period_ms;
    s_last_tick_ms  = to_ms_since_boot(get_absolute_time());
}


size_t scheduler_run(Task* tasks, size_t num_tasks) {
    uint32_t now   = to_ms_since_boot(get_absolute_time());
    uint32_t delta = now - s_last_tick_ms;

    // Only advance on a GCD boundary — same idea as the AVR
    // ISR firing once per millisecond and counting down to GCD.
    if (delta < s_gcd_period_ms) return 0;
    
    s_last_tick_ms = now;

    size_t ticked = 0;
    for (size_t i = 0; i < num_tasks; ++i) {
        tasks[i].elapsed_ms += delta;
        if (tasks[i].elapsed_ms >= tasks[i].period_ms) {
            tasks[i].state      = tasks[i].tick(tasks[i].state);
            tasks[i].elapsed_ms = 0;
            ++ticked;
        }
    }
    return ticked;
}

uint32_t scheduler_millis() {
    return to_ms_since_boot(get_absolute_time());
}
