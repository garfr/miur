/* =====================
 * include/miur/bsl.h
 * 03/08/2022
 * The Beans shading language.
 * ====================
 */

#ifndef MIUR_BSL_H
#define MIUR_BSL_H

#define BSL_MAX_ERROR_LENGTH 512

#include <miur/membuf.h>

typedef struct
{
  int line, column;
  char error[BSL_MAX_ERROR_LENGTH];
  Membuf spirv;
} BSLCompileResult;

typedef struct
{
  int nothing; // @Todo: add flags to the flag struct so i can pass
               // flags to flag features.
} BSLCompileFlags;

bool bsl_compile(BSLCompileResult *result, Membuf data,
                 BSLCompileFlags *flags);

#endif
