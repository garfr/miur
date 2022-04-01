/* =====================
 * src/thread.c
 * 03/29/2022
 * Cross platform threading.
 * ====================
 */

#include <miur/thread.h>
#include <miur/mem.h>

#ifdef MIUR_PLATFORM_WINDOWS

#include <windows.h>

typedef struct
{
  ThreadStartFunction function;
  void *ud;
} Win32UserData;

/* === PROTOTYPES === */
DWORD WINAPI win32_thread_start(void *ud);

/* === PUBLIC FUNCTIONS === */

bool thread_create(Thread *thread_out, ThreadStartFunction function, void *ud)
{
  Win32UserData *win32_ud = MIUR_NEW(Win32UserData);
  win32_ud->function = function;
  win32_ud->ud = ud;

  *thread_out = _beginthreadex(NULL, 0, win32_thread_start, win32_ud, 0, 
      NULL);
  if (*thread_out == 0)
  {
    return false;
  }

  return true;
}

void thread_join(Thread *thread)
{
  WaitForSingleObject((HANDLE) *thread, INFINITE);
}

void thread_destroy(Thread *thread)
{
  CloseHandle((HANDLE) *thread);
}

void mutex_create(Mutex *mutex_out, MutexBits bits)
{
  (void) bits;
  InitializeCriticalSection(mutex_out);
}

void mutex_destroy(Mutex *mutex)
{
  DeleteCriticalSection(mutex);
}

bool mutex_try_lock(Mutex *mutex)
{
  return TryEnterCriticalSection(mutex);
}

void mutex_lock(Mutex *mutex)
{
  EnterCriticalSection(mutex);
}

void mutex_unlock(Mutex *mutex)
{
  LeaveCriticalSection(mutex);
}

/* === PRIVATE FUNCTIONS === */

DWORD WINAPI win32_thread_start(void *_ud)
{
  Win32UserData *ud = (Win32UserData *) _ud;

  ud->function(ud->ud);

  MIUR_FREE(ud);
  return 0;
}

#endif
