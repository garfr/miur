/* =====================
 * include/miur/material.h
 * 03/19/2022
 * Material system.
 * ====================
 */

#ifndef MIUR_MATERIAL_H
#define MIUR_MATERIAL_H

#include <miur/shader.h>
#include <miur/string.h>

#define TECHNIQUE_ERROR_MSG_LENGTH 512

#define PASS_COUNT 1

#define PER_PASS_DATA(_type, _name) struct { _type forward; } name

#define PER_DESCRIPTOR_FREQUENCY_DATA(_type, _name) struct {                   \
    _type frame; _type material; _type draw; } _name

typedef struct
{
  PER_DESCRIPTOR_FREQUENCY_DATA(VkDescriptorSetLayout, set_layouts);

  struct
  {
    ShaderModule *vert;
    ShaderModule *frag;
  } shaders;

  VkPipeline pipeline;
  VkPipelineLayout layout;
} Technique;

typedef struct
{
  PER_PASS_DATA(Technique *, pass_shaders);
} Effect;

#define MAP_KEY_TYPE String
#define MAP_VAL_TYPE Technique
#define MAP_TYPE_PREFIX Technique
#define MAP_NO_FUNCTIONS
#define MAP_HEADER
#include <miur/map.c.h>

typedef struct
{
  TechniqueMap map;
} TechniqueCache;

typedef struct
{
  int line, col;
  char msg[TECHNIQUE_ERROR_MSG_LENGTH];
} TechniqueLoadError;

void technique_cache_create(TechniqueCache *cache_out);
bool technique_cache_load_technique_file(TechniqueCache *cache,
                                         ShaderCache *shaders, Membuf file,
                                         TechniqueLoadError *error);
void technique_cache_destroy(TechniqueCache *cache);

typedef struct
{
  Effect *effect;
} Material;
#endif

