/* =====================
 * include/miur/device.h
 * 03/19/2022
 * Vulkan device creation.
 * ====================
 */

#ifndef MIUR_DEVICE_H
#define MIUR_DEVICE_H

#include <cwin.h>

#include <miur/render_priv.h>

VkInstance create_vulkan_instance(RendererBuilder *builder, 
    VkDebugUtilsMessengerEXT *messenger);

VkPhysicalDevice select_vulkan_physical_device(VkInstance vk,
                                               VkSurfaceKHR surface,
                                               uint32_t *graphics_index,
                                               uint32_t *present_index);

VkDevice create_vulkan_device(VkPhysicalDevice pdev, VkQueue *graphics_queue,
                              VkQueue *present_queue, uint32_t graphics_index,
                              uint32_t present_index);
#endif
