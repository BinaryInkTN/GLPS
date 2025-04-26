

#ifndef GLPS_TIMER_H
#define GLPS_TIMER_H
#include <stdlib.h>
#ifdef GLPS_USE_WIN32
#include <windows.h>
#else
#include <time.h>
#endif

typedef struct
{
#ifdef GLPS_USE_WIN32
    LARGE_INTEGER start;
    LARGE_INTEGER end;
    LARGE_INTEGER frequency;
#else
    struct timespec start;
    struct timespec end;
#endif
} glps_timer;

glps_timer *glps_timer_init(void);
void glps_timer_start(glps_timer *timer);
void glps_timer_stop(glps_timer *timer);
double glps_timer_elapsed_ms(glps_timer *timer);
double glps_timer_elapsed_us(glps_timer *timer);
void glps_timer_destroy(glps_timer *timer);

#endif