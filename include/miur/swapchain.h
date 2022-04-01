/* =====================
 * include/miur/swapchain.h
 * 03/19/2022
 * Vulkan swapchain management.
 * ====================
 */

#ifndef MIUR_SWAPCHAIN_H
#define MIUR_SWAPCHAIN_H

#include <vulkan/vulkan.h>

#include <miur/render.h>

typedef struct
{
  VkSwapchainKHR swapchain;
  VkImage *images;
  VkImageView *image_views;
  VkFramebuffer *framebuffers;
  uint32_t image_count;
  VkExtent2D extent;
  VkSurfaceFormatKHR format;
} Swapchain;

typedef enum
{
  SWAPCHAIN_RESIZED,
  SWAPCHAIN_NORMAL,
  SWAPCHAIN_NOT_READY
} SwapchainStatus;

bool create_vulkan_swapchain(Swapchain *swapchain, VkPhysicalDevice pdev,
                             VkDevice dev,
                             VkSurfaceKHR surface, uint32_t width,
                             uint32_t height, uint32_t *queue_indices, 
                             Swapchain *old_swapchain);
void destroy_vulkan_swapchain(Swapchain *swapchain, VkDevice dev);
SwapchainStatus update_vulkan_swapchain(Swapchain *swapchain,
                                        VkPhysicalDevice pdev,
                                        VkDevice dev, VkSurfaceKHR surface,
                                        uint32_t *queue_indices);
#endif
