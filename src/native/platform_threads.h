#ifndef X2_PLATFORM_THREADS_H
#define X2_PLATFORM_THREADS_H

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <errno.h>
#include <stdint.h>
#include <time.h>

typedef HANDLE pthread_t;
typedef SRWLOCK pthread_mutex_t;
typedef CONDITION_VARIABLE pthread_cond_t;
typedef struct pthread_attr_t {
  SIZE_T stack_size;
} pthread_attr_t;

#define PTHREAD_MUTEX_INITIALIZER SRWLOCK_INIT
#define PTHREAD_COND_INITIALIZER CONDITION_VARIABLE_INIT

static inline int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr) {
  (void)attr;
  InitializeSRWLock(mutex);
  return 0;
}
static inline int pthread_mutex_destroy(pthread_mutex_t *mutex) {
  (void)mutex;
  return 0;
}
static inline int pthread_mutex_lock(pthread_mutex_t *mutex) {
  AcquireSRWLockExclusive(mutex);
  return 0;
}
static inline int pthread_mutex_trylock(pthread_mutex_t *mutex) {
  return TryAcquireSRWLockExclusive(mutex) ? 0 : EBUSY;
}
static inline int pthread_mutex_unlock(pthread_mutex_t *mutex) {
  ReleaseSRWLockExclusive(mutex);
  return 0;
}

static inline int pthread_cond_init(pthread_cond_t *condition,
                                    const void *attr) {
  (void)attr;
  InitializeConditionVariable(condition);
  return 0;
}
static inline int pthread_cond_destroy(pthread_cond_t *condition) {
  (void)condition;
  return 0;
}
static inline int pthread_cond_wait(pthread_cond_t *condition,
                                    pthread_mutex_t *mutex) {
  return SleepConditionVariableSRW(condition, mutex, INFINITE, 0) ? 0 : EINVAL;
}
static inline int pthread_cond_signal(pthread_cond_t *condition) {
  WakeConditionVariable(condition);
  return 0;
}
static inline int pthread_cond_broadcast(pthread_cond_t *condition) {
  WakeAllConditionVariable(condition);
  return 0;
}

static inline uint64_t x2_timespec_milliseconds(const struct timespec *value) {
  return (uint64_t)value->tv_sec * 1000u + (uint64_t)value->tv_nsec / 1000000u;
}

static inline uint64_t x2_realtime_milliseconds(void) {
  FILETIME file_time;
  ULARGE_INTEGER ticks;
  GetSystemTimeAsFileTime(&file_time);
  ticks.LowPart = file_time.dwLowDateTime;
  ticks.HighPart = file_time.dwHighDateTime;
  return (ticks.QuadPart - UINT64_C(116444736000000000)) / 10000u;
}

static inline int pthread_cond_timedwait(pthread_cond_t *condition,
                                         pthread_mutex_t *mutex,
                                         const struct timespec *absolute) {
  const uint64_t target = x2_timespec_milliseconds(absolute);
  const uint64_t now = x2_realtime_milliseconds();
  const DWORD timeout =
      target <= now
          ? 0
          : (target - now > UINT32_MAX ? INFINITE : (DWORD)(target - now));
  if (SleepConditionVariableSRW(condition, mutex, timeout, 0))
    return 0;
  return GetLastError() == ERROR_TIMEOUT ? ETIMEDOUT : EINVAL;
}

typedef struct x2_thread_start_context {
  void *(*routine)(void *);
  void *argument;
} x2_thread_start_context;

static inline DWORD WINAPI x2_thread_start(void *opaque) {
  x2_thread_start_context *context = (x2_thread_start_context *)opaque;
  void *(*routine)(void *) = context->routine;
  void *argument = context->argument;
  HeapFree(GetProcessHeap(), 0, context);
  (void)routine(argument);
  return 0;
}

static inline int pthread_attr_init(pthread_attr_t *attr) {
  attr->stack_size = 0;
  return 0;
}
static inline int pthread_attr_destroy(pthread_attr_t *attr) {
  (void)attr;
  return 0;
}
static inline int pthread_attr_setstacksize(pthread_attr_t *attr,
                                            size_t stack_size) {
  attr->stack_size = stack_size;
  return 0;
}
static inline int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                 void *(*routine)(void *), void *argument) {
  x2_thread_start_context *context = (x2_thread_start_context *)HeapAlloc(
      GetProcessHeap(), 0, sizeof(*context));
  if (!context)
    return EAGAIN;
  context->routine = routine;
  context->argument = argument;
  *thread = CreateThread(NULL, attr ? attr->stack_size : 0, x2_thread_start,
                         context, 0, NULL);
  if (!*thread) {
    HeapFree(GetProcessHeap(), 0, context);
    return EAGAIN;
  }
  return 0;
}
static inline int pthread_detach(pthread_t thread) {
  return CloseHandle(thread) ? 0 : EINVAL;
}
static inline int pthread_join(pthread_t thread, void **result) {
  if (WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0)
    return EINVAL;
  if (result)
    *result = NULL;
  return CloseHandle(thread) ? 0 : EINVAL;
}
static inline pthread_t pthread_self(void) { return GetCurrentThread(); }
static inline int pthread_equal(pthread_t left, pthread_t right) {
  return GetThreadId(left) == GetThreadId(right);
}
static inline void pthread_exit(void *result) {
  (void)result;
  ExitThread(0);
}

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#ifndef CLOCK_PROCESS_CPUTIME_ID
#define CLOCK_PROCESS_CPUTIME_ID 2
#endif

static inline int x2_clock_gettime(int clock_id, struct timespec *result) {
  if (clock_id == CLOCK_REALTIME) {
    const uint64_t milliseconds = x2_realtime_milliseconds();
    result->tv_sec = (time_t)(milliseconds / 1000u);
    result->tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    return 0;
  }
  if (clock_id == CLOCK_PROCESS_CPUTIME_ID) {
    FILETIME creation, exit_time, kernel, user;
    ULARGE_INTEGER ticks;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit_time, &kernel,
                         &user))
      return -1;
    ticks.LowPart = user.dwLowDateTime;
    ticks.HighPart = user.dwHighDateTime;
    result->tv_sec = (time_t)(ticks.QuadPart / 10000000u);
    result->tv_nsec = (long)(ticks.QuadPart % 10000000u) * 100;
    return 0;
  }
  {
    LARGE_INTEGER counter, frequency;
    if (!QueryPerformanceCounter(&counter) ||
        !QueryPerformanceFrequency(&frequency))
      return -1;
    result->tv_sec = (time_t)(counter.QuadPart / frequency.QuadPart);
    result->tv_nsec = (long)((counter.QuadPart % frequency.QuadPart) *
                             1000000000LL / frequency.QuadPart);
  }
  return 0;
}

static inline int x2_nanosleep(const struct timespec *requested,
                               struct timespec *remaining) {
  const uint64_t milliseconds =
      (uint64_t)requested->tv_sec * 1000u +
      ((uint64_t)requested->tv_nsec + 999999u) / 1000000u;
  Sleep(milliseconds > UINT32_MAX ? UINT32_MAX : (DWORD)milliseconds);
  if (remaining) {
    remaining->tv_sec = 0;
    remaining->tv_nsec = 0;
  }
  return 0;
}

static inline int x2_sched_yield(void) {
  SwitchToThread();
  return 0;
}

#define clock_gettime x2_clock_gettime
#define nanosleep x2_nanosleep
#define sched_yield x2_sched_yield

#else

#include <pthread.h>
#include <sched.h>
#include <time.h>

#endif

#endif
