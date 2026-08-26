#ifndef JOYEMU_CADENCE_TRACE_COMPAT_H
#define JOYEMU_CADENCE_TRACE_COMPAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * JoyEmu's Apple build provides the real cross-boundary cadence recorder.
 * Keep this RetroArch fork buildable on its own and on non-Apple platforms:
 * when the host header is unavailable, every probe becomes an inline no-op.
 */
#if defined(__APPLE__) && defined(__has_include)
#if __has_include("../../JoyEngine/Platform/JECadenceTrace.h")
#include "../../JoyEngine/Platform/JECadenceTrace.h"
#define JOYEMU_HAS_CADENCE_TRACE 1
#endif
#endif

#ifndef JOYEMU_HAS_CADENCE_TRACE

typedef enum joyemu_cadence_trace_phase {
   JOYEMU_CADENCE_TRACE_DISPLAY_TICK_GAP = 0,
   JOYEMU_CADENCE_TRACE_OBSERVER_GAP,
   JOYEMU_CADENCE_TRACE_RUNLOOP_ITERATE,
   JOYEMU_CADENCE_TRACE_CORE_RUN,
   JOYEMU_CADENCE_TRACE_AUDIO_BLOCKED,
   JOYEMU_CADENCE_TRACE_METAL_SEMAPHORE_WAIT,
   JOYEMU_CADENCE_TRACE_NEXT_DRAWABLE_WAIT,
   JOYEMU_CADENCE_TRACE_PRESENT,
   JOYEMU_CADENCE_TRACE_TASK_QUEUE,
   JOYEMU_CADENCE_TRACE_GL_SWAP,
   JOYEMU_CADENCE_TRACE_GL_FLUSH,
   JOYEMU_CADENCE_TRACE_VULKAN_ACQUIRE,
   JOYEMU_CADENCE_TRACE_VULKAN_SUBMIT,
   JOYEMU_CADENCE_TRACE_VULKAN_PRESENT,
   JOYEMU_CADENCE_TRACE_VULKAN_READBACK,
   JOYEMU_CADENCE_TRACE_PHASE_COUNT
} joyemu_cadence_trace_phase_t;

typedef struct joyemu_cadence_trace_token {
   uint64_t started_ticks;
   uint64_t signpost_id;
   uint64_t session_id;
   uint64_t window_id;
   bool active;
} joyemu_cadence_trace_token_t;

static inline joyemu_cadence_trace_token_t joyemu_cadence_trace_begin(
      joyemu_cadence_trace_phase_t phase)
{
   joyemu_cadence_trace_token_t token = {0};
   (void)phase;
   return token;
}

static inline void joyemu_cadence_trace_end(
      joyemu_cadence_trace_phase_t phase,
      joyemu_cadence_trace_token_t token)
{
   (void)phase;
   (void)token;
}

static inline void joyemu_cadence_trace_mark_gap(
      joyemu_cadence_trace_phase_t phase)
{
   (void)phase;
}

static inline void joyemu_cadence_trace_record_vulkan_readback_missing_image(
      void)
{
}

static inline void joyemu_audio_benchmark_note_underflow(
      uint64_t concealed_frames)
{
   (void)concealed_frames;
}

static inline void joyemu_audio_benchmark_snapshot_and_reset(
      uint64_t *underflow_count,
      uint64_t *concealed_frame_count)
{
   if (underflow_count)
      *underflow_count = 0;
   if (concealed_frame_count)
      *concealed_frame_count = 0;
}

#endif

#endif
