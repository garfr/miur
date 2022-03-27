/* =====================
 * src/render_graph.c
 * 03/25/2022
 * Render graph for organizing GPU sync.
 * ====================
 */

#include <miur/render_graph.h>

#define VECTOR_TYPE RenderPass
#define VECTOR_IMPLEMENTATION
#define VECTOR_FUN_PREFIX render_pass_vec_
#define VECTOR_TYPE_PREFIX RenderPass
#include <miur/vector.c.h>

#define VECTOR_TYPE RenderGraphTexture *
#define VECTOR_IMPLEMENTATION
#define VECTOR_FUN_PREFIX texture_ptr_vec_
#define VECTOR_TYPE_PREFIX RenderGraphTexturePtr
#include <miur/vector.c.h>

#define MAP_KEY_TYPE String
#define MAP_VAL_TYPE size_t
#define MAP_TYPE_PREFIX RenderPass
#define MAP_FUN_PREFIX render_pass_map_
#define MAP_EQ_FUN string_eq
#define MAP_HASH_FUN string_hash
#define MAP_HEADER
#define MAP_NO_TYPES
#define MAP_KEY_DESTRUCTOR string_libc_destroy
#define MAP_IMPLEMENTATION
#include <miur/map.c.h>

#define VECTOR_TYPE RenderGraphTexture
#define VECTOR_IMPLEMENTATION
#define VECTOR_FUN_PREFIX texture_vec_
#define VECTOR_TYPE_PREFIX RenderGraphTexture
#include <miur/vector.c.h>

#define MAP_KEY_TYPE String
#define MAP_VAL_TYPE size_t
#define MAP_TYPE_PREFIX RenderGraphTexture
#define MAP_FUN_PREFIX texture_map_
#define MAP_EQ_FUN string_eq
#define MAP_HASH_FUN string_hash
#define MAP_HEADER
#define MAP_NO_TYPES
#define MAP_KEY_DESTRUCTOR string_libc_destroy
#define MAP_IMPLEMENTATION
#include <miur/map.c.h>

#define VECTOR_TYPE BakedRenderPass
#define VECTOR_IMPLEMENTATION
#define VECTOR_FUN_PREFIX baked_pass_vec_
#define VECTOR_TYPE_PREFIX BakedPass
#include <miur/vector.c.h>

/* === PROTOTYPES === */

bool topological_sort(BakedPassVec *baked, RenderPassVec *passes);

/* === PUBLIC FUNCTIONS === */

bool render_graph_create(RenderGraph *graph, RenderGraphBuilder *builder)
{
  VkResult err;
  render_pass_vec_create(&graph->passes);
  render_pass_map_create(&graph->pass_index_map);
  texture_vec_create(&graph->textures);
  texture_map_create(&graph->texture_index_map);
  graph->present_format = builder->present_format;
  graph->present_extent = builder->present_extent;
  graph->device = builder->device;
  graph->max_frames_in_flight = builder->max_frames_in_flight;
  graph->present_image_views = builder->present_image_views;
  graph->present_image_count = builder->present_image_count;

  VkCommandPoolCreateInfo pool_create_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = builder->graphics_queue_index,
  };

  err = vkCreateCommandPool(graph->device, &pool_create_info, NULL,
                            &graph->pool);
  if (err)
  {
    print_vulkan_error(err);
    return false;
  }

  VkCommandBufferAllocateInfo alloc_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = graph->pool,
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = builder->max_frames_in_flight,
  };

  graph->command_buffers = MIUR_ARR(VkCommandBuffer, builder->max_frames_in_flight);
  err = vkAllocateCommandBuffers(graph->device, &alloc_info,
                                 graph->command_buffers);
  if (err)
  {
    print_vulkan_error(err);
    return false;
  }

  return true;
}

void render_graph_destroy(RenderGraph *graph)
{
  for (size_t i = 0; i < graph->passes.size; i++)
  {
    texture_ptr_vec_destroy(&graph->passes.arr[i].color_outputs);
    texture_ptr_vec_destroy(&graph->passes.arr[i].inputs);
  }
  for (size_t i = 0; i < graph->baked_passes.size; i++)
  {
    BakedRenderPass *pass = &graph->baked_passes.arr[i];
    vkDestroyRenderPass(graph->device, pass->vk_pass, NULL);
    for (uint32_t j = 0; j < graph->max_frames_in_flight; j++)
    {
      vkDestroyFramebuffer(graph->device, pass->framebuffers[j],
                           NULL);
    }
    MIUR_FREE(pass->framebuffers);
  }

  for (size_t i = 0; i < graph->textures.size; i++)
  {
    /* Do nothing, for now. */
  }
  render_pass_vec_destroy(&graph->passes);
  baked_pass_vec_destroy(&graph->baked_passes);
  render_pass_map_destroy(&graph->pass_index_map);
  texture_map_destroy(&graph->texture_index_map);
  vkDestroyCommandPool(graph->device, graph->pool, NULL);
}

RenderPass *render_graph_add_pass(RenderGraph *graph, String _str)
{
  RenderPass *pass = render_pass_vec_alloc(&graph->passes);
  texture_ptr_vec_create(&pass->color_outputs);
  texture_ptr_vec_create(&pass->inputs);

  pass->mark = TOPO_MARK_NEW;

  size_t index = pass - graph->passes.arr;

  String str = string_libc_clone(&_str);

  if (render_pass_map_insert(&graph->pass_index_map, &str, &index) == NULL)
  {
    return NULL;
  }

  return pass;
}

bool render_graph_draw(RenderGraph *graph, int frame)
{
  MIUR_LOG_INFO("frame: %d", frame);
  VkResult err;

  vkResetCommandBuffer(graph->command_buffers[frame], 0);

  VkCommandBufferBeginInfo buffer_begin_info = {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
  };

  err = vkBeginCommandBuffer(graph->command_buffers[frame],
                             &buffer_begin_info);
  if (err)
  {
    print_vulkan_error(err);
    return false;
  }

  VkClearValue clear;

  for (size_t i = 0; i < graph->baked_passes.size; i++)
  {
    BakedRenderPass *pass = &graph->baked_passes.arr[i];

    memset(&clear.color, 0, sizeof(VkClearColorValue));
    if (pass->pass->clear_color_callback != NULL)
    {
      pass->pass->clear_color_callback(NULL, &clear.color);
    }

    MIUR_LOG_INFO("%p", (void*) pass->framebuffers[frame]);

    VkRenderPassBeginInfo render_pass_begin_info = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = pass->vk_pass,
      .framebuffer = pass->framebuffers[frame],
      .renderArea = {
        .offset = {0, 0},
        .extent = graph->present_extent,
      },
      .clearValueCount = 1,
      .pClearValues = &clear,
    };

    vkCmdBeginRenderPass(graph->command_buffers[frame],
                         &render_pass_begin_info,
                         VK_SUBPASS_CONTENTS_INLINE);

    if (pass->pass->draw_callback != NULL)
    {
      pass->pass->draw_callback(pass->pass->ud,
                                &graph->command_buffers[frame]);
    }

    vkCmdEndRenderPass(graph->command_buffers[frame]);
  }

  err = vkEndCommandBuffer(graph->command_buffers[frame]);
  if (err)
  {
    print_vulkan_error(err);
    return false;
  }

  return true;
}

bool render_pass_add_color_output(RenderGraph *graph, RenderPass *pass,
                                  RenderGraphTexture *output)
{
  texture_ptr_vec_insert(&pass->color_outputs, output);
  return true;
}

bool render_pass_add_input_texture(RenderGraph *graph, RenderPass *pass,
                                   RenderGraphTexture *input)
{
  texture_ptr_vec_insert(&pass->inputs, input);
  return true;
}

RenderGraphTexture *render_graph_create_texture(RenderGraph *graph,
                                                String _str)
{
  RenderGraphTexture *texture = texture_vec_alloc(&graph->textures);
  size_t index = texture - graph->textures.arr;
  String str = string_libc_clone(&_str);
  texture_map_insert(&graph->texture_index_map, &str, &index);
  return texture;
}

void render_graph_set_present(RenderGraph *graph, RenderGraphTexture *tex)
{
  graph->present_texture = tex;
  tex->format = graph->present_format;
  tex->views = graph->present_image_views;
}

bool render_graph_bake(RenderGraph *graph)
{
  VkResult err;

  baked_pass_vec_create(&graph->baked_passes);

  if (!topological_sort(&graph->baked_passes, &graph->passes))
  {
    MIUR_LOG_ERR("Failed to perform topological sort on render passes");
    return false;
  }

  for (size_t i = 0; i < graph->baked_passes.size; i++)
  {
    BakedRenderPass *baked = &graph->baked_passes.arr[i];
    RenderPass *pass = baked->pass;
    size_t attachment_count = pass->color_outputs.size;
    VkAttachmentDescription *attachments = MIUR_ARR(VkAttachmentDescription,
                                                    attachment_count);

    VkAttachmentReference *attachment_refs = MIUR_ARR(VkAttachmentReference,
                                                      pass->color_outputs.size);


    for (size_t j = 0; j < pass->color_outputs.size; j++)
    {
      attachment_refs[j].attachment = j;
      attachment_refs[j].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

      attachments[j].format = pass->color_outputs.arr[j]->format;
      attachments[j].samples = VK_SAMPLE_COUNT_1_BIT;
      attachments[j].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      attachments[j].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      attachments[j].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      attachments[j].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      attachments[j].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      attachments[j].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    VkSubpassDescription subpass = {
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = pass->color_outputs.size,
      .pColorAttachments = attachment_refs,
    };

    VkSubpassDependency subpass_dependency = {
      .srcSubpass = VK_SUBPASS_EXTERNAL,
      .dstSubpass = 0,
      .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .srcAccessMask = 0,
      .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    VkRenderPassCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = attachment_count,
      .pAttachments = attachments,
      .subpassCount = 1,
      .pSubpasses = &subpass,
      .dependencyCount = 1,
      .pDependencies = &subpass_dependency,
    };

    vkCreateRenderPass(graph->device, &create_info, NULL,
                       &baked->vk_pass);


    baked->framebuffers = MIUR_ARR(VkFramebuffer, graph->max_frames_in_flight);
    for (size_t i = 0; i < graph->max_frames_in_flight; i++)
    {
      VkImageView *views = MIUR_ARR(VkImageView, attachment_count);
      for (size_t j = 0; j < pass->color_outputs.size; j++)
      {
        views[j] = pass->color_outputs.arr[j]->views[i];
      }

      VkFramebufferCreateInfo framebuffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = baked->vk_pass,
        .attachmentCount = attachment_count,
        .pAttachments = views,
        .width = graph->present_extent.width,
        .height = graph->present_extent.height,
        .layers = 1,
      };

      err = vkCreateFramebuffer(graph->device, &framebuffer_create_info,
                                NULL, &baked->framebuffers[i]);
      if (err)
      {
        print_vulkan_error(err);
        return false;
      }
    }

    MIUR_FREE(attachments);
    MIUR_FREE(attachment_refs);
  }
  return true;
}

/* === PRIVATE FUNCTIONS === */

bool topological_sort_rec(BakedPassVec *baked, RenderPassVec *passes,
                          RenderPass *pass)
{
  switch (pass->mark)
  {
  case TOPO_MARK_FOUND:
    return true;
  case TOPO_MARK_TEMP:
    return false;
  case TOPO_MARK_NEW: {
    pass->mark = TOPO_MARK_TEMP;
    for (size_t i = 0; i < pass->color_outputs.size; i++)
    {
      RenderGraphTexture *output = pass->color_outputs.arr[i];
      for (size_t j = 0; j < passes->size; j++)
      {
        RenderPass *temp_pass = &passes->arr[j];
        for (size_t k = 0; k < temp_pass->inputs.size; k++)
        {
          RenderGraphTexture *input = pass->inputs.arr[k];
          if (input == output)
          {
            if (!topological_sort_rec(baked, passes, temp_pass))
            {
              return false;
            }
          }
        }
      }
    }
    pass->mark = TOPO_MARK_FOUND;
    BakedRenderPass *baked_pass = baked_pass_vec_alloc(baked);
    baked_pass->pass = pass;
    return true;
  }
  default:
    MIUR_LOG_ERR("Invalid topological state case: %d", pass->mark);
    return false;
  }
}

bool topological_sort(BakedPassVec *baked, RenderPassVec *passes)
{
  for (size_t i = 0; i < passes->size; i++)
  {
    if (!topological_sort_rec(baked, passes, &passes->arr[i]))
    {
      return false;
    }
  }
  return true;
}
