/* =====================
 * src/membuf.c
 * 03/05/2022
 * Memory buffers.
 * ====================
 */

#include <stdio.h>

#include <miur/log.h>
#include <miur/mem.h>
#include <miur/membuf.h>

bool membuf_load_file(Membuf *membuf, const char *filename)
{
  FILE *file = fopen(filename, "rb");
  if (file == NULL)
  {
    return false;
  }

  fseek(file, 0, SEEK_END);
  membuf->size = ftell(file);
  if (membuf->size == 0)
  {
    membuf->data = NULL;
    return true;
  }

  fseek(file, 0, SEEK_SET);

  uint8_t *data = MIUR_ARR(uint8_t, membuf->size);
  if (data == NULL)
  {
    fclose(file);
    return false;
  }

  fread(data, 1, membuf->size, file);

  membuf->data = data;
  fclose(file);

  return true;
}

bool membuf_write_file(Membuf membuf, const char *filename)
{
  FILE *file = fopen(filename, "wb");
  fwrite(membuf.data, 1, membuf.size, file);
  fclose(file);
  return true;
}

void membuf_destroy(Membuf *membuf)
{
  MIUR_FREE((uint8_t *)membuf->data);
}
