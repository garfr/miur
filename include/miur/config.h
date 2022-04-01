/* =====================
 * include/miur/config.h
 * 03/29/2022
 * General configuration.
 * ====================
 */

#ifndef MIUR_CONFIG_H
#define MIUR_CONFIG_H

#ifdef _WIN32
#define MIUR_PLATFORM_WINDOWS
#else
#error Unkown platform
#endif

#endif
