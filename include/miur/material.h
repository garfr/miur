/* =====================
 * include/miur/material.h
 * 03/19/2022
 * Material system.
 * ====================
 */

#ifndef MIUR_MATERIAL_H
#define MIUR_MATERIAL_H

#include <miur/shader.h>
#include <miur/utils.h>
#include <miur/string.h>

#define PASS_COUNT 1

#define PER_PASS_DATA(_type, _name) struct { _type forward; } _name

#define DESCRIPTOR_FREQUENCY_COUNT 4

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
  bool mark;
} Technique;

typedef struct
{
  PER_PASS_DATA(Technique *, techniques);
  bool mark;
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
  VkDevice *dev;
} TechniqueCache;

void technique_cache_create(TechniqueCache *cache_out);
bool technique_cache_load_file(VkDevice dev, VkExtent2D present_extent,
                               VkFormat present_format,
                               TechniqueCache *cache, ShaderCache *shaders, 
                               Membuf file, ParseError *error);
Technique *technique_cache_lookup(TechniqueCache *cache, String *name);

void technique_cache_destroy(TechniqueCache *cache);

#define MAP_KEY_TYPE String
#define MAP_VAL_TYPE Effect 
#define MAP_TYPE_PREFIX Effect
#define MAP_NO_FUNCTIONS
#define MAP_HEADER
#include <miur/map.c.h>

typedef struct
{
  EffectMap map;
} EffectCache;

void effect_cache_create(EffectCache *cache_out);
void effect_cache_destroy(EffectCache *cache);
bool effect_cache_load_file(VkDevice dev, EffectCache *cache,
                            TechniqueCache *techs, Membuf file,
                            ParseError *error);
Effect *effect_cache_lookup(EffectCache *cache, String *name);

typedef struct
{
  bool mark;
  Effect *effect;
} Material;

#define MAP_KEY_TYPE String
#define MAP_VAL_TYPE Material 
#define MAP_TYPE_PREFIX Material 
#define MAP_NO_FUNCTIONS
#define MAP_HEADER
#include <miur/map.c.h>

typedef struct
{
  MaterialMap map;
} MaterialCache;

void material_cache_create(MaterialCache *cache_out);
void material_cache_destroy(MaterialCache *cache);
Material *material_cache_add(MaterialCache *cache,
                             Effect *effect, String *material_name);
Material *material_cache_lookup(MaterialCache *cache, String *name);

void material_cache_rebuild(VkDevice dev, VkExtent2D present_extent,
    VkFormat present_format, MaterialCache *materials, EffectCache *effect, 
    TechniqueCache *techniques, ShaderModule *mod);

#endif

