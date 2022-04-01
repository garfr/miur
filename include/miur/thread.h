/* =====================
 * include/miur/thread.h
 * 03/29/2022
 * Cross platform threading.
 * ====================
 */

#ifndef MIUR_THREAD_H
#define MIUR_THREAD_H

#include <stdbool.h>
#include <stdint.h>

#include <miur/config.h>

#ifdef MIUR_PLATFORM_WINDOWS

#include <windows.h>

typedef uintptr_t Thread;

typedef CRITICAL_SECTION Mutex;

#else
#error Threads only support windows.
#endif


typedef void (*ThreadStartFunction)(void *ud);

bool thread_create(Thread *thread_out, ThreadStartFunction function, void *ud);
void thread_join(Thread *thread);
void thread_destroy(Thread *thread);

typedef enum
{
  MUTEX_PLAIN     = 1 << 0,
  MUTEX_TIMED     = 1 << 1,
  MUTEX_RECURSIVE = 1 << 2,
} MutexBits;

void mutex_create(Mutex *mutex_out, MutexBits bits);
void mutex_destroy(Mutex *mutex);
bool mutex_try_lock(Mutex *mutex);
void mutex_lock(Mutex *mutex);
void mutex_unlock(Mutex *mutex);

#endif
