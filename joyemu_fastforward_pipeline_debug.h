#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct joyemu_fastforward_pipeline_window
{
   uint64_t requested;
   uint64_t completed;
   int64_t started_at_us;
} joyemu_fastforward_pipeline_window_t;

typedef struct joyemu_fastforward_pipeline_snapshot
{
   uint64_t requested;
   uint64_t completed;
   uint64_t dropped;
   int64_t elapsed_us;
} joyemu_fastforward_pipeline_snapshot_t;

static inline void joyemu_fastforward_pipeline_reset(
      joyemu_fastforward_pipeline_window_t *window)
{
   if (!window)
      return;

   window->requested     = 0;
   window->completed     = 0;
   window->started_at_us = 0;
}

static inline bool joyemu_fastforward_pipeline_note(
      joyemu_fastforward_pipeline_window_t *window,
      bool completed,
      int64_t now_us,
      int64_t report_interval_us,
      joyemu_fastforward_pipeline_snapshot_t *snapshot)
{
   int64_t elapsed_us;

   if (!window || !snapshot || report_interval_us <= 0)
      return false;

   if (!window->started_at_us)
      window->started_at_us = now_us;

   window->requested++;
   if (completed)
      window->completed++;

   elapsed_us = now_us - window->started_at_us;
   if (elapsed_us < report_interval_us)
      return false;

   snapshot->requested  = window->requested;
   snapshot->completed  = window->completed;
   snapshot->dropped    = window->requested - window->completed;
   snapshot->elapsed_us = elapsed_us;

   window->requested     = 0;
   window->completed     = 0;
   window->started_at_us = now_us;
   return true;
}
