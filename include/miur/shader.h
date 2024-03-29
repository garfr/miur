/* =====================
 * include/miur/shader.h
 * 03/19/2022
 * Shader management.
 * ====================
 */

#ifndef MIUR_SHADER_H
#define MIUR_SHADER_H

#include <vulkan/vulkan.h>
#include <shaderc/shaderc.h>

#include <miur/string.h>
#include <miur/membuf.h>

typedef struct
{
  Membuf code;
  VkShaderModule module;
  VkShaderStageFlagBits stage;
} ShaderModule;

#define MAP_KEY_TYPE String
#define MAP_VAL_TYPE ShaderModule
#define MAP_TYPE_PREFIX Shader
#define MAP_NO_FUNCTIONS
#define MAP_HEADER
#include <miur/map.c.h>

typedef struct
{
  ShaderMap map;
  shaderc_compiler_t compiler;
} ShaderCache;

void shader_cache_create(ShaderCache *cache_out, VkDevice *dev);
void shader_cache_destroy(ShaderCache *cache);
ShaderModule *shader_cache_lookup(ShaderCache *cache, String *str);
bool shader_cache_reload_shader(VkDevice dev, ShaderCache *cache, 
    ShaderModule *module, const char *path);
ShaderModule *shader_cache_load(VkDevice dev, ShaderCache *cache,
                                String *str);
#endif
