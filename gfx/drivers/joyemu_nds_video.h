#ifndef JOYEMU_NDS_VIDEO_H
#define JOYEMU_NDS_VIDEO_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
/* Private, synchronous extension. Unsupported frontends return false.
 * Source pointers are borrowed until the immediately following video_refresh.
 * Accepted frames use RETRO_HW_FRAME_BUFFER_VALID and unchanged canvas geometry.
 * Do not change this layout without incrementing version. */
#define JOYEMU_ENVIRONMENT_NDS_NATIVE_FRAME 0x4A440100u
#define JOYEMU_NDS_VIDEO_VERSION 1u
#define JOYEMU_NDS_VIDEO_FALLBACK 0u
#define JOYEMU_NDS_VIDEO_ACCEPTED 1u
#define JOYEMU_NDS_VIDEO_SKIPPED 2u
struct joyemu_nds_rect { int32_t x, y, width, height; };
struct joyemu_nds_native_frame {
 uint32_t version, size;
 const uint32_t *top, *bottom;
 uint32_t canvas_width, canvas_height;
 struct joyemu_nds_rect top_rect, bottom_rect;
 uint32_t result;
};
static inline bool joyemu_nds_rect_valid(struct joyemu_nds_rect r, uint32_t w, uint32_t h) {
 if (r.x < 0 || r.y < 0 || r.width < 0 || r.height < 0) return false;
 if (r.width == 0 && r.height == 0) return (uint32_t)r.x <= w && (uint32_t)r.y <= h;
 return r.width > 0 && r.height > 0 && (int64_t)r.x+r.width <= w && (int64_t)r.y+r.height <= h;
}
static inline bool joyemu_nds_frame_valid(const struct joyemu_nds_native_frame *f) {
 return f && f->version == JOYEMU_NDS_VIDEO_VERSION && f->size >= sizeof(*f)
  && f->top && f->bottom && f->canvas_width > 0 && f->canvas_height > 0
  && f->canvas_width <= 8192 && f->canvas_height <= 8192
  && (uint64_t)f->canvas_width*f->canvas_height <= 16u*1024u*1024u
  && joyemu_nds_rect_valid(f->top_rect,f->canvas_width,f->canvas_height)
  && joyemu_nds_rect_valid(f->bottom_rect,f->canvas_width,f->canvas_height);
}
static inline bool joyemu_nds_should_skip(uint64_t last_us, uint64_t now_us,
 uint64_t interval_us, bool fast_forward, bool layout_changed) {
 return fast_forward && !layout_changed && last_us && now_us >= last_us
  && interval_us && now_us-last_us < interval_us;
}
/* Keep the fractional interval remainder: the clock tracks a completed
 * cadence boundary, not the arrival timestamp of the latest core frame.
 * Only a successfully encoded presentation may consume an opportunity. */
static inline uint64_t joyemu_nds_pacing_completed(uint64_t last_us, uint64_t now_us,
 uint64_t interval_us, bool fast_forward, bool layout_changed, bool presented) {
 if (!presented) return last_us;
 if (!fast_forward) return 0;
 if (!last_us || layout_changed || now_us < last_us || !interval_us) return now_us;
 return now_us - (now_us-last_us) % interval_us;
}
#ifdef __cplusplus
extern "C" {
#endif
bool joyemu_metal_stage_nds_frame(struct joyemu_nds_native_frame *frame);
#ifdef __cplusplus
}
#endif
#endif
