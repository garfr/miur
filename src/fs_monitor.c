/* =====================
 * src/fs_monitor.c
 * 03/29/022
 * Monitors file systems.
 * ====================
 */

#include <string.h>

#include <miur/fs_monitor.h>
#include <miur/log.h>
#include <miur/mem.h>

#define FS_MONITOR_MAX_WATCHES 100
#define FS_MONITOR_MAX_RAW_EVENTS 100

typedef struct
{
  HANDLE dir_handle;
  OVERLAPPED overlapped;
  uint8_t buffer[64512];
  DWORD notify_filter;
  char dirpath[FS_MONITOR_MAX_PATH];
} FsMonitorWatch;

typedef struct
{
  DWORD action;
  char filepath[FS_MONITOR_MAX_PATH];
  bool skip;
  FsMonitorWatch *watch;
} RawEvent;

struct FsMonitor
{
  FsMonitorEvent events[FS_MONITOR_MAX_EVENTS];
  size_t cur_event;
  Thread thread;
  Mutex mutex, event_mutex;
  size_t num_watches;
  FsMonitorWatch watches[FS_MONITOR_MAX_WATCHES];
  bool should_quit;
  RawEvent raw_events[FS_MONITOR_MAX_RAW_EVENTS];
  size_t cur_raw_event;
};

/* === PROTOTYPES === */

void win32_monitor_function(void *ud);
static bool refresh_watch(FsMonitorWatch *watch);
static void process_raw_events(FsMonitor *mon);

/* === PUBLIC FUNCTIONS === */

FsMonitor *fs_monitor_create(void)
{
  FsMonitor *mon = MIUR_NEW(FsMonitor);
  memset(mon->events, 0, sizeof(FsMonitorEvent) * FS_MONITOR_MAX_EVENTS);
  mutex_create(&mon->mutex, MUTEX_PLAIN);
  mutex_create(&mon->event_mutex, MUTEX_PLAIN);
  mon->cur_event = 0;
  mon->num_watches = 0;
  mon->should_quit = false;
  mon->cur_raw_event = 0;
  if (!thread_create(&mon->thread, win32_monitor_function, mon))
  {
    return NULL;
  }

  return mon;
}

void fs_monitor_destroy(FsMonitor *mon)
{
  mon->should_quit = true;
  if (((HANDLE) mon->thread) != INVALID_HANDLE_VALUE)
  {
    thread_join(&mon->thread);
    thread_destroy(&mon->thread);
  }
  MIUR_FREE(mon);
  return;
}

bool fs_monitor_add_dir(FsMonitor *mon, const char *path)
{
  mutex_lock(&mon->mutex);
  FsMonitorWatch *watch = &mon->watches[mon->num_watches++];

  watch->dir_handle = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | 
      FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
  if (watch->dir_handle == INVALID_HANDLE_VALUE)
  {
    MIUR_LOG_ERR("Failed to add directory");
    mutex_unlock(&mon->mutex);
    return false;
  }

  watch->notify_filter = FILE_NOTIFY_CHANGE_CREATION | 
    FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | 
    FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE;
  watch->overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
  if (watch->overlapped.hEvent == INVALID_HANDLE_VALUE)
  {
    MIUR_LOG_ERR("Failed to create event");
    return false;
  }

  memcpy(watch->dirpath, path, strlen(path) + 1);

  if (!refresh_watch(watch)) {
    return false;
  }
  mutex_unlock(&mon->mutex);
  return true;
}

FsMonitorEvent *fs_monitor_get_events(FsMonitor *mon, size_t *size)
{
  mutex_lock(&mon->event_mutex);

  *size = mon->cur_event;
  return mon->events;
}

void fs_monitor_release_events(FsMonitor *mon)
{
  mon->cur_event = 0;
  mutex_unlock(&mon->event_mutex);
}

/* === PRIVATE FUNCTIONS === */

void win32_monitor_function(void *ud)
{
  FsMonitor *mon = (FsMonitor *) ud;
  HANDLE wait_handles[FS_MONITOR_MAX_WATCHES];
  
  while (!mon->should_quit)
  {
    if (!mutex_try_lock(&mon->mutex))
    {
      Sleep(10);
      continue;
    }
    
    if (mon->num_watches == 0)
    {
      Sleep(10);
      mutex_unlock(&mon->mutex);
      continue;
    }

    for (size_t i = 0; i < mon->num_watches; i++)
    {
      FsMonitorWatch *watch = &mon->watches[i];
      wait_handles[i] = watch->overlapped.hEvent;
    }

    DWORD result = WaitForMultipleObjects(mon->num_watches, wait_handles, FALSE, 10);
    if (result != WAIT_TIMEOUT)
    {
      FsMonitorWatch *watch = &mon->watches[result - WAIT_OBJECT_0];

      DWORD bytes;
      if (GetOverlappedResult(watch->dir_handle, &watch->overlapped, &bytes, FALSE))
      {
        char filepath[FS_MONITOR_MAX_PATH];
        PFILE_NOTIFY_INFORMATION notify;
        size_t offset = 0;

        if (bytes == 0)
        {
          refresh_watch(watch);
          mutex_unlock(&mon->mutex);
          continue;
        }

        do {
          notify = (PFILE_NOTIFY_INFORMATION) &watch->buffer[offset];
          int count = WideCharToMultiByte(CP_UTF8, 0, notify->FileName, 
              notify->FileNameLength / sizeof(WCHAR),
              filepath, FS_MONITOR_MAX_PATH - 1, NULL, NULL);
          filepath[count] = TEXT('\0');

          RawEvent *ev = &mon->raw_events[mon->cur_raw_event++];
          ev->action = notify->Action;
          ev->skip = false;
          ev->watch = watch;
          memcpy(ev->filepath, filepath, sizeof(ev->filepath));

          offset = notify->NextEntryOffset;
        } while (notify->NextEntryOffset > 0);
      }

      if (!mon->should_quit)
      {
        refresh_watch(watch);
      }
    }

    if (mon->cur_raw_event > 0)
    {
      process_raw_events(mon);
    }

    mutex_unlock(&mon->mutex);
  }
}

static void process_raw_events(FsMonitor *mon)
{
  mutex_lock(&mon->event_mutex);

  for (size_t i = 0; i < mon->cur_raw_event; i++)
  { 
    RawEvent *raw = &mon->raw_events[i];
    if (raw->skip)
    {
      continue;
    }
    if (raw->action == FILE_ACTION_MODIFIED || raw->action == FILE_ACTION_ADDED)
    {
      for (size_t j = i; j < mon->cur_raw_event; j++)
      {
        RawEvent *tmp_raw = &mon->raw_events[j];
        if (tmp_raw->action == FILE_ACTION_MODIFIED && 
            strcmp(raw->filepath, tmp_raw->filepath) == 0)
        {
          tmp_raw->skip = true;
        }
      }
    }

    if (raw->action == FILE_ACTION_MODIFIED)
    {
      FsMonitorEvent *ev = &mon->events[mon->cur_event++];
      ev->t = FS_MONITOR_EVENT_MODIFY;
      size_t dirlen = strlen(raw->watch->dirpath);
      size_t filelen = strlen(raw->filepath);
      if (dirlen + filelen + 1 >= FS_MONITOR_MAX_PATH)
      {
        MIUR_LOG_ERR("Path longer than max");
        mon->cur_raw_event = 0;
        mutex_unlock(&mon->event_mutex);
        return;
      }

      memcpy(ev->path, raw->watch->dirpath, dirlen);
      ev->path[dirlen] = '/';
      memcpy(ev->path + dirlen + 1, raw->filepath, filelen + 1);
    }
  }

  mon->cur_raw_event = 0;
  mutex_unlock(&mon->event_mutex);
}

static bool refresh_watch(FsMonitorWatch *watch)
{
  return ReadDirectoryChangesW(watch->dir_handle, watch->buffer, sizeof(watch->buffer),
      TRUE, watch->notify_filter, NULL, &watch->overlapped, NULL) != 0;
}
