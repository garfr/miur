/* =====================
 * src/render.c
 * 03/05/2022
 * Renderer implemented in Vulkan.
 * ====================
 */

#include <stdbool.h>
#include <inttypes.h>

#include <miur/membuf.h>
#include <miur/bsl.h>
#include <miur/mem.h>
#include <miur/log.h>
#include <miur/render.h>
#include <miur/render_priv.h>
#include <miur/device.h>

/* === PROTOTYPES === */

bool recreate_swapchain(Renderer *render);
bool create_sync_objects(Renderer *render);
bool create_buffer(Renderer *render, VkDeviceSize size,
                   VkBufferUsageFlagBits bits,
                   VkMemoryPropertyFlagBits properties, VkBuffer *buffer,
                   VkDeviceMemory *memory);

/* === PUBLIC FUNCTIONS === */

void draw_triangle_callback(void *ud, VkCommandBuffer *buffer)
{
  Renderer *render = (Renderer *) ud;
  
}

void clear_color_triangle_callback(void *ud, VkClearColorValue *color)
{
  if (color != NULL)
  {
    color->float32[0] = 1.0f;
    color->float32[1] = 0.0f;
    color->float32[2] = 0.0f;
    color->float32[3] = 0.0f;
  }
}

Renderer *renderer_create(RendererBuilder *builder)
{
  VkResult err;
  enum cwin_error cwin_err;
  Renderer *render = MIUR_NEW(Renderer);
  if (render == NULL)
  {
    MIUR_LOG_ERR("Memory allocation failure while allocating renderer");
    goto cleanup;
  }

  render->window = builder->window;

  render->techniques_filename = builder->technique_filename;

  render->current_frame = 0;

  render->vk = create_vulkan_instance(builder);
  if (!render->vk)
  {
    goto cleanup;
  }
  MIUR_LOG_INFO("Created Vulkan instance");

  cwin_err = cwin_vk_create_surface(builder->window, render->vk,
                                                     &render->surface);
  if (cwin_err)
  {
    MIUR_LOG_INFO("Error creating Vulkan context from CWin window: %d",
                  cwin_err);
    goto cleanup;
  }

  MIUR_LOG_INFO("Created Vulkan surface");

  render->pdev = select_vulkan_physical_device(render->vk, render->surface,
                                               &render->queue_indices.graphics,
                                               &render->queue_indices.present);
  if (!render->pdev)
  {
    goto cleanup;
  }

  render->dev = create_vulkan_device(render->pdev, &render->graphics_queue,
                                     &render->present_queue,
                                     render->queue_indices.graphics,
                                     render->queue_indices.present);
  if (!render->dev)
  {
    goto cleanup;
  }

  MIUR_LOG_INFO("Created Vulkan device and queues");
  int width, height;
  cwin_window_get_size_pixels(builder->window, &width, &height);

  if (!create_vulkan_swapchain(&render->swapchain, render->pdev, render->dev,
                               render->surface, width, height,
                               (uint32_t *) &render->queue_indices))
  {
    goto cleanup;
  }
  MIUR_LOG_INFO("Created Vulkan swapchain");

  shader_cache_create(&render->shader_cache, &render->dev);
  technique_cache_create(&render->technique_cache);
  RenderGraphBuilder render_graph_builder = {
    .present_format = render->swapchain.format.format,
    .device = render->dev,
    .max_frames_in_flight = MAX_FRAMES_IN_FLIGHT,
    .graphics_queue_index = render->queue_indices.graphics,
    .present_extent = render->swapchain.extent,
    .present_image_count = render->swapchain.image_count,
    .present_image_views = render->swapchain.image_views,
  };

  if (!render_graph_create(&render->render_graph, &render_graph_builder))
  {
    MIUR_LOG_ERR("Failed to create render graph");
    goto cleanup;
  }

  render->triangle_pass = render_graph_add_pass(&render->render_graph,
                                                string_from_cstr("triangle"));

  render->triangle_pass->draw_callback = draw_triangle_callback;
  render->triangle_pass->clear_color_callback = clear_color_triangle_callback;
  render->triangle_pass->ud = render;
  render->present_texture =
    render_graph_create_texture(&render->render_graph,
                                string_from_cstr("present"));
  render_pass_add_color_output(&render->render_graph, render->triangle_pass,
                                 render->present_texture);

  render_graph_set_present(&render->render_graph, render->present_texture);

  render_graph_bake(&render->render_graph);

  Membuf technique_config;
  if (!membuf_load_file(&technique_config, builder->technique_filename))
  {
    MIUR_LOG_ERR("Failed to load technique config: '%s'",
                   builder->technique_filename);
    goto cleanup;
  }

  TechniqueLoadError technique_error;
  if (!technique_cache_load_technique_file(&render->technique_cache,
                                           &render->shader_cache,
                                           technique_config, &technique_error))
  {
    MIUR_LOG_ERR("Error parsing technique config fiel '%s'\n%d:%d: %s",
                 builder->technique_filename, technique_error.line,
                 technique_error.col, technique_error.msg);
    goto cleanup;
  }

  if (!create_sync_objects(render))
  {
    goto cleanup;
  }

  return render;
cleanup:
  if (render != NULL)
  {
    MIUR_FREE(render);
  }
  return NULL;
}

void renderer_destroy(Renderer *render)
{
  vkDeviceWaitIdle(render->dev);

  shader_cache_destroy(&render->shader_cache);
  technique_cache_destroy(&render->technique_cache);

  destroy_vulkan_swapchain(&render->swapchain, render->dev);

  render_graph_destroy(&render->render_graph);

  for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
  {
    vkDestroySemaphore(render->dev, render->image_available_semas[i], NULL);
    vkDestroySemaphore(render->dev, render->render_finished_semas[i], NULL);
    vkDestroyFence(render->dev, render->inflight_fences[i], NULL);
  }

  vkDestroyCommandPool(render->dev, render->command_pool, NULL);

  vkDestroyDevice(render->dev, NULL);
  vkDestroySurfaceKHR(render->vk, render->surface, NULL);
  vkDestroyInstance(render->vk, NULL);
  MIUR_FREE(render);
}

void renderer_configure(Renderer *render, RendererConfigure *cofigure)
{

//  cleanup_swapchain(render);
}

bool renderer_init_static_mesh(Renderer *render, StaticMesh *mesh)
{
  render->mesh = mesh;
  VkDeviceSize buffer_size = mesh->vert_count * 3 * sizeof(float);
  if (!create_buffer(render, buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &mesh->vert_bufs[0],
                     &mesh->pos_memory))
  {
    return false;
  }

  void *data;
  vkMapMemory(render->dev, mesh->pos_memory, 0, buffer_size, 0, &data);
  memcpy(data, mesh->verts_pos, buffer_size);
  vkUnmapMemory(render->dev, mesh->pos_memory);

  if (!create_buffer(render, buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &mesh->vert_bufs[1],
                     &mesh->norm_memory))
  {
    return false;
  }

  vkMapMemory(render->dev, mesh->norm_memory, 0, buffer_size, 0, &data);
  memcpy(data, mesh->verts_norm, buffer_size);
  vkUnmapMemory(render->dev, mesh->norm_memory);

  buffer_size = mesh->index_count * sizeof(uint16_t);
  if (!create_buffer(render, buffer_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &mesh->index_buf,
                     &mesh->index_memory))
  {
    return false;
  }

  vkMapMemory(render->dev, mesh->index_memory, 0, buffer_size, 0, &data);
  memcpy(data, mesh->indices, buffer_size);
  vkUnmapMemory(render->dev, mesh->index_memory);

  return true;
}

void renderer_deinit_static_mesh(Renderer *render, StaticMesh *mesh)
{
  vkDestroyBuffer(render->dev, mesh->vert_bufs[0], NULL);
  vkDestroyBuffer(render->dev, mesh->vert_bufs[1], NULL);
  vkDestroyBuffer(render->dev, mesh->index_buf, NULL);
  vkFreeMemory(render->dev, mesh->pos_memory, NULL);
  vkFreeMemory(render->dev, mesh->norm_memory, NULL);
  vkFreeMemory(render->dev, mesh->index_memory, NULL);
}

bool do_recreate = false;
void renderer_draw(Renderer *render)
{
  VkResult err;

  vkWaitForFences(render->dev, 1, &render->inflight_fences[render->current_frame],
                  VK_TRUE, UINT64_MAX);
  vkResetFences(render->dev, 1, &render->inflight_fences[render->current_frame]);

  err = vkAcquireNextImageKHR(render->dev, render->swapchain.swapchain, UINT64_MAX,
                        render->image_available_semas[render->current_frame],
                        VK_NULL_HANDLE, &render->image_index);

  if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
  {
    do_recreate = true;
  } else if (err)
  {
    print_vulkan_error(err);
    return;
  }

  if (!render_graph_draw(&render->render_graph, render->current_frame))
  {
    MIUR_LOG_ERR("Failed to draw render graph");
    return;
  }

/*   vkCmdBindPipeline(render->command_buffers[render->current_frame], */
/*                     VK_PIPELINE_BIND_POINT_GRAPHICS, */
/*                     render->techniques[0].pipeline); */
/*   VkDeviceSize offsets[] = {0, 0}; */

/*   vkCmdBindVertexBuffers(render->command_buffers[render->current_frame], */
/*                          0, 2, render->mesh->vert_bufs, offsets); */
/*   vkCmdBindIndexBuffer(render->command_buffers[render->current_frame], */
/*                        render->mesh->index_buf, 0, VK_INDEX_TYPE_UINT16); */

/*   vkCmdDrawIndexed(render->command_buffers[render->current_frame], */
/*                    6, */
/* //                   render->mesh->index_count, */
/*                    1, 0, 0, 0); */
/* //  vkCmdDraw(render->command_buffers[render->current_frame], 3, 1, 0, 0); */
/*   vkCmdEndRenderPass(render->command_buffers[render->current_frame]); */

  VkPipelineStageFlags wait_stages[] = {
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
  };

  VkSubmitInfo submit_info = {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &render->image_available_semas[render->current_frame],
    .pWaitDstStageMask = wait_stages,
    .commandBufferCount = 1,
    .pCommandBuffers =
    &render->render_graph.command_buffers[render->current_frame],
    .signalSemaphoreCount = 1,
    .pSignalSemaphores = &render->render_finished_semas[render->current_frame],
  };

  err = vkQueueSubmit(render->graphics_queue, 1, &submit_info,
                      render->inflight_fences[render->current_frame]);

  if (err)
  {
    print_vulkan_error(err);
    return;
  }

  VkPresentInfoKHR present_info = {
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &render->render_finished_semas[render->current_frame],
    .swapchainCount = 1,
    .pSwapchains = &render->swapchain.swapchain,
    .pImageIndices = &render->image_index,
  };

  err = vkQueuePresentKHR(render->present_queue, &present_info);
  if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
  {
    do_recreate = true;
  } else if (err)
  {
    print_vulkan_error(err);
  }

  render->current_frame = (render->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;

  if (do_recreate)
  {
    recreate_swapchain(render);
    do_recreate = false;
  }
}

void print_vulkan_error(VkResult err)
{
  MIUR_LOG_ERR("Vulkan error: %d", err);
}


bool create_sync_objects(Renderer *render)
{
  VkResult err;
  for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
  {
    VkSemaphoreCreateInfo semaphore_create_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    VkFenceCreateInfo fence_create_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    err = vkCreateSemaphore(render->dev, &semaphore_create_info, NULL,
                            &render->image_available_semas[i]);
    if (err)
    {
      print_vulkan_error(err);
      return false;
    }

    err = vkCreateSemaphore(render->dev, &semaphore_create_info, NULL,
                            &render->render_finished_semas[i]);
    if (err)
    {
      print_vulkan_error(err);
      return false;
    }

    err = vkCreateFence(render->dev, &fence_create_info, NULL,
                        &render->inflight_fences[i]);
    if (err)
    {
      print_vulkan_error(err);
      return false;
    }

  }

  return true;
}

bool
recreate_swapchain(Renderer *render)
{
  Swapchain old = render->swapchain;

  int width, height;
  cwin_window_get_size_pixels(render->window, &width, &height);
  Swapchain new = {0};

  create_vulkan_swapchain(&new, render->pdev, render->dev,
                          render->surface, width, height,
                          (uint32_t *) &render->queue_indices);
  render->swapchain = new;

  vkDeviceWaitIdle(render->dev);

  destroy_vulkan_swapchain(&old, render->dev);


  return true;
}

bool create_buffer(Renderer *render, VkDeviceSize size,
                   VkBufferUsageFlagBits usage,
                   VkMemoryPropertyFlagBits properties, VkBuffer *buffer,
                   VkDeviceMemory *memory)
{
  VkResult err;
  VkBufferCreateInfo buffer_create_info = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = size,
    .usage = usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  err = vkCreateBuffer(render->dev, &buffer_create_info, NULL, buffer);
  if (err)
  {
    print_vulkan_error(err);
    return false;
  }

  uint32_t memory_type;
  VkMemoryRequirements mem_required;
  vkGetBufferMemoryRequirements(render->dev, *buffer, &mem_required);
  VkPhysicalDeviceMemoryProperties mem_props;
  vkGetPhysicalDeviceMemoryProperties(render->pdev, &mem_props);

  for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
  {
    if (mem_required.memoryTypeBits & (1 << i) &&
        (mem_props.memoryTypes[i].propertyFlags) & properties)
      {
        memory_type = i;
        goto done;
      }
  }
    MIUR_LOG_ERR("couldn't find suitable memory type");
    return false;
  done:

  VkMemoryAllocateInfo alloc_info = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = mem_required.size,
    .memoryTypeIndex = memory_type,
  };

  err = vkAllocateMemory(render->dev, &alloc_info, NULL, memory);
  if (err)
  {
    print_vulkan_error(err);
    return false;
  }

  vkBindBufferMemory(render->dev, *buffer, *memory, 0);
  return true;
}
