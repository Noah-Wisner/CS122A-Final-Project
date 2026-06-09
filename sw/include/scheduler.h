#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stddef.h>

// ─────────────────────────────────────────────────────────────
//  Task descriptor
//  Mirrors the AVR `task` struct from timerISR-Fixed pattern.
//  Each subsystem registers one of these.
// ─────────────────────────────────────────────────────────────
struct Task {
    int      state;        // Current state (owned by the tick function)
    uint32_t period_ms;    // How often this task should run
    uint32_t elapsed_ms;   // Time accumulated since last tick
    int    (*tick)(int);   // State-machine tick; returns next state
};

// ─────────────────────────────────────────────────────────────
//  Scheduler API
// ─────────────────────────────────────────────────────────────

// Call once before the main loop.
// Sets the GCD period and records the start time.
void scheduler_init(uint32_t gcd_period_ms);

// Call every iteration of the main loop.
// Internally checks elapsed time and fires tasks whose period has elapsed.
// Returns the number of tasks ticked this call (informational).
size_t scheduler_run(Task* tasks, size_t num_tasks);

// Returns milliseconds since scheduler_init() was called.
// Useful for tasks that need their own timestamps.
uint32_t scheduler_millis();

#endif // SCHEDULER_H
