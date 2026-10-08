#ifndef JOYEMU_MOLTENVK_INSTANCE_POLICY_H
#define JOYEMU_MOLTENVK_INSTANCE_POLICY_H

#include "../include/vulkan/vulkan_core.h"
#include "joyemu_vulkan_feature_policy.h"

/* VK_EXT_layer_settings ABI from Khronos Vulkan-Headers. The bundled Vulkan
 * header predates this extension; keep these private names until it is updated. */
typedef struct joyemu_vk_layer_setting
{
   const char *layer_name;
   const char *setting_name;
   int32_t type;
   uint32_t value_count;
   const void *values;
} joyemu_vk_layer_setting_t;

typedef struct joyemu_vk_layer_settings_info
{
   VkStructureType sType;
   const void *pNext;
   uint32_t setting_count;
   const joyemu_vk_layer_setting_t *settings;
} joyemu_vk_layer_settings_info_t;

typedef struct joyemu_moltenvk_instance_policy
{
   VkBool32 use_argument_buffers;
   joyemu_vk_layer_setting_t setting;
   joyemu_vk_layer_settings_info_t info;
} joyemu_moltenvk_instance_policy_t;

#define JOYEMU_VK_LAYER_SETTINGS_EXTENSION "VK_EXT_layer_settings"
#define JOYEMU_VK_LAYER_SETTINGS_STYPE ((VkStructureType)1000496000)

/* Lifetime is the synchronous vkCreateInstance call. Never mutate the caller's
 * pNext chain or process-wide MoltenVK configuration. An existing layer setting
 * chain belongs to the core; don't inject a duplicate Vulkan structure. */
static inline bool joyemu_moltenvk_prepare_instance_policy(
      VkInstanceCreateInfo *create_info,
      joyemu_moltenvk_instance_policy_t *policy,
      bool is_legacy_ios, bool extension_available)
{
   const VkBaseInStructure *next;
   if (!extension_available || !create_info->pApplicationInfo ||
       !joyemu_vulkan_should_use_discrete_resources(
          create_info->pApplicationInfo->pApplicationName, is_legacy_ios))
      return false;

   for (next = (const VkBaseInStructure *)create_info->pNext; next; next = next->pNext)
      if (next->sType == JOYEMU_VK_LAYER_SETTINGS_STYPE)
         return false;

   policy->use_argument_buffers = VK_FALSE;
   policy->setting.layer_name = "MoltenVK";
   policy->setting.setting_name = "MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS";
   policy->setting.type = 0; /* VK_LAYER_SETTING_TYPE_BOOL32_EXT */
   policy->setting.value_count = 1;
   policy->setting.values = &policy->use_argument_buffers;
   policy->info.sType = JOYEMU_VK_LAYER_SETTINGS_STYPE;
   policy->info.pNext = create_info->pNext;
   policy->info.setting_count = 1;
   policy->info.settings = &policy->setting;
   create_info->pNext = &policy->info;
   return true;
}

#endif
