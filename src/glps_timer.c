#include "glps_timer.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef GLPS_USE_WIN32
#include <windows.h>
static uint64_t now_ms(void)
{
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER li = {.LowPart = ft.dwLowDateTime,
                       .HighPart = ft.dwHighDateTime};
  return li.QuadPart / 10000;
}
#else
#include <sys/time.h>
static uint64_t now_ms(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}
#endif

glps_timer *glps_timer_init(void)
{
  glps_timer *timer = (glps_timer *)calloc(1, sizeof(glps_timer));
  timer->is_valid = false;
  return timer;
}

void glps_timer_start(glps_timer *timer, uint64_t duration_ms,
                      timer_callback callback, void *arg)
{
  if (timer)
  {
    timer->start_time_ms = now_ms();
    timer->duration_ms = duration_ms;
    timer->callback = callback;
    timer->callback_arg = arg;
    timer->is_valid = true;
  }
}

void glps_timer_stop(glps_timer *timer)
{
  if (timer)
  {
    timer->is_valid = false;
  }
}

double glps_timer_elapsed_ms(glps_timer *timer)
{
  if (!timer || !timer->is_valid)
    return 0.0;
  return (double)(now_ms() - timer->start_time_ms);
}

double glps_timer_elapsed_us(glps_timer *timer)
{
  if (!timer || !timer->is_valid)
    return 0.0;
  return (double)(now_ms() - timer->start_time_ms) * 1000.0;
}

void glps_timer_destroy(glps_timer *timer)
{
  if (timer != NULL)
  {
    free(timer);
  }
}

void glps_timer_check_and_call(glps_timer *timer)
{
  if (timer && timer->is_valid && timer->callback)
  {
    uint64_t elapsed = now_ms() - timer->start_time_ms;
    if (elapsed >= timer->duration_ms)
    {
      timer->callback(timer->callback_arg);
      timer->is_valid = false;  // Mark timer as expired
    }
  }
}