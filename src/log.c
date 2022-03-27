/* =====================
 * src/log.c
 * 2/24/2022
 * Priority based logging.
 * ====================
 */

#include <stdarg.h>
#include <stdio.h>

#include <miur/log.h>

const char *level_to_str[] = {
    [LOG_LEVEL_INFO] = "INFO ",
    [LOG_LEVEL_WARN] = "WARN ",
    [LOG_LEVEL_ERR] = "ERR  ",
    [LOG_LEVEL_FATAL] = "FATAL",
};

void _miur_log(Log_Level level, int line, const char *file, const char *msg,
               ...) {
  va_list args;
  va_start(args, msg);

  printf("%s %s:%d: ", level_to_str[level], file, line);
  vprintf(msg, args);
  printf("\n");
}
