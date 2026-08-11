#pragma once

#include <stdbool.h>

static inline int joyemu_cocoa_gl_fast_forward_skip_count(
      bool is_syncing,
      bool frontend_frameskip_enabled)
{
   if (is_syncing || frontend_frameskip_enabled)
      return 0;

   return 3;
}
