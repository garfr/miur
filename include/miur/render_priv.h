/* =====================
 * include/miur/render_priv.h
 * 03/19/2022
 * Renderer internals.
 * ====================
 */

#ifndef MIUR_RENDER_PRIV_H
#define MIUR_RENDER_PRIV_H

#include <vulkan/vulkan.h>

#include <miur/render.h>
#include <miur/swapchain.h>
#include <miur/shader.h>
#include <miur/membuf.h>
#include <miur/material.h>
#include <miur/render_graph.h>

#define MAX_FRAMES_IN_FLIGHT 1

struct Renderer
{
  VkInstance vk;
  VkPhysicalDevice pdev;
  struct {
    uint32_t graphics;
    uint32_t present;
  } queue_indices;
  VkDevice dev;
  VkSurfaceKHR surface;
  VkQueue graphics_queue;
  VkQueue present_queue;
  Swapchain swapchain;

  StaticMesh *mesh;

  ShaderCache shader_cache;
  TechniqueCache technique_cache;

  RenderGraph render_graph;
  RenderPass *triangle_pass, *shadow_pass;
  RenderGraphTexture *gbuffer, *present_texture;

  struct cwin_window *window;

  VkCommandPool command_pool;
  VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT];

  VkSemaphore image_available_semas[MAX_FRAMES_IN_FLIGHT];
  VkSemaphore render_finished_semas[MAX_FRAMES_IN_FLIGHT];
  VkFence inflight_fences[MAX_FRAMES_IN_FLIGHT];
  uint32_t image_index;
  uint32_t current_frame;

  const char *techniques_filename;
};

void print_vulkan_error(VkResult err);

#endif
