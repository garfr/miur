/* =====================
 * include/miur/string.h
 * 03/19/2022
 * String handling
 * ====================
 */

#ifndef MIUR_STRING_H
#define MIUR_STRING_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct
{
  const uint8_t *data;
  size_t size;
} String;

bool string_eq(String *str1, String *str2);
uint32_t string_hash(String *str);
void string_print(String *str);
void string_libc_destroy(void *ud, String *str);
String string_libc_clone(String *str);
String string_from_cstr(const char *str);

#endif
