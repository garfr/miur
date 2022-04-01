/* =====================
 * include/miur/utils.h
 * 03/28/2022
 * General utilities.
 * ====================
 */

#ifndef MIUR_UTILS_H
#define MIUR_UTILS_H

#define MAX_PARSE_ERROR_MSG_LENGTH 512

typedef struct
{
  int line, col;
  char msg[MAX_PARSE_ERROR_MSG_LENGTH];
} ParseError;

#endif
