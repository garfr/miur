/* =====================
 * src/swapchain.c
 * 03/19/2022
 * Vulkan swapchain management.
 * ====================
 */

#include <miur/mem.h>
#include <miur/log.h>
#include <miur/swapchain.h>

bool create_vulkan_swapchain(Swapchain *swapchain, VkPhysicalDevice pdev,
                             VkDevice dev,
                             VkSurfaceKHR surface, uint32_t width,
                             uint32_t height, uint32_t *queue_indices,
                             Swapchain *old_swapchain)
{
  VkSurfaceCapabilitiesKHR capabilities;
  VkResult err;

  VkSurfaceFormatKHR *surface_formats;
  uint32_t surface_format_count;
  VkPresentModeKHR *surface_present_modes;
  uint32_t surface_present_mode_count;

  VkPresentModeKHR present_mode;
  VkSurfaceFormatKHR surface_format;

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pdev, surface, &capabilities);

  vkGetPhysicalDeviceSurfaceFormatsKHR(pdev, surface, &surface_format_count,
                                       NULL);
  surface_formats = MIUR_ARR(VkSurfaceFormatKHR,
                             surface_format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(pdev, surface, &surface_format_count,
                                       surface_formats);

  vkGetPhysicalDeviceSurfacePresentModesKHR(pdev, surface,
                                            &surface_present_mode_count,
                                            NULL);
  surface_present_modes = MIUR_ARR(VkPresentModeKHR,
                                   surface_present_mode_count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(pdev, surface,
                                            &surface_present_mode_count,
                                            surface_present_modes);

  for (uint32_t i = 0; i < surface_format_count; i++)
  {
    VkSurfaceFormatKHR format = surface_formats[i];
    if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
        format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
    {
      surface_format = format;
      goto found_format;
    }
  }
  surface_format = surface_formats[0];
found_format:

  for (uint32_t i = 0; i < surface_present_mode_count; i++)
  {
    VkPresentModeKHR mode = surface_present_modes[i];
    if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
    {
      present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
      goto found_mailbox;
    }
  }
  present_mode = VK_PRESENT_MODE_FIFO_KHR;
found_mailbox:

  if (capabilities.currentExtent.width != 0xFFFFFFFF) {
    swapchain->extent = capabilities.currentExtent;
  } else
  {
    if (width > capabilities.maxImageExtent.width)
    {
      width = capabilities.maxImageExtent.width;
    }
    else if (width < capabilities.minImageExtent.width)
    {
      width = capabilities.minImageExtent.width;
    }
    if (height > capabilities.maxImageExtent.height)
    {
      height = capabilities.maxImageExtent.height;
    }
    else if (height < capabilities.minImageExtent.height)
    {
      height = capabilities.minImageExtent.height;
    }

    swapchain->extent.width = width;
    swapchain->extent.height = height;
  }

  swapchain->image_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 &&
      swapchain->image_count > capabilities.maxImageCount)
  {
    swapchain->image_count = capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR swapchain_create_info = {
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = surface,
    .minImageCount = swapchain->image_count,
    .imageFormat = surface_format.format,
    .imageColorSpace = surface_format.colorSpace,
    .imageExtent = swapchain->extent,
    .imageArrayLayers = 1,
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .preTransform = capabilities.currentTransform,
    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode = present_mode,
    .clipped = VK_TRUE,
    .oldSwapchain = VK_NULL_HANDLE,
  };

  if (old_swapchain != NULL)
  {
    swapchain_create_info.oldSwapchain = old_swapchain->swapchain;
  }

  if (queue_indices[0] != queue_indices[1])
  {
    swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    swapchain_create_info.queueFamilyIndexCount = 2;
    swapchain_create_info.pQueueFamilyIndices = queue_indices;
  } else
  {
    swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }

  err = vkCreateSwapchainKHR(dev, &swapchain_create_info, NULL,
                             &swapchain->swapchain);
  if (err)
  {
    MIUR_LOG_ERR("Failed to create swapchain");
    print_vulkan_error(err);
    return false;
  }

  vkGetSwapchainImagesKHR(dev, swapchain->swapchain, &swapchain->image_count,
                          NULL);
  swapchain->images = MIUR_ARR(VkImage, swapchain->image_count);
  vkGetSwapchainImagesKHR(dev, swapchain->swapchain,
                          &swapchain->image_count,
                          swapchain->images);

  swapchain->image_views = MIUR_ARR(VkImageView,
                                    swapchain->image_count);

  for (uint32_t i = 0; i < swapchain->image_count; i++)
  {
    VkImageViewCreateInfo view_create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = swapchain->images[i],
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = surface_format.format,
      .components = {
        .r = VK_COMPONENT_SWIZZLE_IDENTITY,
        .g = VK_COMPONENT_SWIZZLE_IDENTITY,
        .b = VK_COMPONENT_SWIZZLE_IDENTITY,
        .a = VK_COMPONENT_SWIZZLE_IDENTITY,
      },
      .subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
      },
    };

    err = vkCreateImageView(dev, &view_create_info, NULL,
                            &swapchain->image_views[i]);
    if (err)
    {
      print_vulkan_error(err);
      return false;
    }
  }

  MIUR_FREE(surface_formats);
  MIUR_FREE(surface_present_modes);
  swapchain->format = surface_format;
  return true;
}

void destroy_vulkan_swapchain(Swapchain *swapchain, VkDevice dev)
{
  for (uint32_t i = 0; i < swapchain->image_count; i++)
  {
//    vkDestroyFramebuffer(dev, swapchain->framebuffers[i], NULL);
  }

  for (uint32_t i = 0; i < swapchain->image_count; i++)
  {
    vkDestroyImageView(dev, swapchain->image_views[i], NULL);
  }

  MIUR_FREE(swapchain->framebuffers);
  MIUR_FREE(swapchain->images);
  MIUR_FREE(swapchain->image_views);

  vkDestroySwapchainKHR(dev, swapchain->swapchain, NULL);
}

