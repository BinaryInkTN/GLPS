#ifndef GLPS_TIMER_H
#define GLPS_TIMER_H

#include <stdint.h>

typedef void (*timer_callback)(void *arg);  // Callback function type

typedef struct glps_timer {
    uint64_t start_time_ms;
    uint64_t end_time_ms;
    uint64_t duration_ms;          // Timer duration in ms
    timer_callback callback;       // Callback function
    void *callback_arg;            // Argument to pass to callback
} glps_timer;

glps_timer *glps_timer_init(void);
void glps_timer_start(glps_timer *timer, uint64_t duration_ms, timer_callback callback, void *arg);
void glps_timer_stop(glps_timer *timer);
double glps_timer_elapsed_ms(glps_timer *timer);
double glps_timer_elapsed_us(glps_timer *timer);
void glps_timer_destroy(glps_timer *timer);
void glps_timer_check_and_call(glps_timer *timer);

#endif // GLPS_TIMER_H
