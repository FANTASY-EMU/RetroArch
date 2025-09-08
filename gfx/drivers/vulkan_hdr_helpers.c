/* Helper functions for HDR support in Vulkan renderer
 * These functions are designed to work with MoltenVK on iOS/tvOS
 */

#include <string/stdstring.h>
#include <vulkan/vulkan.h>
#include <retro_assert.h>
#include <retro_math.h>
#include <retro_miscellaneous.h>
#include "vulkan_hdr_helpers.h"
#include "../common/vulkan_common.h"
#include "../../verbosity.h"
#if defined(HAVE_COCOATOUCH) || defined(TARGET_OS_TV)
#include "../../../PVRetroArchCore/Core/vulkan_ios_tvos_helpers.h"
#endif

#ifdef VULKAN_HDR_SWAPCHAIN

/* Helper function to update HDR UBO when the buffer cannot be mapped
 * This is particularly useful for iOS/tvOS where mapping may fail */
bool vulkan_update_hdr_ubo(
      vk_t *vk,
      VkCommandBuffer cmd,
      float max_nits,
      float paper_white_nits,
      float contrast,
      bool expand_gamut,
      float inverse_tonemap,
      float hdr10)
{
   if (!vk || !cmd || vk->hdr.ubo.buffer == VK_NULL_HANDLE)
      return false;

   /* If the buffer is mapped, update it directly */
   if (vk->hdr.ubo.mapped)
   {
      vulkan_hdr_uniform_t* mapped_ubo = (vulkan_hdr_uniform_t*)vk->hdr.ubo.mapped;
      mapped_ubo->mvp              = vk->mvp_no_rot;
      mapped_ubo->max_nits         = max_nits;
      mapped_ubo->paper_white_nits = paper_white_nits;
      mapped_ubo->contrast         = contrast;
      mapped_ubo->expand_gamut     = expand_gamut ? 1.0f : 0.0f;
      mapped_ubo->inverse_tonemap  = inverse_tonemap;
      mapped_ubo->hdr10            = hdr10;
      return true;
   }

   /* If the buffer is not mapped, use the staging buffer approach */
   vulkan_hdr_uniform_t ubo_data;
   ubo_data.mvp              = vk->mvp_no_rot;
   ubo_data.max_nits         = max_nits;
   ubo_data.paper_white_nits = paper_white_nits;
   ubo_data.contrast         = contrast;
   ubo_data.expand_gamut     = expand_gamut ? 1.0f : 0.0f;
   ubo_data.inverse_tonemap  = inverse_tonemap;
   ubo_data.hdr10            = hdr10;

#if defined(HAVE_COCOATOUCH) || defined(TARGET_OS_TV)
   /* Use the helper function to update the buffer on iOS/tvOS */
   if (vulkan_ios_tvos_update_buffer_without_mapping(vk->context->device, cmd,
         &vk->hdr.ubo, &ubo_data, sizeof(vulkan_hdr_uniform_t)))
   {
      RARCH_LOG("[Vulkan]: Updated HDR UBO using staging buffer for iOS/tvOS.\n");
      return true;
   }

   RARCH_ERR("[Vulkan]: Failed to update HDR UBO using staging buffer.\n");
#else
   /* On other platforms, try to map the buffer directly */
   void *data;
   if (vkMapMemory(vk->context->device, vk->hdr.ubo.memory, 0, sizeof(vulkan_hdr_uniform_t), 0, &data) == VK_SUCCESS)
   {
      memcpy(data, &ubo_data, sizeof(vulkan_hdr_uniform_t));
      vkUnmapMemory(vk->context->device, vk->hdr.ubo.memory);
      RARCH_LOG("[Vulkan]: Updated HDR UBO using direct mapping.\n");
      return true;
   }

   RARCH_ERR("[Vulkan]: Failed to map HDR UBO memory.\n");
#endif

   return false;
}

#endif /* VULKAN_HDR_SWAPCHAIN */
