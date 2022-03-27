/* =====================
 * src/string.c
 * 03/19/2022
 * String handling
 * ====================
 */

#include <string.h>

#include <miur/string.h>
#include <miur/mem.h>

bool string_eq(String *str1, String *str2)
{
  return str1->size == str2->size &&
    strncmp(str1->data, str2->data, str1->size) == 0;
}

uint32_t string_hash(String *str)
{
  return str->size;
}

void string_print(String *str)
{
  printf("%.*s", (int) str->size, (char *) str->data);
}

void string_libc_destroy(void *ud, String *str)
{
  MIUR_FREE(str->data);
  str->data = NULL;
  str->size = 0;
}

String string_libc_clone(String *str)
{
  uint8_t *buf = MIUR_ARR(uint8_t, str->size);
  memcpy(buf, str->data, str->size);
  String new = {
    .data = buf,
    .size = str->size,
  };
  return new;
}

String string_from_cstr(const char *cstr)
{
  String str = {
    .data = (uint8_t *) cstr,
    .size = strlen(cstr),
  };
  return str;
}
