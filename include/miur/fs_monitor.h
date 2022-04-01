/* =====================
 * include/miur/fs_monitor.h
 * 03/29/022
 * Monitors file systems.
 * ====================
 */

#ifndef MIUR_FS_MONITOR_H
#define MIUR_FS_MONITOR_H

#define FS_MONITOR_MAX_EVENTS 512
#define FS_MONITOR_MAX_PATH 512

#include <stddef.h>
#include <stdbool.h>

#include <miur/thread.h>

typedef enum
{
  FS_MONITOR_EVENT_CREATE,
  FS_MONITOR_EVENT_DELETE,
  FS_MONITOR_EVENT_MODIFY,
  FS_MONITOR_EVENT_MOVE,
} FsMonitorEventType;

typedef struct
{
  FsMonitorEventType t;
  char path[FS_MONITOR_MAX_PATH];
} FsMonitorEvent;

typedef struct FsMonitor FsMonitor;

FsMonitor *fs_monitor_create(void);
void fs_monitor_destroy(FsMonitor *mon);
bool fs_monitor_add_dir(FsMonitor *mon, const char *path);

FsMonitorEvent *fs_monitor_get_events(FsMonitor *mon, size_t *size);
void fs_monitor_release_events(FsMonitor *mon);

#endif
