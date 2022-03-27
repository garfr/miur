/* =====================
 * include/miur/mem.h
 * 03/05/2022
 * Host memory allocation helpers.
 * ====================
 */

#ifndef MIUR_MEM_H
#define MIUR_MEM_H

#include <stdlib.h>

#define MIUR_NEW(type) ((type *) calloc(1, sizeof(type)))
#define MIUR_FREE(ptr) (free((void*) ptr))
#define MIUR_ARR(type, size) ((type *) calloc(size, sizeof(type)))
#define MIUR_REALLOC(type, ptr, size) ((type *) realloc(ptr, sizeof(type) * size))

#endif
