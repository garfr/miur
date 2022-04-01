/* =====================
 * src/material.c
 * 03/23/2022
 * Material system.
 * ====================
 */

#include <stdarg.h>

#include <miur/material.h>
#include <miur/json.h>

/* === PROTOTYPES FUNCTIONS === */

static void json_parse_error(JsonStream *stream, JsonTok bad_tok,
                             ParseError *error,
                             const char *fmt, ...);
bool technique_build(VkDevice dev, VkExtent2D present_extent, 
    VkFormat present_format, Technique *tech);
void technique_destroy(void *ud, Technique *tech);
void effect_destroy(void *ud, Effect *tech);
static void mark_materials(MaterialCache *materials, Effect *effect);
static void mark_effects(MaterialCache *materials, EffectCache *effects, Technique *tech);
static void mark_techniques(TechniqueCache *techs, MaterialCache *materials, 
    EffectCache *effects, ShaderModule *mod);

/* === PUBLIC FUNCTIONS === */

#define MAP_KEY_TYPE String
#define MAP_VAL_TYPE Technique
#define MAP_TYPE_PREFIX Technique
#define MAP_FUN_PREFIX technique_map_
#define MAP_HASH_FUN string_hash
#define MAP_EQ_FUN string_eq
#define MAP_HEADER
#define MAP_NO_TYPES
#define MAP_KEY_DESTRUCTOR string_libc_destroy
#define MAP_VAL_DESTRUCTOR technique_destroy
#define MAP_IMPLEMENTATION
#include <miur/map.c.h>

#define MAP_KEY_TYPE String
#define MAP_VAL_TYPE Effect
#define MAP_TYPE_PREFIX Effect
#define MAP_FUN_PREFIX effect_map_
#define MAP_HASH_FUN string_hash
#define MAP_EQ_FUN string_eq
#define MAP_HEADER
#define MAP_NO_TYPES
#define MAP_KEY_DESTRUCTOR string_libc_destroy
#define MAP_VAL_DESTRUCTOR effect_destroy
#define MAP_IMPLEMENTATION
#include <miur/map.c.h>

#define MAP_KEY_TYPE String
#define MAP_VAL_TYPE Material 
#define MAP_TYPE_PREFIX Material
#define MAP_FUN_PREFIX material_map_ 
#define MAP_HASH_FUN string_hash
#define MAP_EQ_FUN string_eq
#define MAP_HEADER
#define MAP_NO_TYPES
#define MAP_KEY_DESTRUCTOR string_libc_destroy
#define MAP_IMPLEMENTATION
#include <miur/map.c.h>

void technique_cache_create(TechniqueCache *cache_out)
{
  cache_out->dev = MIUR_NEW(VkDevice);
  technique_map_create(&cache_out->map);
  technique_map_set_user_data(&cache_out->map, cache_out->dev);
}

void technique_cache_destroy(TechniqueCache *cache)
{
  technique_map_destroy(&cache->map);
  MIUR_FREE(cache->dev);
}

bool technique_cache_load_file(VkDevice device, VkExtent2D present_extent,
                               VkFormat present_format, 
                               TechniqueCache *cache,
                               ShaderCache *shaders, Membuf file,
                               ParseError*error)
{
  *(cache->dev) = device;
  JsonStream stream;
  JsonTok global, technique;
  json_stream_init(&stream, file);
  Technique empty_technique = {0};

  if (!JSON_EXPECT_WITH(&stream, JSON_OBJECT, &global))
  {
    json_parse_error(&stream, global, error,
                         "expected global object specifiying techniques");
    return false;
  }


  JSON_FOR_OBJECT(&stream, global, technique_name_tok)
  {
    String _technique_name = json_get_string(&stream, technique_name_tok);
    String technique_name = string_libc_clone(&_technique_name);
    Technique *tech = technique_map_insert(&cache->map, &technique_name, 
        &empty_technique);
    if (tech == NULL)
    {
      json_parse_error(&stream, technique_name_tok, error, 
          "duplicate technique '%.*s'", (int) technique_name.size, 
          (char *) technique_name.data);
      return false;
    }
    tech->shaders.vert = tech->shaders.frag = NULL;

    JsonTok technique_tok;
    if (!JSON_EXPECT_WITH(&stream, JSON_OBJECT, &technique_tok))
    {
      json_parse_error(&stream, technique_tok, error,
          "techniques should be specified as a JSON object");
      return false;
    }

    JSON_FOR_OBJECT(&stream, technique_tok, field)
    {
      if (json_streq(&stream, field, "vert"))
      {
        JsonTok filename_tok;
        if (!JSON_EXPECT_WITH(&stream, JSON_STRING, &filename_tok))
        {

          json_parse_error(&stream, filename_tok, error,
              "technique field 'vert' must correspond to a string");
          return false;
        }

        String filename = json_get_string(&stream, filename_tok);
        tech->shaders.vert = shader_cache_load(device, shaders, &filename);
        if (tech->shaders.vert == NULL)
        {
          json_parse_error(&stream, filename_tok, error,
              "failed to load vertex shader file: '%.*s'", (int) filename.size,
              (char *) filename.data);
          return false;
        }
      }
      else if (json_streq(&stream, field, "frag"))
      {
        JsonTok filename_tok;
        if (!JSON_EXPECT_WITH(&stream, JSON_STRING, &filename_tok))
        {

          json_parse_error(&stream, filename_tok, error,
              "technique field 'frag' must correspond to a string");
          return false;
        }

        String filename = json_get_string(&stream, filename_tok);
        tech->shaders.frag = shader_cache_load(device, shaders, &filename);
        if (tech->shaders.frag == NULL)
        {
          json_parse_error(&stream, filename_tok, error,
              "failed to load fragment shader file: '%.*s'", (int) filename.size,
              (char *) filename.data);
          return false;
        }
      } else
      {
        String str = json_get_string(&stream, field);
        json_parse_error(&stream, field, error, 
            "unknown technique field: '%.*s'", (int) str.size, str.data);
        return false;
      } 

    }

    if (tech->shaders.frag == NULL || tech->shaders.vert == NULL)
    {
      json_parse_error(&stream, technique_name_tok, error,
          "technique requires both a vertex and fragment shader");
      return false;
    }

    if (!technique_build(device, present_extent, present_format, tech))
    {
      json_parse_error(&stream, technique_name_tok, error,
          "failed to build technique: '%.*s'", (int) technique_name.size,
          technique_name.data);
      return false;
    }
  }

  json_stream_deinit(&stream);
  return true;
}

Technique *technique_cache_lookup(TechniqueCache *cache, String *name)
{
  return technique_map_find(&cache->map, name); 
}

Effect *effect_cache_lookup(EffectCache *cache, String *name)
{
  return effect_map_find(&cache->map, name);
}

void effect_cache_create(EffectCache *cache_out)
{
  effect_map_create(&cache_out->map);
}

void effect_cache_destroy(EffectCache *cache)
{
  effect_map_destroy(&cache->map);
}

bool effect_cache_load_file(VkDevice dev, EffectCache *cache,
                            TechniqueCache *techs, Membuf file,
                            ParseError *error)
{
  JsonStream stream;
  JsonTok global;
  json_stream_init(&stream, file);
  Effect empty_effect = {0};

  if (!JSON_EXPECT_WITH(&stream, JSON_OBJECT, &global))
  {
    json_parse_error(&stream, global, error,
                     "expected global object specifiying effects");
    return false;
  }

  JSON_FOR_OBJECT(&stream, global, effect_name_tok)
  {
    String _effect_name = json_get_string(&stream, effect_name_tok);
    String effect_name = string_libc_clone(&_effect_name);
    Effect *effect = effect_map_insert(&cache->map, &effect_name, &empty_effect);
    if (effect == NULL)
    {
      json_parse_error(&stream, effect_name_tok, error,
          "duplicate effect '%.*s'", (int) effect_name.size, effect_name.data); 
      return false;
    }

    JsonTok effect_tok;
    if (!JSON_EXPECT_WITH(&stream, JSON_OBJECT, &effect_tok))
    {
      json_parse_error(&stream, effect_tok, error,
          "effects should be specified as a JSON object");
      return false;
    }

    JSON_FOR_OBJECT(&stream, effect_tok, field_tok)
    {
      String field = json_get_string(&stream, field_tok);
      if (string_cstr_eq(&field, "forward"))
      {
        JsonTok technique_name_tok;
        if (!JSON_EXPECT_WITH(&stream, JSON_STRING, &technique_name_tok))
        {
          json_parse_error(&stream, technique_name_tok, error,
              "effect field 'forward' must be the name of a technique");
          return false;
        }
        String _technique_name = json_get_string(&stream, technique_name_tok);
        String technique_name = string_libc_clone(&_technique_name);
        effect->techniques.forward = technique_cache_lookup(techs, &technique_name);
        if (effect->techniques.forward == NULL)
        {
          json_parse_error(&stream, field_tok, error,
              "unknown technique name '%.*s'", (int) technique_name.size, 
              technique_name.data);
          return false;
        }
      } else
      {
        json_parse_error(&stream, field_tok, error,
            "unknown technique field '%.*s'", (int) field.size, field.data);
        return false;
      }
    }
  }
  // @TODO: Load effect file.
  return true;
}

void material_cache_create(MaterialCache *cache_out)
{
  material_map_create(&cache_out->map);
}

void material_cache_destroy(MaterialCache *cache)
{
  material_map_destroy(&cache->map);
}

Material *material_cache_add(MaterialCache *cache, Effect *effect, 
    String *material_name)
{
  Material new_mat = {
    .effect = effect,
  };

  Material *mat = material_map_insert(&cache->map,  material_name, &new_mat);
  if (mat == NULL)
  {
    return NULL;
  }

  return mat;
}

Material *material_cache_lookup(MaterialCache *cache, String *name)
{
  return material_map_find(&cache->map, name);
}

void material_cache_rebuild(VkDevice dev, VkExtent2D present_extent,
    VkFormat present_format, MaterialCache *materials, EffectCache *effects, 
    TechniqueCache *techniques, ShaderModule *mod)
{
  mark_techniques(techniques, materials, effects, mod);

  TechniqueMapIter tech_iter = technique_map_iter_create(&techniques->map);

  Technique *tech = technique_map_iter_next(&tech_iter);
  vkDeviceWaitIdle(dev);
  for (; tech != NULL; tech = technique_map_iter_next(&tech_iter))
  {
    if (tech->mark)
    {
      vkDestroyPipeline(dev, tech->pipeline, NULL);
      vkDestroyPipelineLayout(dev, tech->layout, NULL);
      if (!technique_build(dev, present_extent, present_format, tech))
      {
        MIUR_LOG_ERR("failed to build new technique after hot reload");
        return;
      }
    }
  }
}
/* === PRIVATE FUNCTIONS === */

static void mark_materials(MaterialCache *materials, Effect *effect)
{
  MaterialMapIter iter = material_map_iter_create(&materials->map);
  Material *material = material_map_iter_next(&iter);

  for (; material != NULL; material = material_map_iter_next(&iter))
  {
    if (material->effect == effect)
    {
      material->mark = true;
    }
  }
}

static void mark_effects(MaterialCache *materials, EffectCache *effects, Technique *tech)
{
  EffectMapIter iter = effect_map_iter_create(&effects->map);
  Effect *effect = effect_map_iter_next(&iter);

  for (; effect != NULL; effect = effect_map_iter_next(&iter))
  {
    if (effect->techniques.forward == tech)
    {
      effect->mark = true;
      mark_materials(materials, effect);
    }
  }
}

static void mark_techniques(TechniqueCache *techs, MaterialCache *materials, 
    EffectCache *effects, ShaderModule *mod)
{
  TechniqueMapIter iter = technique_map_iter_create(&techs->map);

  Technique *tech = technique_map_iter_next(&iter); 
  for (; tech != NULL; tech = technique_map_iter_next(&iter))
  {
    if (tech->shaders.vert == mod || tech->shaders.frag == mod)
    {
      tech->mark = true;
      mark_effects(materials, effects, tech);
    }
  }
}

bool technique_build(VkDevice dev, VkExtent2D present_extent, 
    VkFormat present_format, Technique *tech)
{
  VkResult err;
  tech->mark = false;

  // @TODO: Add technique descriptor set building. 
  VkPipelineLayoutCreateInfo layout_create_info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
  };

  err = vkCreatePipelineLayout(dev, &layout_create_info, NULL, &tech->layout);
  if (err)
  {
    print_vulkan_error(err);
    return false;
  }
  
  VkVertexInputBindingDescription binding_descriptions[] = {
    {
      .binding = 0,
      .stride = 3 * sizeof(float),
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    },
    {
      .binding = 1,
      .stride = 3 * sizeof(float),
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    },
  };

  VkVertexInputAttributeDescription attribute_descriptions[] = {
    {
      .binding = 0,
      .location = 0,
      .format = VK_FORMAT_R32G32B32_SFLOAT,
      .offset = 0,
    },
    {
      .binding = 1,
      .location = 1,
      .format = VK_FORMAT_R32G32B32_SFLOAT,
      .offset = 0,
    },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount = 2,
    .pVertexBindingDescriptions = binding_descriptions,
    .vertexAttributeDescriptionCount = 2,
    .pVertexAttributeDescriptions = attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewport = {
    .x = 0.0f,
    .y = 0.0f,
    .width = (float) present_extent.width,
    .height = (float) present_extent.height,
    .minDepth = 0.0f,
    .maxDepth = 1.0f,
  };

  VkRect2D scissor = {
    .offset = {0, 0},
    .extent = present_extent,
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .viewportCount = 1,
    .pViewports = &viewport,
    .scissorCount = 1,
    .pScissors = &scissor
  };

  VkPipelineRasterizationStateCreateInfo rasterization_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .depthClampEnable = VK_FALSE,
    .rasterizerDiscardEnable = VK_FALSE,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .lineWidth = 1.0f,
    .cullMode = VK_CULL_MODE_BACK_BIT,
    .frontFace = VK_FRONT_FACE_CLOCKWISE,
    .depthBiasEnable = VK_FALSE,
  };

  VkPipelineMultisampleStateCreateInfo multisample_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .sampleShadingEnable = VK_FALSE,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
  };

  VkDynamicState dynamic_states[] = {
    VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
  };

  VkPipelineDynamicStateCreateInfo dynamic_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = 2,
    .pDynamicStates = dynamic_states,
  };

  VkPipelineColorBlendAttachmentState color_blend_attach = {
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    .blendEnable = false,
  };

  VkPipelineColorBlendStateCreateInfo color_blend_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .logicOpEnable = VK_FALSE,
    .logicOp = VK_LOGIC_OP_COPY,
    .attachmentCount = 1,
    .pAttachments = &color_blend_attach,
  };

  VkPipelineShaderStageCreateInfo shader_stage_state[2] = {
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = tech->shaders.vert->module,
      .pName = "vert",
    },
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = tech->shaders.frag->module,
      .pName = "frag",
    }
  }; 

  VkRenderPass suitable_render_pass;
  VkAttachmentDescription color_attachment = {
    .format = present_format,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  };

  VkAttachmentReference attachment_ref = {
    .attachment = 0,
    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };

  VkSubpassDescription subpass = {
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .colorAttachmentCount = 1,
    .pColorAttachments = &attachment_ref,
  };

  VkRenderPassCreateInfo render_pass_create_info = {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .attachmentCount = 1,
    .pAttachments = &color_attachment,
    .subpassCount = 1,
    .pSubpasses = &subpass,
  };

  err = vkCreateRenderPass(dev, &render_pass_create_info, NULL, 
      &suitable_render_pass);
  if (err)
  {
    print_vulkan_error(err);
    return false;
  }

  VkGraphicsPipelineCreateInfo pipeline_create_info = {
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .stageCount = 2,
    .pStages = shader_stage_state,
    .pVertexInputState = &vertex_input_state,
    .pInputAssemblyState = &input_assembly_state,
    .pViewportState = &viewport_state,
    .pRasterizationState = &rasterization_state,
    .pMultisampleState = &multisample_state,
    .pDynamicState = &dynamic_state,
    .pColorBlendState = &color_blend_state,
    .layout = tech->layout,
    .renderPass = suitable_render_pass,
    .subpass = 0,
  };

  err = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, 
      &pipeline_create_info, NULL,
      &tech->pipeline);
  if (err)
  {
    print_vulkan_error(err);
    return false;
  }

  vkDestroyRenderPass(dev, suitable_render_pass, NULL);

  return true;
}

static void json_parse_error(JsonStream *stream, JsonTok bad_tok,
                             ParseError *error, const char *fmt,
                             ...)
{
  va_list args;
  va_start(args, fmt);

  vsnprintf(error->msg, MAX_PARSE_ERROR_MSG_LENGTH, fmt, args);
  json_get_position_info(stream, bad_tok, &error->line, &error->col);
}

void technique_destroy(void *ud, Technique *tech)
{
  VkDevice *dev = (VkDevice *) ud;
  vkDestroyPipeline(*dev, tech->pipeline, NULL);
  vkDestroyPipelineLayout(*dev, tech->layout, NULL);
  return;
}

void effect_destroy(void *ud, Effect *tech)
{
  return; // @Todo: Add effect destructor.
}
