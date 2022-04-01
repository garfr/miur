/* =====================
 * src/shader.c
 * 03/19/2022
 * Shader management.
 * ====================
 */

#include <vulkan/vulkan.h>

#include <miur/shader.h>
#include <miur/log.h>
#include <miur/bsl.h>

/* === PROTOTYPES === */

static void shader_destroy(void *ud, ShaderModule *module);

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
    VkResult err;
    uint8_t *zero_terminated = MIUR_ARR(uint8_t, str->size + 1);
    bool result;
    memcpy(zero_terminated, str->data, str->size);
    zero_terminated[str->size] = '\0';
    result = membuf_load_file(&file_contents, zero_terminated);
    if (!result) {
      MIUR_LOG_ERR("Failed to open shader file '%s'", zero_terminated);
      MIUR_FREE(zero_terminated);
      return NULL;
    }

    result = bsl_compile(&compile_result, file_contents, NULL);
    membuf_destroy(&file_contents);
    if (!result)
    {
      MIUR_LOG_ERR("Failed to compile shader file '%s'\n%d%d:%s",
                     zero_terminated, compile_result.line,
                     compile_result.column, compile_result.error);
      MIUR_FREE(zero_terminated);
      return NULL;
    }

    module.code = compile_result.spirv;

    VkShaderModuleCreateInfo shader_create_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = module.code.size,
      .pCode = (uint32_t *) module.code.data,
    };

    err = vkCreateShaderModule(dev, &shader_create_info, NULL, &module.module);
    if (err)
    {
      membuf_destroy(&file_contents);
      membuf_destroy(&compile_result.spirv);
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

  BSLCompileResult compile_result;
  if (!bsl_compile(&compile_result, source, NULL))
  {
    MIUR_LOG_ERR("Failed to compile shader file '%s'\n%d%d:%s",
                  path, compile_result.line,
                  compile_result.column, compile_result.error);
    return false;
  }

  module->code = compile_result.spirv;
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
