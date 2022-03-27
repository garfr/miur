/* =====================
 * include/miur/vector.c.h
 * 03/25/2022
 * Generic vector template header
 * ====================
 */

#ifndef VECTOR_TYPE
#error Must define a type inside the vector
#endif

#ifndef VECTOR_FUN_PREFIX
#error Must define a function prefix for the vector type
#endif

#ifndef VECTOR_TYPE_PREFIX
#error Must define a type prefix for the vector type
#endif

#define CAT(a, b) a##b
#define PASTE(a, b) CAT(a, b)

#define MANGLE_TYPE(name) PASTE(VECTOR_TYPE_PREFIX, name)
#define MANGLE_FUN(name) PASTE(VECTOR_FUN_PREFIX, name)

#ifdef VECTOR_HEADER
#ifndef VECTOR_NO_TYPES
typedef struct
{
  VECTOR_TYPE *arr;
  size_t size;
  size_t alloc;
} MANGLE_TYPE(Vec);
#endif

#ifndef VECTOR_NO_FUNCTIONS
bool MANGLE_FUN(create)(MANGLE_TYPE(Vec) *out_vec);
bool MANGLE_FUN(create_with)(MANGLE_TYPE(Vec) *out_vec, size_t size);
void MANGLE_FUN(destroy)(MANGLE_TYPE(Vec) *vec);
VECTOR_TYPE *MANGLE_FUN(insert)(MANGLE_TYPE(Vec) *vec, VECTOR_TYPE val);
VECTOR_TYPE *MANGLE_FUN(alloc)(MANGLE_TYPE(Vec) *vec);
#endif

#endif

#ifdef VECTOR_IMPLEMENTATION

#ifndef VECTOR_INIT_SIZE
#define VECTOR_INIT_SIZE 16
#endif

#ifndef VECTOR_GROWTH_RATIO
#define VECTOR_GROWTH_RATIO 1.5
#endif

bool MANGLE_FUN(create)(MANGLE_TYPE(Vec) *out_vec)
{
  out_vec->size = 0;
  out_vec->arr = MIUR_ARR(VECTOR_TYPE, VECTOR_INIT_SIZE);
  if (out_vec->arr == NULL)
  {
    return false;
  }
  out_vec->alloc = VECTOR_INIT_SIZE;
  return true;
}

bool MANGLE_FUN(create_with)(MANGLE_TYPE(Vec) *out_vec, size_t size)
{
  out_vec->alloc = size;
  out_vec->arr = MIUR_ARR(VECTOR_TYPE, out_vec->alloc);
  if (out_vec->arr == NULL)
  {
    return false;
  }
  out_vec->size = 0;
  return true;
}

void MANGLE_FUN(destroy)(MANGLE_TYPE(Vec) *vec)
{
  MIUR_FREE(vec->arr);
  vec->size = 0;
  vec->alloc = 0;
  vec->arr = NULL;
}

VECTOR_TYPE *MANGLE_FUN(insert)(MANGLE_TYPE(Vec) *vec, VECTOR_TYPE val)
{
  if (vec->size >= vec->alloc)
  {
    vec->alloc = (size_t) (((double) vec->alloc) *
                           (VECTOR_GROWTH_RATIO));
    vec->arr = MIUR_REALLOC(VECTOR_TYPE, vec->arr, vec->alloc);
    if (vec->arr == NULL)
    {
      return NULL;
    }
  }

  vec->arr[vec->size] = val;
  return &vec->arr[vec->size++];
}

VECTOR_TYPE *MANGLE_FUN(alloc)(MANGLE_TYPE(Vec) *vec)
{
  if (vec->size >= vec->alloc)
  {
    vec->alloc = (size_t) (((double) vec->alloc) *
                           (VECTOR_GROWTH_RATIO));
    vec->arr = MIUR_REALLOC(VECTOR_TYPE, vec->arr, vec->alloc);
    if (vec->arr == NULL)
    {
      return NULL;
    }
  }

  return &vec->arr[vec->size++];
}

#endif

#undef MANGLE_TYPE
#undef MANGLE_FUN
#undef VECTOR_TYPE
#undef VECTOR_NAME

#undef VECTOR_FUN_PREFIX
#undef VECTOR_TYPE_PREFIX

#ifdef VECTOR_HEADER
#undef VECTOR_HEADER
#endif

#ifdef VECTOR_NO_TYPES
#undef VECTOR_NO_TYPES
#endif

#ifdef VECTOR_NO_FUNCTIONS
#undef VECTOR_NO_FUNCTIONS
#endif

#undef CAT
#undef PASTE
