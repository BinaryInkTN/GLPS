#include "glps_timer.h"

#include <stdio.h>

glps_timer *glps_timer_init(void)
{
    glps_timer *timer = (glps_timer *)calloc(1, sizeof(glps_timer));
    if (!timer)
    {
        fprintf(stderr, "Failed to allocate memory for timer\n");
        return NULL;
    }
}


void glps_timer_start(glps_timer *timer)
{
#ifdef GLPS_USE_WIN32
    QueryPerformanceFrequency(&timer->frequency);
    QueryPerformanceCounter(&timer->start);
#else
    clock_gettime(CLOCK_MONOTONIC, &timer->start);
#endif
}

void glps_timer_stop(glps_timer *timer)
{
#ifdef GLPS_USE_WIN32
    QueryPerformanceCounter(&timer->end);
#else
    clock_gettime(CLOCK_MONOTONIC, &timer->end);
#endif
}

double glps_timer_elapsed_ms(glps_timer *timer)
{
#ifdef GLPS_USE_WIN32
    return ((timer->end.QuadPart - timer->start.QuadPart) * 1000.0) / timer->frequency.QuadPart;
#else
    long seconds = timer->end.tv_sec - timer->start.tv_sec;
    long nanoseconds = timer->end.tv_nsec - timer->start.tv_nsec;
    return (seconds * 1000.0) + (nanoseconds / 1000000.0);
#endif
}

double glps_timer_elapsed_us(glps_timer *timer)
{
#ifdef GLPS_USE_WIN32
    return ((timer->end.QuadPart - timer->start.QuadPart) * 1000000.0) / timer->frequency.QuadPart;
#else
    long seconds = timer->end.tv_sec - timer->start.tv_sec;
    long nanoseconds = timer->end.tv_nsec - timer->start.tv_nsec;
    return (seconds * 1000000.0) + (nanoseconds / 1000.0);
#endif
}

void glps_timer_destroy(glps_timer *timer)
{
    if (timer)
    {
        free(timer);
    }
}