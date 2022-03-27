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

static void technique_json_error(JsonStream *stream, JsonTok bad_tok,
                                 TechniqueLoadError *error,
                                 const char *fmt, ...);
void technique_destroy(void *ud, Technique *tech);

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

void technique_cache_create(TechniqueCache *cache_out)
{
  technique_map_create(&cache_out->map);
}

void technique_cache_destroy(TechniqueCache *cache)
{
  technique_map_destroy(&cache->map);
}

bool technique_cache_load_technique_file(TechniqueCache *cache,
                                         ShaderCache *shaders, Membuf file,
                                         TechniqueLoadError *error)
{
  JsonStream stream;
  JsonTok global, technique;
  json_stream_init(&stream, file);

  if (!JSON_EXPECT_WITH(&stream, JSON_OBJECT, &global))
  {
    technique_json_error(&stream, global, error,
                         "expected global object specifiying techniques");
    return false;
  }
  json_stream_deinit(&stream);
  return true;
}

/* === PRIVATE FUNCTIONS === */

static void technique_json_error(JsonStream *stream, JsonTok bad_tok,
                                 TechniqueLoadError *error, const char *fmt,
                                 ...)
{
  va_list args;
  va_start(args, fmt);

  vsnprintf(error->msg, TECHNIQUE_ERROR_MSG_LENGTH, fmt, args);
  json_get_position_info(stream, bad_tok, &error->line, &error->col);
}

void technique_destroy(void *ud, Technique *tech)
{
  return;
}
