#ifndef JOYEMU_VULKAN_FEATURE_POLICY_H
#define JOYEMU_VULKAN_FEATURE_POLICY_H

#include <stdbool.h>
#include <string.h>

static inline bool joyemu_vulkan_should_mask_cull_distance(
      const char *core_name, bool uses_moltenvk)
{
   return uses_moltenvk && core_name && strcmp(core_name, "PPSSPP") == 0;
}

static inline bool joyemu_vulkan_should_use_discrete_resources(
      const char *core_name, bool is_legacy_ios)
{
   return is_legacy_ios && core_name && strcmp(core_name, "PPSSPP") == 0;
}

#endif
