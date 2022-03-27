/* =====================
 * include/miur/render_graph.h
 * 03/24/2022
 * Render graph for organizing GPU sync.
 * ====================
 */

#ifndef MIUR_RENDER_GRAPH_H
#define MIUR_RENDER_GRAPH_H

#include <vulkan/vulkan.h>

#include <miur/string.h>

typedef struct
{
  float x, y;
  VkFormat format;
  VkImage *images;
  VkImageView *views;
} RenderGraphTexture;

#define MAP_KEY_TYPE String
#define MAP_VAL_TYPE size_t
#define MAP_TYPE_PREFIX RenderGraphTexture
#define MAP_NO_FUNCTIONS
#define MAP_HEADER
#include <miur/map.c.h>

#define VECTOR_TYPE RenderGraphTexture
#define VECTOR_HEADER
#define VECTOR_NO_FUNCTIONS
#define VECTOR_FUN_PREFIX texture_vec_
#define VECTOR_TYPE_PREFIX RenderGraphTexture
#include <miur/vector.c.h>

#define VECTOR_TYPE RenderGraphTexture *
#define VECTOR_HEADER
#define VECTOR_NO_FUNCTIONS
#define VECTOR_TYPE_PREFIX RenderGraphTexturePtr
#define VECTOR_FUN_PREFIX texture_ptr_vec_
#include <miur/vector.c.h>

typedef struct
{
  VkDeviceSize size;
  VkBufferUsageFlags usage;
  String name;
} RenderGraphBuffer;

typedef void (*RenderGraphDrawCallback)(void *ud, VkCommandBuffer *buffer);
typedef void (*RenderGraphClearColorCallback)(void *ud, VkClearColorValue *color);

typedef enum {
  TOPO_MARK_NEW = 0,
  TOPO_MARK_FOUND,
  TOPO_MARK_TEMP,
} TopoMark;

typedef struct
{
  RenderGraphTexturePtrVec color_outputs;
  RenderGraphTexturePtrVec inputs;
  RenderGraphDrawCallback draw_callback;
  RenderGraphClearColorCallback clear_color_callback;
  TopoMark mark; /* Used for topological sort marking. */
  void *ud;
} RenderPass;

#define MAP_KEY_TYPE String
#define MAP_VAL_TYPE size_t
#define MAP_TYPE_PREFIX RenderPass
#define MAP_NO_FUNCTIONS
#define MAP_HEADER
#include <miur/map.c.h>

#define VECTOR_TYPE RenderPass
#define VECTOR_HEADER
#define VECTOR_NO_FUNCTIONS
#define VECTOR_FUN_PREFIX render_pass_vec_
#define VECTOR_TYPE_PREFIX RenderPass
#include <miur/vector.c.h>

typedef struct
{
  RenderPass *pass;
  VkRenderPass vk_pass;
  VkFramebuffer *framebuffers;
} BakedRenderPass;

#define VECTOR_TYPE BakedRenderPass
#define VECTOR_HEADER
#define VECTOR_NO_FUNCTIONS
#define VECTOR_FUN_PREFIX baked_pass_vec_
#define VECTOR_TYPE_PREFIX BakedPass
#include <miur/vector.c.h>

typedef struct
{
  VkFormat present_format;
  VkDevice device;
  size_t max_frames_in_flight;
  uint32_t graphics_queue_index;
  VkExtent2D present_extent;
  uint32_t present_image_count;
  VkImageView *present_image_views;
} RenderGraphBuilder;

typedef struct
{
  RenderPassVec passes;
  RenderPassMap pass_index_map;
  RenderGraphTextureVec textures;
  RenderGraphTextureMap texture_index_map;
  RenderGraphTexture *present_texture;
  VkFormat present_format;
  BakedPassVec baked_passes;
  VkDevice device;
  VkCommandPool pool;
  VkCommandBuffer *command_buffers;
  VkExtent2D present_extent;
  size_t max_frames_in_flight;
  uint32_t present_image_count;
  VkImageView *present_image_views;
} RenderGraph;

bool render_pass_add_color_output(RenderGraph *graph, RenderPass *pass,
                                  RenderGraphTexture *output);
bool render_pass_add_input_texture(RenderGraph *graph, RenderPass *pass,
                                   RenderGraphTexture *input);

bool render_graph_create(RenderGraph *graph, RenderGraphBuilder *builder);
void render_graph_destroy(RenderGraph *graph);
RenderPass *render_graph_add_pass(RenderGraph *graph, String str);
bool render_graph_draw(RenderGraph *graph, int frame);
RenderGraphTexture *render_graph_create_texture(RenderGraph *graph, String str);
void render_graph_set_present(RenderGraph *graph, RenderGraphTexture *tex);
bool render_graph_bake(RenderGraph *graph);

#endif
