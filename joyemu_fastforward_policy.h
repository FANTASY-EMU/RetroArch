#pragma once

#include <stdbool.h>

static inline bool joyemu_frontend_fastforward_frameskip_is_active(
      bool input_nonblock,
      bool frameskip_enabled,
      bool menu_is_alive)
{
   return input_nonblock && frameskip_enabled && !menu_is_alive;
}

static inline int joyemu_cocoa_gl_fast_forward_skip_count(
      bool is_syncing,
      bool frontend_frameskip_active)
{
   if (is_syncing || frontend_frameskip_active)
      return 0;

   return 3;
}

static inline bool joyemu_cocoa_gl_fast_forward_should_present(
      int *remaining_skips,
      int configured_skip_count)
{
   if (!remaining_skips)
      return false;

   if (configured_skip_count == 0)
      *remaining_skips = 0;

   if (--(*remaining_skips) >= 0)
      return false;

   *remaining_skips = configured_skip_count;
   return true;
}
