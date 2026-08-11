#include <assert.h>

#include "../../gfx/drivers_context/cocoa_gl_fastforward_policy.h"

int main(void)
{
   assert(joyemu_cocoa_gl_fast_forward_skip_count(true, false) == 0);
   assert(joyemu_cocoa_gl_fast_forward_skip_count(false, true) == 0);
   assert(joyemu_cocoa_gl_fast_forward_skip_count(false, false) == 3);
   return 0;
}
