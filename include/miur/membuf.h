/* =====================
 * include/miur/membuf.c
 * 03/05/2022
 * Memory buffers.
 * ====================
 */

#ifndef MIUR_MEMBUF_H
#define MIUR_MEMBUF_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct
{
  const uint8_t *data;
  size_t size;
} Membuf;

bool membuf_load_file(Membuf *membuf, const char *filename);
bool membuf_write_file(Membuf membuf, const char *filename);

void membuf_destroy(Membuf *membuf);

#endif
