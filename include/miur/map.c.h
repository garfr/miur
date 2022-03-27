/* =====================
 * include/miur/map.c.h
 * 03/19/2022
 * Generic map/set implementation.
 * ====================
 */

/* Macros that must be defined:
 *
 * MAP_KEY_TYPE
 * MAP_VAL_TYPE
 * MAP_FUN_PREFIX
 * MAP_TYPE_PREFIX
 * MAP_HASH_FUN
 * MAP_EQ_FUN
 */

#include <miur/mem.h>
#include <miur/log.h>

/* === UTILS === */

#define CAT(a, b) a##b
#define PASTE(a, b) CAT(a, b)

#define MANGLE_TYPE(name) PASTE(MAP_TYPE_PREFIX, name)

#ifndef MAP_NO_FUNCTIONS
#define MANGLE_FUN(name) PASTE(MAP_FUN_PREFIX, name)
#endif

/* === HEADER === */

#ifdef MAP_HEADER

#ifndef MAP_NO_TYPES
typedef struct MANGLE_TYPE(MapEntry)
{
  MAP_KEY_TYPE key;
  MAP_VAL_TYPE val;
  uint32_t hash;
  struct MANGLE_TYPE(MapEntry) *next;
} MANGLE_TYPE(MapEntry);

typedef struct
{
  MANGLE_TYPE(MapEntry) **buckets;
  size_t nbuckets;
  size_t buckets_filled;
  void *ud;
} MANGLE_TYPE(Map);
#endif

#ifndef MAP_NO_FUNCTIONS
void MANGLE_FUN(create)(MANGLE_TYPE(Map) *map_out);
void MANGLE_FUN(destroy)(MANGLE_TYPE(Map) *map);
void MANGLE_FUN(set_user_data)(MANGLE_TYPE(Map) *map, void *ud);

/* Returns NULL when it already exists in the map. */
MAP_VAL_TYPE *MANGLE_FUN(insert)(MANGLE_TYPE(Map) *map, MAP_KEY_TYPE *key,
                                 MAP_VAL_TYPE *val);

/* Returns NULL when nothing is found. */
MAP_VAL_TYPE *MANGLE_FUN(find)(MANGLE_TYPE(Map) *map, MAP_KEY_TYPE *key);
#endif

#endif

/* === IMPLEMENTATION === */

#ifdef MAP_IMPLEMENTATION

#ifndef MAP_INIT_BUCKETS
#define MAP_INIT_BUCKETS 8
#endif

void
MANGLE_FUN(create)(MANGLE_TYPE(Map) *map_out)
{
  map_out->buckets = MIUR_ARR(MANGLE_TYPE(MapEntry) *, MAP_INIT_BUCKETS);
  map_out->nbuckets = MAP_INIT_BUCKETS;
  map_out->buckets_filled = 0;
}

void
MANGLE_FUN(destroy)(MANGLE_TYPE(Map) *map)
{
  MANGLE_TYPE(MapEntry) *first, *follow;
  for (size_t i = 0; i < map->nbuckets; i++)
  {
    first = map->buckets[i];
    while (first != NULL)
    {
      follow = first;
      first = first->next;

#ifdef MAP_KEY_DESTRUCTOR
      MAP_KEY_DESTRUCTOR(map->ud, &follow->key);
#endif

#ifdef MAP_VAL_DESTRUCTOR
      MAP_VAL_DESTRUCTOR(map->ud, &follow->val);
#endif

      MIUR_FREE(follow);
    }
  }
  MIUR_FREE(map->buckets);
}

void MANGLE_FUN(set_user_data)(MANGLE_TYPE(Map) *map, void *ud)
{
  map->ud = ud;
}

/* Returns NULL when it already exists in the map. */
MAP_VAL_TYPE *MANGLE_FUN(insert)(MANGLE_TYPE(Map) *map, MAP_KEY_TYPE *key,
                                 MAP_VAL_TYPE *val)
{
  uint32_t hash = MAP_HASH_FUN(key);
  size_t idx = hash % map->nbuckets;
  MANGLE_TYPE(MapEntry) *iter = map->buckets[idx];

  if (iter == NULL)
  {
    map->buckets_filled++;
  }

  while (iter != NULL)
  {
    if (MAP_EQ_FUN(key, &iter->key))
    {
      return NULL;
    }
    iter = iter->next;
  }

  iter = MIUR_NEW(MANGLE_TYPE(MapEntry));
  iter->key = *key;
  iter->val = *val;
  iter->hash = hash;
  iter->next = map->buckets[idx];
  map->buckets[idx] = iter;

  return &iter->val;
}

/* Returns NULL when nothing is found. */
MAP_VAL_TYPE *MANGLE_FUN(find)(MANGLE_TYPE(Map) *map, MAP_KEY_TYPE *key)
{
uint32_t hash = MAP_HASH_FUN(key);
  size_t idx = hash % map->nbuckets;
  MANGLE_TYPE(MapEntry) *iter = map->buckets[idx];

  while (iter != NULL)
  {
    if (MAP_EQ_FUN(key, &iter->key))
    {
      return &iter->val;
    }
    iter = iter->next;
  }

  return NULL;
}

#endif

#undef MAP_KEY_TYPE
#undef MAP_VAL_TYPE
#undef MAP_TYPE_PREFIX
#undef MAP_FUN_PREFIX
#undef MAP_EQ_FUNCTION
#undef MAP_HASH_FUNCTION

#ifdef MAP_NO_FUNCTIONS
#undef MAP_NO_FUNCTIONS
#else
#undef MANGLE_FUN
#endif

#ifdef MAP_NO_TYPES
#undef MAP_NO_TYPES
#endif

#undef MANGLE_TYPE

#ifdef MAP_HEADER
#undef MAP_HEADER
#endif

#ifdef MAP_IMPLEMENTATION
#undef MAP_IMPLEMENTATION
#endif

#ifdef MAP_KEY_DESTRUCTOR
#undef MAP_KEY_DESTRUCTOR
#endif

#ifdef MAP_VAL_DESTRUCTOR
#undef MAP_VAL_DESTRUCTOR
#endif

#undef CAT
#undef PASTE
#undef MANGLE_TYPE
