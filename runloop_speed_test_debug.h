#pragma once

#include <stdbool.h>
#include <stddef.h>

#if DEBUG && defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <TargetConditionals.h>
#include <stdarg.h>
#include <stdio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

bool joyemu_runloop_should_reset_frame_limit_timestamp_after_fastforward(
      bool was_fastmotion,
      bool is_fastmotion,
      float previous_ratio,
      float next_ratio);
bool joyemu_runloop_should_log_core_message(unsigned target);
bool joyemu_runloop_should_display_core_message_on_osd(unsigned target);

#if DEBUG
typedef struct je_speed_test_frame_stats
{
   double average_ms;
   double max_ms;
   double min_ms;
   int long_frame_count;
   int short_frame_count;
} je_speed_test_frame_stats_t;

bool je_speed_test_is_long_frame(double frame_delta_ms, double target_frame_ms);
bool je_speed_test_is_short_frame(double frame_delta_ms, double target_frame_ms);
bool je_speed_test_should_warn_long_frame(double frame_delta_ms, double target_frame_ms);
bool je_speed_test_should_warn_short_frame(double frame_delta_ms, double target_frame_ms);
bool je_speed_test_should_reset_window(bool runloop_paused, bool runloop_idle);
bool joyemu_speed_test_should_reset_sampling_window(
      bool runloop_paused,
      bool runloop_idle,
      bool fastmotion);
je_speed_test_frame_stats_t je_speed_test_analyze_frame_deltas(
      const double *samples,
      size_t count,
      double target_frame_ms);
void je_speed_test_reset(void);
void je_speed_test_set_rotation_suppressed(bool suppressed);
#endif

#if DEBUG && defined(__APPLE__)
static inline bool joyemu_runloop_fastforward_trace_enabled(void)
{
   static int s_cached_enabled = -1;
   if (s_cached_enabled >= 0)
      return s_cached_enabled != 0;

   Boolean key_exists = false;
   Boolean enabled = CFPreferencesGetAppBooleanValue(
         CFSTR("JoyEMU.JoyEngine.FastForwardTraceEnabled"),
         kCFPreferencesCurrentApplication,
         &key_exists);

   /* Keep FFTrace available for targeted device diagnostics,
    * but default to disabled unless explicitly enabled. */
   s_cached_enabled = key_exists ? (enabled ? 1 : 0) : 0;
   return s_cached_enabled != 0;
}
#else
static inline bool joyemu_runloop_fastforward_trace_enabled(void)
{
   return false;
}
#endif

#if DEBUG && defined(__APPLE__)
static inline void joyemu_runloop_fastforward_trace_stderr(const char *format, ...)
{
#if !TARGET_OS_SIMULATOR && !TARGET_OS_OSX
   char buffer[1024];
   va_list args;

   va_start(args, format);
   vsnprintf(buffer, sizeof(buffer), format, args);
   va_end(args);

   fputs(buffer, stderr);
   fflush(stderr);
#else
   (void)format;
#endif
}
#else
static inline void joyemu_runloop_fastforward_trace_stderr(const char *format, ...)
{
   (void)format;
}
#endif

#define JOYEMU_FFTRACE_LOG(...) \
   do { \
      RARCH_LOG(__VA_ARGS__); \
      joyemu_runloop_fastforward_trace_stderr(__VA_ARGS__); \
   } while (0)

#ifdef __cplusplus
}
#endif
