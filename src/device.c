/* =====================
 * src/device.c
 * 03/19/2022
 * Vulkan device creation.
 * ====================
 */

#include <vulkan/vulkan.h>

#include <miur/render.h>
#include <miur/render_priv.h>
#include <miur/mem.h>
#include <miur/log.h>

const char *layers[] = {
  "VK_LAYER_KHRONOS_validation",
};

static const char *device_extensions[] = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

VkInstance create_vulkan_instance(RendererBuilder *builder)
{
  VkResult err;
  VkInstance vk;
  uint32_t extension_count;
  const char **extensions;

  VkApplicationInfo app_info = {
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pApplicationName = builder->name,
    .applicationVersion = builder->version,
    .pEngineName = "MIUR",
  };

  cwin_vk_get_required_extensions(builder->window, NULL, &extension_count);
  extensions = MIUR_ARR(const char *, extension_count);
  cwin_vk_get_required_extensions(builder->window, extensions,
                                  &extension_count);

  VkInstanceCreateInfo instance_create_info = {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pApplicationInfo = &app_info,
    .ppEnabledExtensionNames = extensions,
    .enabledExtensionCount = extension_count,
    .ppEnabledLayerNames = layers,
    .enabledLayerCount = sizeof(layers) / sizeof(layers[0]),
  };

  err = vkCreateInstance(&instance_create_info, NULL, &vk);
  if (err)
  {
    print_vulkan_error(err);
    MIUR_FREE(extensions);
    return VK_NULL_HANDLE;
  }

  MIUR_FREE(extensions);
  return vk;
}

VkPhysicalDevice select_vulkan_physical_device(VkInstance vk,
                                               VkSurfaceKHR surface,
                                               uint32_t *graphics_index,
                                               uint32_t *present_index)
{
  VkResult err;
  VkPhysicalDevice *devices;
  uint32_t device_count = 0;

  err = vkEnumeratePhysicalDevices(vk, &device_count, NULL);
  if (err)
  {
    return VK_NULL_HANDLE;
  }

  devices = MIUR_ARR(VkPhysicalDevice, device_count);
  if (devices == NULL)
  {
    return VK_NULL_HANDLE;
  }

  err = vkEnumeratePhysicalDevices(vk, &device_count, devices);
  if (err)
  {
    return VK_NULL_HANDLE;
  }


  for (uint32_t i = 0; i < device_count; i++)
  {
    VkPhysicalDevice device = devices[i];
    VkPhysicalDeviceProperties props;
    VkQueueFamilyProperties *queues;
    uint32_t queue_count;
    bool has_present_support = false;
    bool has_graphics_support = false;

    vkGetPhysicalDeviceProperties(device, &props);

    /* Find all the queues and their properties supported by this device. */
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, NULL);

    queues = MIUR_ARR(VkQueueFamilyProperties, queue_count);
    if (queues == NULL)
    {
      MIUR_LOG_ERR("Memory allocation failure while allocating queue "
                   "properties");
      MIUR_FREE(devices);
      return VK_NULL_HANDLE;
    }

    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, queues);

    /* Review the device properties and decide if its good enough. */
    for (uint32_t j = 0; j < queue_count; j++)
    {
      VkQueueFamilyProperties *queue = &queues[j];
      VkBool32 present_support;

      if (queue->queueFlags & VK_QUEUE_GRAPHICS_BIT)
      {
        has_graphics_support = true;
        *graphics_index = j;
      }

      vkGetPhysicalDeviceSurfaceSupportKHR(device, j, surface,
                                           &present_support);
      if (present_support)
      {
        has_present_support = true;
        *present_index = j;
      }
    }

    uint32_t extension_count;
    VkExtensionProperties *available_extensions;
    bool has_swapchain_support = true;
    vkEnumerateDeviceExtensionProperties(device, NULL, &extension_count, NULL);
    available_extensions = MIUR_ARR(VkExtensionProperties, extension_count);
    vkEnumerateDeviceExtensionProperties(device, NULL, &extension_count,
                                         available_extensions);

    for (uint32_t j = 0; j < sizeof(device_extensions) /
           sizeof(device_extensions[0]); j++)
    {
      for (uint32_t k = 0; k < extension_count; k++)
      {
        if (strcmp(available_extensions[k].extensionName,
                   device_extensions[j]) == 0)
        {
          goto found;
        }
      }
      has_swapchain_support = false;
    found:;
    }

    MIUR_FREE(available_extensions);
    MIUR_FREE(queues);

    if (has_graphics_support && has_present_support && has_swapchain_support)
    {
      MIUR_LOG_INFO("Found suitable physical device '%s'", props.deviceName);
      MIUR_FREE(devices);
      return device;
    }
  }

  MIUR_LOG_INFO("Could not find suitable device");
  MIUR_FREE(devices);
  return VK_NULL_HANDLE;
}

VkDevice create_vulkan_device(VkPhysicalDevice pdev, VkQueue *graphics_queue,
                              VkQueue *present_queue, uint32_t graphics_index,
                              uint32_t present_index)
{
  VkResult err;
  float queue_priority = 1.0f;
  VkDevice dev;

  VkDeviceQueueCreateInfo queue_create_infos[] = {
    {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = graphics_index,
      .queueCount = 1,
      .pQueuePriorities = &queue_priority,
    },
    {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = present_index,
      .queueCount = 1,
      .pQueuePriorities = &queue_priority,
    },
  };

  uint32_t num_unique_queues = present_index == graphics_index ? 1 : 2;

  VkDeviceCreateInfo device_create_info = {
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .queueCreateInfoCount = num_unique_queues,
    .pQueueCreateInfos = queue_create_infos,
    .enabledExtensionCount = sizeof(device_extensions) /
    sizeof(device_extensions[0]),
    .ppEnabledExtensionNames = device_extensions,
  };

  err = vkCreateDevice(pdev, &device_create_info, NULL, &dev);
  if (err)
  {
    print_vulkan_error(err);
    return VK_NULL_HANDLE;
  }

  vkGetDeviceQueue(dev, present_index, 0, present_queue);
  vkGetDeviceQueue(dev, graphics_index, 0, graphics_queue);

  return dev;
}
