/* =====================
 * src/shader.c
 * 03/19/2022
 * Shader management.
 * ====================
 */

#include <vulkan/vulkan.h>
#include <string.h>

#include <miur/shader.h>
#include <miur/log.h>
#include <miur/bsl.h>

/* === PROTOTYPES === */

static void shader_destroy(void *ud, ShaderModule *module);
const char *get_filename_ext(const char *filename);

/* === PUBLIC FUNCTIONS === */

#define MAP_KEY_TYPE String
#define MAP_VAL_TYPE ShaderModule
#define MAP_TYPE_PREFIX Shader
#define MAP_FUN_PREFIX shader_map_
#define MAP_HASH_FUN string_hash
#define MAP_EQ_FUN string_eq
#define MAP_HEADER
#define MAP_NO_TYPES
#define MAP_KEY_DESTRUCTOR string_libc_destroy
#define MAP_VAL_DESTRUCTOR shader_destroy
#define MAP_IMPLEMENTATION
#include <miur/map.c.h>

void shader_cache_create(ShaderCache *cache_out, VkDevice *dev)
{
  shader_map_create(&cache_out->map);
  shader_map_set_user_data(&cache_out->map, dev);
  cache_out->compiler = shaderc_compiler_initialize();
}

void shader_cache_destroy(ShaderCache *cache)
{
  shader_map_destroy(&cache->map);
}

ShaderModule *shader_cache_load(VkDevice dev, ShaderCache *cache,
                                String *str)
{
  ShaderModule *mod = shader_map_find(&cache->map, str);
  if (mod == NULL)
  {
    ShaderModule module;
    Membuf file_contents;
    BSLCompileResult compile_result;
    shaderc_shader_kind kind;
    VkResult err;
    uint8_t *zero_terminated = MIUR_ARR(uint8_t, str->size + 1);
    bool result;
    shaderc_compilation_result_t glsl_result;
    memcpy(zero_terminated, str->data, str->size);
    zero_terminated[str->size] = '\0';
    result = membuf_load_file(&file_contents, zero_terminated);
    if (!result) {
      MIUR_LOG_ERR("Failed to open shader file '%s'", zero_terminated);
      MIUR_FREE(zero_terminated);
      return NULL;
    }
    
    const char *extension = strrchr(zero_terminated, '.');
    if (strcmp(extension, ".vert") == 0)
    {
      kind = shaderc_vertex_shader;
    } else if (strcmp(extension, ".frag") == 0)
    {
      kind = shaderc_fragment_shader;
    } else
    {
      MIUR_LOG_ERR("Unknown extension: '%s'", extension);
      MIUR_FREE(zero_terminated);
      return NULL;
    }

    glsl_result = shaderc_compile_into_spv(
      cache->compiler,
      (char *) file_contents.data,
      file_contents.size,
      kind, zero_terminated, "main", 
      NULL
    );

    membuf_destroy(&file_contents);
    if (shaderc_result_get_compilation_status(glsl_result) != 0)
    {
      const char *msg = shaderc_result_get_error_message(glsl_result);
      MIUR_LOG_ERR("Failed to compile shader file '%s'\n%s",
                     zero_terminated, msg);
      MIUR_FREE(zero_terminated);
      return NULL;
    }

    module.code.size = shaderc_result_get_length(glsl_result);
    char *new_data = MIUR_ARR(uint8_t, module.code.size);
    const char *data = shaderc_result_get_bytes(glsl_result);
    memcpy(new_data, data, module.code.size);
    module.code.data = new_data;

    VkShaderModuleCreateInfo shader_create_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = module.code.size,
      .pCode = (uint32_t *) module.code.data,
    };

    err = vkCreateShaderModule(dev, &shader_create_info, NULL, &module.module);
    if (err)
    {
      membuf_destroy(&file_contents);
      MIUR_FREE(zero_terminated);
      MIUR_LOG_ERR("Failed to create shader module from shader file '%s': %d",
                   zero_terminated, err);
      return NULL;
    }

    String new_str = {
      .data = zero_terminated,
      .size = str->size,
    };
    return shader_map_insert(&cache->map, &new_str, &module);
  }

  return mod;
}

ShaderModule *shader_cache_lookup(ShaderCache *cache, String *str)
{
  return shader_map_find(&cache->map, str);
}

bool shader_cache_reload_shader(VkDevice dev, ShaderCache *cache, 
    ShaderModule *module, const char *path)
{
  shaderc_shader_kind kind;
  shaderc_compilation_result_t glsl_result;
  Membuf source;
  /*
  membuf_destroy(&module->code);
  */
  VkResult err;

  if (!membuf_load_file(&source, path))
  {
    MIUR_LOG_ERR("Cannot open shader file: %s", path);
    return false;
  }

  const char *extension = strrchr(path, '.');
  if (strcmp(extension, ".vert") == 0)
  {
    kind = shaderc_vertex_shader;
  } else if (strcmp(extension, ".frag") == 0)
  {
    kind = shaderc_fragment_shader;
  } else
  {
    MIUR_LOG_ERR("Unknown extension: '%s'", extension);
    return 0;
  }

  glsl_result = shaderc_compile_into_spv(
    cache->compiler,
    (char *) source.data,
    source.size,
    kind, path, "main", 
    NULL
  );

  membuf_destroy(&source);
  if (shaderc_result_get_compilation_status(glsl_result) != 0)
  {
    const char *msg = shaderc_result_get_error_message(glsl_result);
    MIUR_LOG_ERR("Failed to compile shader file '%s'\n%s",
                    path, msg);
    return NULL;
  }

  module->code.size = shaderc_result_get_length(glsl_result);
  char *new_data = MIUR_ARR(uint8_t, module->code.size);
  const char *data = shaderc_result_get_bytes(glsl_result);
  memcpy(new_data, data, module->code.size);
  module->code.data = new_data;

  vkDestroyShaderModule(dev, module->module, NULL);
  VkShaderModuleCreateInfo shader_create_info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = module->code.size,
    .pCode = (uint32_t *) module->code.data,
  };

  err = vkCreateShaderModule(dev, &shader_create_info, NULL, &module->module);
  if (err)
  {
    print_vulkan_error(err);
    return false;
  }

  return true;
}


/* === PRIVATE FUNCTIONS === */

static void shader_destroy(void *ud, ShaderModule *module)
{
  VkDevice *dev = (VkDevice *) ud;
  vkDestroyShaderModule(*dev, module->module, NULL);
}

const char *
get_filename_ext(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return "";
    return dot + 1;
}
