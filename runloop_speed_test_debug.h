#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool joyemu_runloop_should_reset_frame_limit_timestamp_after_fastforward(
      bool was_fastmotion,
      bool is_fastmotion);
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

#ifdef __cplusplus
}
#endif
