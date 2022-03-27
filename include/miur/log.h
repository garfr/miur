/* =====================
 * include/miur/log.h
 * 02/23/2022
 * Priority based logging.
 * ====================
 */

#ifndef MIUR_LOG_H
#define MIUR_LOG_H

typedef enum {
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERR,
    LOG_LEVEL_FATAL,
} Log_Level;

#define MIUR_LOG_INFO(msg, ...)                                                \
    _miur_log(LOG_LEVEL_INFO, __LINE__, __FILE__, msg, __VA_ARGS__)
#define MIUR_LOG_WARN(msg, ...)                                                \
    _miur_log(LOG_LEVEL_WARN, __LINE__, __FILE__, msg, __VA_ARGS__)
#define MIUR_LOG_ERR(msg, ...)                                                 \
    _miur_log(LOG_LEVEL_ERR, __LINE__, __FILE__, msg, __VA_ARGS__)
#define MIUR_LOG_FATAL(msg, ...)                                               \
    _miur_log(LOG_LEVEL_FATAL, __LINE__, __FILE__, msg, __VA_ARGS__)

/* === PRIVATE === */

void _miur_log(Log_Level level, int line, const char *file, const char *msg,
               ...);

#endif
