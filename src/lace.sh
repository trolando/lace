#! /bin/bash

nparams=$1
tasksize=$2

# Copyright notice:
echo "/* 
 * Copyright 2013-2016 Formal Methods and Tools, University of Twente
 * Copyright 2016-2018 Tom van Dijk, Johannes Kepler University Linz
 * Copyright 2019-2026 Tom van Dijk, Formal Methods and Tools, University of Twente
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */"

echo '
#pragma once
#ifndef __LACE_H__
#define __LACE_H__

// Lace version
#define LACE_VERSION_MAJOR 2
#define LACE_VERSION_MINOR 1
#define LACE_VERSION_PATCH 0

#if defined(_MSC_VER) && !defined(__clang__)
    #define LACE_MSVC 1
#else
    #define LACE_MSVC 0
#endif

// Platform configuration
#include <lace_config.h>

// Standard includes
#include <assert.h> // for static_assert
#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>

#if LACE_MSVC
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <intrin.h>
#else
    #include <pthread.h>
    #include <unistd.h>
#endif

#if defined(__APPLE__)
  #include <time.h>
  #include <Availability.h>
  #include <TargetConditionals.h>
  #include <mach/mach_time.h>
#endif

#ifndef __cplusplus
    #include <stdatomic.h>
#else
    // Even though we are not really intending to support C++...
    // Compatibility with C11
    #include <atomic>
    #define _Atomic(T) std::atomic<T>
    using std::memory_order_relaxed;
    using std::memory_order_acquire;
    using std::memory_order_release;
    using std::memory_order_seq_cst;
#endif

// Portable macros

#if LACE_MSVC
    #define LACE_UNUSED
    #define LACE_NOINLINE __declspec(noinline)
    #define LACE_NORETURN __declspec(noreturn)
    #define LACE_ALIGN(N) __declspec(align(N))
    #define LACE_LIKELY(x)   (x)
    #define LACE_UNLIKELY(x) (x)

#elif defined(__GNUC__) || defined(__clang__)
    #define LACE_UNUSED __attribute__((unused))
    #define LACE_NOINLINE __attribute__((noinline))
    #if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
        #define LACE_NORETURN _Noreturn
        #define LACE_ALIGN(N) _Alignas(N)
    #else
        #define LACE_NORETURN __attribute__((noreturn))
        #define LACE_ALIGN(N) __attribute__((aligned(N)))
    #endif
    #define LACE_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define LACE_UNLIKELY(x) __builtin_expect(!!(x), 0)

#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    #define LACE_UNUSED
    #define LACE_NOINLINE
    #define LACE_NORETURN _Noreturn
    #define LACE_ALIGN(N) _Alignas(N)
    #define LACE_LIKELY(x)   (x)
    #define LACE_UNLIKELY(x) (x)

#else
    #define LACE_UNUSED
    #define LACE_NOINLINE
    #define LACE_NORETURN
    #define LACE_ALIGN(N)
    #define LACE_LIKELY(x)   (x)
    #define LACE_UNLIKELY(x) (x)
#endif

#if LACE_MSVC
    #define LACE_TLS __declspec(thread)
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    #define LACE_TLS _Thread_local
#elif defined(__GNUC__) || defined(__clang__)
    #define LACE_TLS __thread
#else
    #error "No thread-local storage qualifier available"
#endif

#if LACE_MSVC
    #include <malloc.h>
    #define LACE_ALLOCA(sz) _alloca(sz)
#else
    #if defined(__has_include)
        #if __has_include(<alloca.h>)
            #include <alloca.h>
        #endif
    #else
        #include <alloca.h>
    #endif
    #define LACE_ALLOCA(sz) alloca(sz)
#endif

#if LACE_MSVC
    #include <limits.h>

    typedef HANDLE lace_sem_t;

    static inline int lace_sem_init(lace_sem_t* sem, unsigned value)
    {
        *sem = CreateSemaphoreA(NULL, (LONG)value, LONG_MAX, NULL);
        return (*sem == NULL) ? -1 : 0;
    }

    static inline int lace_sem_destroy(lace_sem_t* sem)
    {
        int ok = CloseHandle(*sem) ? 0 : -1;
        *sem = NULL;
        return ok;
    }

    static inline int lace_sem_post(lace_sem_t* sem)
    {
        return ReleaseSemaphore(*sem, 1, NULL) ? 0 : -1;
    }

    static inline int lace_sem_wait(lace_sem_t* sem)
    {
        DWORD r = WaitForSingleObject(*sem, INFINITE);
        return (r == WAIT_OBJECT_0) ? 0 : -1;
    }

    static inline int lace_sem_trywait(lace_sem_t* sem)
    {
        DWORD r = WaitForSingleObject(*sem, 0);
        if (r == WAIT_OBJECT_0) return 0;
        if (r == WAIT_TIMEOUT) { errno = EAGAIN; return -1; }
        return -1;
    }

#elif defined(__APPLE__)
    #include <dispatch/dispatch.h>

    typedef dispatch_semaphore_t lace_sem_t;

    static inline int lace_sem_init(lace_sem_t* s, unsigned value)
    {
        *s = dispatch_semaphore_create((long)value);
        return (*s == NULL) ? -1 : 0;
    }

    static inline int lace_sem_wait(lace_sem_t* s)
    {
        dispatch_semaphore_wait(*s, DISPATCH_TIME_FOREVER);
        return 0;
    }

    static inline int lace_sem_trywait(lace_sem_t* s)
    {
        long r = dispatch_semaphore_wait(*s, DISPATCH_TIME_NOW);
        if (r == 0) return 0;
        errno = EAGAIN;
        return -1;
    }

    static inline int lace_sem_post(lace_sem_t* s)
    {
        dispatch_semaphore_signal(*s);
        return 0;
    }

    static inline int lace_sem_destroy(lace_sem_t* s)
    {
        /* See note: usually fine to leak until process exit for runtime globals. */
        *s = NULL;
        return 0;
    }

#else
    #include <semaphore.h>

    typedef sem_t lace_sem_t;

    static inline int lace_sem_init(lace_sem_t* sem, unsigned value) { return sem_init(sem, 0, value); }
    static inline int lace_sem_wait(lace_sem_t* sem) { return sem_wait(sem); }
    static inline int lace_sem_trywait(lace_sem_t* sem) { return sem_trywait(sem); }
    static inline int lace_sem_post(lace_sem_t* sem) { return sem_post(sem); }
    static inline int lace_sem_destroy(lace_sem_t* sem) { return sem_destroy(sem); }
#endif

#if LACE_MSVC

    typedef CRITICAL_SECTION lace_mutex_t;
    typedef CONDITION_VARIABLE lace_cond_t;

    static inline void lace_mutex_init(lace_mutex_t* m) { InitializeCriticalSection(m); }
    static inline void lace_mutex_destroy(lace_mutex_t* m) { DeleteCriticalSection(m); }
    static inline void lace_mutex_lock(lace_mutex_t* m) { EnterCriticalSection(m); }
    static inline void lace_mutex_unlock(lace_mutex_t* m) { LeaveCriticalSection(m); }

    static inline void lace_cond_init(lace_cond_t* c) { InitializeConditionVariable(c); }
    static inline void lace_cond_destroy(lace_cond_t* c) { (void)c; } // no-op on Windows
    static inline void lace_cond_signal(lace_cond_t* c) { WakeConditionVariable(c); }
    static inline void lace_cond_broadcast(lace_cond_t* c) { WakeAllConditionVariable(c); }
    static inline void lace_cond_wait(lace_cond_t* c, lace_mutex_t* m) { SleepConditionVariableCS(c, m, INFINITE); }

#else

    typedef pthread_mutex_t lace_mutex_t;
    typedef pthread_cond_t lace_cond_t;

    static inline void lace_mutex_init(lace_mutex_t* m) { pthread_mutex_init(m, NULL); }
    static inline void lace_mutex_destroy(lace_mutex_t* m) { pthread_mutex_destroy(m); }
    static inline void lace_mutex_lock(lace_mutex_t* m) { pthread_mutex_lock(m); }
    static inline void lace_mutex_unlock(lace_mutex_t* m) { pthread_mutex_unlock(m); }

    static inline void lace_cond_init(lace_cond_t* c) { pthread_cond_init(c, NULL); }
    static inline void lace_cond_destroy(lace_cond_t* c) { pthread_cond_destroy(c); }
    static inline void lace_cond_signal(lace_cond_t* c) { pthread_cond_signal(c); }
    static inline void lace_cond_broadcast(lace_cond_t* c) { pthread_cond_broadcast(c); }
    static inline void lace_cond_wait(lace_cond_t* c, lace_mutex_t* m) { pthread_cond_wait(c, m); }

#endif

// Architecture configuration

// We add padding to some datastructures in order to avoid false sharing.
// We just overapproximate the size of cache lines. On some modern machines,
// cache lines are 128 bytes, so we pick that.
// If needed, this can be overridden with -DLACE_PADDING_TARGET=256 for example
// if targetting architectures that have even larger cache line sizes.
#ifndef LACE_PADDING_TARGET
#define LACE_PADDING_TARGET 128
#endif

/* The size is in bytes. Note that includes the common fields, so that leaves a little less space
   for the task and parameters. Typically tasksize is 64 for lace.h and 128 for lace14.h. If the
   size of a pointer is 32/64 bits (4/8 bytes) then this leaves 56/48 bytes for parameters of the
   task and the return value. */
#ifndef LACE_TASKSIZE
#define LACE_TASKSIZE ('$tasksize')
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// Forward declarations
typedef struct _lace_worker lace_worker;
typedef struct _lace_task lace_task;

/**************************************
 * Lifecycle functions
 * - lace_set_verbosity
 * - lace_start
 * - lace_suspend
 * - lace_resume
 * - lace_stop
 **************************************/

/**
 * Set verbosity level (0 = no startup messages, 1 = startup messages)
 * Default level: 0
 */
void lace_set_verbosity(int level);

/**
 * Start Lace with <n_workers> workers and a task deque size of <dqsize> per worker.
 * If <n_workers> is set to 0, automatically detects available cores.
 * If <dqsize> is set to 0, uses a reasonable default value.
 * If <stacksize> is set to 0, uses the minimum of 16M and the stack size of the calling thread.
 */
void lace_start(unsigned int n_workers, size_t dqsize, size_t stacksize);

/**
 * Suspend all workers. Do not call this from inside Lace threads.
 */
void lace_suspend(void);

/**
 * Resume all workers. Do not call this from inside Lace threads.
 */
void lace_resume(void);

/**
 * Stop Lace. Do not call this from inside Lace threads.
 */
void lace_stop(void);

/**
 * Check if Lace is running. Returns 1 if it does, or 0 otherwise.
 */
int lace_is_running(void);

/**************************************
 * Worker context
 * - lace_worker_count
 * - lace_is_worker
 * - lace_get_worker
 * - lace_worker_id
 * - lace_rng
 **************************************/

/**
 * Retrieve the number of Lace workers.
 */
unsigned int lace_worker_count(void);

/**
 * Retrieve whether we are running in a Lace worker. Returns 1 if this is the case, 0 otherwise.
 */
static inline int lace_is_worker(void) LACE_UNUSED;

/**
 * Retrieve the current worker data.
 */
static inline lace_worker* lace_get_worker(void) LACE_UNUSED;

/**
 * Get the current worker id. Returns -1 if not inside a Lace thread.
 */
static inline int lace_worker_id(void) LACE_UNUSED;

/**
 * Thread-local pseudo-random number generator for Lace workers.
 */
static inline uint64_t lace_rng(lace_worker *lace_worker) LACE_UNUSED;

/**************************************
 * lace_task operations
 * - lace_barrier
 * - lace_drop
 * - lace_is_stolen_task
 * - lace_is_completed_task
 * - lace_steal_random
 * - lace_check_yield
 * - lace_make_all_shared
 * - lace_get_head
 **************************************/

/**
 * Enter the Lace barrier. This is a collective operation.
 * All workers must enter it before the method returns for all workers.
 * Only run this from inside a Lace task.
 */
void lace_barrier(void);

/**
 * Instead of SYNCing on the next task, drop the task (unless stolen already)
 */
void lace_drop(lace_worker *lace_worker);

/**
 * Returns 1 if the given task is stolen, 0 otherwise.
 */
static inline int lace_is_stolen_task(lace_task* t) LACE_UNUSED;

/**
 * Returns 1 if the given task is completed, 0 otherwise.
 */
static inline int lace_is_completed_task(lace_task* t) LACE_UNUSED;

/**
 * Try to steal and execute a random task from a random worker.
 * Only use this from inside a Lace task.
 */
void lace_steal_random(lace_worker*);

/**
 * Check if current tasks must be interrupted, and if so, interrupt.
 */
static inline void lace_check_yield(lace_worker*) LACE_UNUSED;

/**
 * Make all tasks of the current worker shared.
 */
static inline void lace_make_all_shared(void) LACE_UNUSED;

/**
 * Retrieve the current head of the deque of the worker.
 */
static inline lace_task *lace_get_head(void) LACE_UNUSED;

/**************************************
 * Statistics
 * - lace_count_report_file
 * - lace_count_reset
 * - lace_count_report
 **************************************/

/**
 * Reset internal stats counters.
 */
void lace_count_reset(void);

/**
 * Report Lace stats to the given file.
 */
void lace_count_report_file(FILE *file);

/**
 * Report Lace stats to stdout.
 */
static inline LACE_UNUSED void lace_count_report(void)
{
    lace_count_report_file(stdout);
}

/**************************************
 * Miscellaneous
 * - lace_sleep_us
 **************************************/

#if defined(_WIN32)
    // not inline, because we do not want to pull in windows.h here
    // also Windows sleep has a ms resolution, so it is not very practical anyway...
    void lace_sleep_us(int64_t microseconds);
#else
    #include <time.h>
    static inline void lace_sleep_us(int64_t microseconds) {
        if (microseconds <= 0) return;
        struct timespec ts;
        ts.tv_sec = microseconds / 1000000;
        ts.tv_nsec = (microseconds % 1000000) * 1000;
        nanosleep(&ts, NULL);
    }
#endif

/**************************************
 * Internals
 **************************************/

#ifndef LACE_COUNT_EVENTS
#define LACE_COUNT_EVENTS (LACE_PIE_TIMES || LACE_COUNT_TASKS || LACE_COUNT_STEALS || LACE_COUNT_SPLITS)
#endif

typedef enum {
#ifdef LACE_COUNT_TASKS
    CTR_tasks,       /* Number of tasks spawned */
#endif
#ifdef LACE_COUNT_STEALS
    CTR_steal_tries, /* Number of steal attempts */
    CTR_leap_tries,  /* Number of leap attempts */
    CTR_steals,      /* Number of succesful steals */
    CTR_leaps,       /* Number of succesful leaps */
    CTR_steal_busy,  /* Number of steal busies */
    CTR_leap_busy,   /* Number of leap busies */
#endif
#ifdef LACE_COUNT_SPLITS
    CTR_split_grow,  /* Number of split right */
    CTR_split_shrink,/* Number of split left */
    CTR_split_req,   /* Number of split requests */
#endif
    CTR_fast_sync,   /* Number of fast syncs */
    CTR_slow_sync,   /* Number of slow syncs */
#ifdef LACE_PIE_TIMES
    CTR_init,        /* Timer for initialization */
    CTR_close,       /* Timer for shutdown */
    CTR_wapp,        /* Timer for application code (steal) */
    CTR_lapp,        /* Timer for application code (leap) */
    CTR_wsteal,      /* Timer for steal code (steal) */
    CTR_lsteal,      /* Timer for steal code (leap) */
    CTR_wstealsucc,  /* Timer for succesful steal code (steal) */
    CTR_lstealsucc,  /* Timer for succesful steal code (leap) */
    CTR_wsignal,     /* Timer for signal after work (steal) */
    CTR_lsignal,     /* Timer for signal after work (leap) */
    CTR_backoff,     /* Timer for backoff */
#endif
    CTR_MAX
} CTR_index;

typedef struct _lace_worker_public lace_worker_public;

#define TASK_COMMON_FIELDS(type)                   \
    void (*f)(lace_worker *, struct type *);        \
    _Atomic(struct _lace_worker_public*) thief;

typedef struct _lace_task {
    TASK_COMMON_FIELDS(_lace_task)
    char d[LACE_TASKSIZE-sizeof(void*)-sizeof(struct _lace_worker_public*)];
} lace_task;

static_assert(LACE_PADDING_TARGET % 32 == 0, "LACE_PADDING_TARGET must be a multiple of 32");
static_assert(sizeof(lace_task) == '$tasksize', "A Lace task should be '$tasksize' bytes.");

typedef union {
    struct {
        _Atomic(uint32_t) tail;
        _Atomic(uint32_t) split;
    } ts;
    _Atomic(uint64_t) v;
} TailSplit;

typedef union {
    struct {
        uint32_t tail;
        uint32_t split;
    } ts;
    uint64_t v;
} TailSplitNA;

static_assert(sizeof(TailSplit) == 8, "TailSplit size should be 8 bytes");
static_assert(sizeof(TailSplitNA) == 8, "TailSplit size should be 8 bytes");

#if LACE_MSVC
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

typedef struct _lace_worker_public {
    lace_task *dq;
    TailSplit ts;
    uint8_t allstolen;

    LACE_ALIGN(LACE_PADDING_TARGET) uint8_t movesplit;
} lace_worker_public;

#if LACE_MSVC
#pragma warning(pop)
#endif

typedef struct { uint64_t s0, s1; } lace_rng_state;

typedef struct _lace_worker {
    lace_task *head;                 // my head
    lace_task *split;                // same as dq+ts.ts.split
    lace_task *end;                  // dq+dq_size
    lace_task *dq;                   // my queue
    lace_worker_public *_public;     // pointer to public lace_worker_public struct
    lace_rng_state rng;              // my random seed (for lace_rng)
    uint16_t worker;                 // what is my worker id?
    uint8_t allstolen;               // my allstolen

    uint64_t time;
#if LACE_COUNT_EVENTS
    uint64_t ctr[CTR_MAX];      // counters
    int level;
#endif
} lace_worker;

extern LACE_TLS lace_worker *lace_thread_worker;

static inline lace_worker* lace_get_worker(void)
{
    return lace_thread_worker;
}

/**
 * Retrieve whether we are running in a Lace worker. Returns 1 if this is the case, 0 otherwise.
 */
static inline int lace_is_worker(void)
{
    return lace_get_worker() != NULL ? 1 : 0;
}

/**
 * Retrieve the current head of the deque of the worker.
 */
static inline lace_task *lace_get_head(void)
{
    return lace_get_worker()->head;
}

/**
 * Helper function to call from outside Lace threads.
 */
void lace_run_task(lace_task *task);

/**
 * Helper function to call from outside Lace threads.
 */
void lace_run_task_exclusive(lace_task *task);

/**
 * Helper function to start a new task execution (task frame) on a given task.
 * This helper function is used by the _NEWFRAME methods for the NEWFRAME() macro
 * Only when the task is done, do workers continue with the previous task frame.
 */
void lace_run_newframe(lace_task *task);

/**
 * Helper function to make all run a given task together.
 * This helper function is used by the _TOGETHER methods for the TOGETHER() macro
 * They all start the task in a lace_barrier and complete it with a lace barrier.
 * Meaning they all start together, and all end together.
 */
void lace_run_together(lace_task *task);

/**
 * Get the current worker id, or -1 if not inside a Lace thread.
 */
static inline int lace_worker_id(void)
{
    return lace_get_worker() == NULL ? -1 : lace_get_worker()->worker;
}

/**
 * 1 if the given task is stolen, 0 otherwise.
 */
static inline int lace_is_stolen_task(lace_task* t)
{
    return ((size_t)(lace_worker_public*)t->thief > 1) ? 1 : 0;
}

/**
 * 1 if the given task is completed, 0 otherwise.
 */
static inline int lace_is_completed_task(lace_task* t)
{
    return ((size_t)(lace_worker_public*)t->thief == 2) ? 1 : 0;
}

/**
 * Retrieves a pointer to the result of the given task.
 */
#define lace_task_result(t) (&t->d[0])

static inline uint64_t lace_rotl64(uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

/**
 * Compute a random number, thread-local (so scalable)
 */
static inline uint64_t lace_rng(lace_worker* w)
{
    // Xoroshiro128**
    uint64_t s0 = w->rng.s0;
    uint64_t s1 = w->rng.s1;

    // Scrambled output (good low bits)
    uint64_t result = lace_rotl64(s0 * 5ULL, 7) * 9ULL;

    s1 ^= s0;
    w->rng.s0 = lace_rotl64(s0, 24) ^ s1 ^ (s1 << 16);
    w->rng.s1 = lace_rotl64(s1, 37);

    return result;
}

static inline uint64_t lace_macos_now_ns(void)
{
#if defined(__APPLE__)
    #if defined(CLOCK_UPTIME_RAW)
        return (uint64_t)clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    #else
        static mach_timebase_info_data_t tb;
        if (tb.denom == 0) mach_timebase_info(&tb);
        uint64_t t = (uint64_t)mach_absolute_time();
        return (t * (uint64_t)tb.numer) / (uint64_t)tb.denom;
    #endif
#else
    return 0;
#endif
}

/* High resolution timer */
static inline uint64_t lace_gethrtime(void)
{
#if (defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64))
    #if defined(_MSC_VER) && !defined(__clang__)
        unsigned aux;
        return (uint64_t)__rdtscp(&aux);   // if supported by CPU; MSVC emits rdtscp
    #elif defined(__clang__) || defined(__GNUC__)
        #if defined(__RDTSCP__)
            unsigned lo, hi, aux;
            __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux) :: "memory");
            return ((uint64_t)hi << 32) | lo;
        #else
            unsigned lo, hi;
            __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
            return ((uint64_t)hi << 32) | lo;
        #endif
    #else
        /* unknown compiler */
    #endif
#elif defined(_WIN32)
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (uint64_t)t.QuadPart;
#elif defined(__APPLE__)
    return lace_macos_now_ns();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

/* Some flags that influence Lace behavior */

#if LACE_COUNT_TASKS
#define PR_COUNTTASK(s) PR_INC(s,CTR_tasks)
#else
#define PR_COUNTTASK(s) /* Empty */
#endif

#if LACE_COUNT_STEALS
#define PR_COUNTSTEALS(s,i) PR_INC(s,i)
#else
#define PR_COUNTSTEALS(s,i) /* Empty */
#endif

#if LACE_COUNT_SPLITS
#define PR_COUNTSPLITS(s,i) PR_INC(s,i)
#else
#define PR_COUNTSPLITS(s,i) /* Empty */
#endif

#if LACE_COUNT_EVENTS
#define PR_ADD(s,i,k) ( ((s)->ctr[i])+=k )
#else
#define PR_ADD(s,i,k) /* Empty */
#endif
#define PR_INC(s,i) PR_ADD(s,i,1)

#define THIEF_EMPTY     ((struct _lace_worker_public*)0x0)
#define THIEF_TASK      ((struct _lace_worker_public*)0x1)
#define THIEF_COMPLETED ((struct _lace_worker_public*)0x2)

#define LACE_STOLEN   ((lace_worker_public*)0)
#define LACE_BUSY     ((lace_worker_public*)1)
#define LACE_NOWORK   ((lace_worker_public*)2)

#if LACE_PIE_TIMES
static LACE_UNUSED void lace_time_event( lace_worker *w, int event )
{
    uint64_t now = lace_gethrtime(),
             prev = w->time;

    switch( event ) {

        // Enter application code
        case 1 :
            if(  w->level /* level */ == 0 ) {
                PR_ADD( w, CTR_init, now - prev );
                w->level = 1;
            } else if( w->level /* level */ == 1 ) {
                PR_ADD( w, CTR_wsteal, now - prev );
                PR_ADD( w, CTR_wstealsucc, now - prev );
            } else {
                PR_ADD( w, CTR_lsteal, now - prev );
                PR_ADD( w, CTR_lstealsucc, now - prev );
            }
            break;

            // Exit application code
        case 2 :
            if( w->level /* level */ == 1 ) {
                PR_ADD( w, CTR_wapp, now - prev );
            } else {
                PR_ADD( w, CTR_lapp, now - prev );
            }
            break;

            // Enter sync on stolen
        case 3 :
            if( w->level /* level */ == 1 ) {
                PR_ADD( w, CTR_wapp, now - prev );
            } else {
                PR_ADD( w, CTR_lapp, now - prev );
            }
            w->level++;
            break;

            // Exit sync on stolen
        case 4 :
            if( w->level /* level */ == 1 ) {
                fprintf( stderr, "This should not happen, level = %d\n", w->level );
            } else {
                PR_ADD( w, CTR_lsteal, now - prev );
            }
            w->level--;
            break;

            // Return from failed steal
        case 7 :
            if( w->level /* level */ == 0 ) {
                PR_ADD( w, CTR_init, now - prev );
            } else if( w->level /* level */ == 1 ) {
                PR_ADD( w, CTR_wsteal, now - prev );
            } else {
                PR_ADD( w, CTR_lsteal, now - prev );
            }
            break;

            // Signalling time
        case 8 :
            if( w->level /* level */ == 1 ) {
                PR_ADD( w, CTR_wsignal, now - prev );
                PR_ADD( w, CTR_wsteal, now - prev );
            } else {
                PR_ADD( w, CTR_lsignal, now - prev );
                PR_ADD( w, CTR_lsteal, now - prev );
            }
            break;

            // Done
        case 9 :
            if( w->level /* level */ == 0 ) {
                PR_ADD( w, CTR_init, now - prev );
            } else {
                PR_ADD( w, CTR_close, now - prev );
            }
            break;

        default: return;
    }

    w->time = now;
}
#else
#define lace_time_event( w, e ) /* Empty */
#endif

/**
 * Helper function when a lace_task stack overflow is detected.
 */
LACE_NORETURN void lace_abort_stack_overflow(void);

/**
 * Support for interrupting Lace workers
 */

typedef struct
{
    _Atomic(lace_task*) t;
    char pad[LACE_PADDING_TARGET-sizeof(lace_task *)];
} lace_newframe_t;

extern lace_newframe_t lace_newframe;

/**
 * Interrupt the current worker and run a task in a new frame
 */
void lace_yield(lace_worker*);

/**
 * Check if current tasks must be interrupted, and if so, interrupt.
 */
static inline void lace_check_yield(lace_worker *w)
{
    if (LACE_UNLIKELY(atomic_load_explicit(&lace_newframe.t, memory_order_relaxed) != NULL)) {
        lace_yield(w);
    }
}

/**
 * Make all tasks of the current worker shared.
 */
static inline void lace_make_all_shared(void)
{
    lace_worker* w = lace_get_worker();
    if (w->split != w->head) {
        w->split = w->head;
        w->_public->ts.ts.split = (uint32_t)(w->head - w->dq);
    }
}

/**
 * Helper function for _SYNC implementations
 */
int lace_sync(lace_worker *w, lace_task *head);
'
#
# Create macros for each arity
#

for(( r = 0; r <= $nparams; r++ )) do

# Extend various argument lists
if ((r)); then
  TASK_FIELDS="$TASK_FIELDS ATYPE_$r arg_$r;"
  TASK_INIT="$TASK_INIT t->d.args.arg_$r = arg_$r;"
  if (( r == 1)); then
    MACRO_ARGS="ATYPE_$r, ARG_$r"
    DECL_ARGS=", ATYPE_1"
    TASK_GET_FROM_t=", t->d.args.arg_1"
    FUN_ARGS=", ATYPE_1 arg_1"
    RUN_ARGS="ATYPE_1 arg_1"
    CALL_ARGS=", arg_1"
  else
    MACRO_ARGS="$MACRO_ARGS, ATYPE_$r, ARG_$r"
    DECL_ARGS="$DECL_ARGS, ATYPE_$r"
    TASK_GET_FROM_t="$TASK_GET_FROM_t, t->d.args.arg_$r"
    FUN_ARGS="$FUN_ARGS, ATYPE_$r arg_$r"
    RUN_ARGS="$RUN_ARGS, ATYPE_$r arg_$r"
    CALL_ARGS="$CALL_ARGS, arg_$r"
  fi
  ARGS_STRUCT="struct { $TASK_FIELDS } args;"
else
  RUN_ARGS="void"
fi

echo
echo "// lace_task macros for tasks of arity $r"
echo

# Create a void and a non-void version
for isvoid in 0 1; do
if (( isvoid==0 )); then
  if ((r)); then
    DEF_MACRO="#define TASK_$r(RTYPE, NAME, $MACRO_ARGS)"
  else
    DEF_MACRO="#define TASK_$r(RTYPE, NAME)"
  fi
  RTYPE="RTYPE"
  RES_FIELD="$RTYPE res;"
  SAVE_RVAL="t->d.res ="
  RETURN_RES="((TD_##NAME *)t)->d.res"
  UNION="union { $ARGS_STRUCT $RTYPE res; } d;"
  SS_RETURN="return "
  SS_RETURN2=""
else
  if ((r)); then
    DEF_MACRO="#define VOID_TASK_$r(NAME, $MACRO_ARGS)"
  else
    DEF_MACRO="#define VOID_TASK_$r(NAME)"
  fi
  RTYPE="void"
  SAVE_RVAL=""
  RETURN_RES=""
  if ((r)); then UNION="union { $ARGS_STRUCT } d;"; else UNION=""; fi
  SS_RETURN=""
  SS_RETURN2="return;"
fi

# Write down the macro for the task declaration
(\
echo "$DEF_MACRO

typedef struct _TD_##NAME {
  TASK_COMMON_FIELDS(_TD_##NAME)
  $UNION
} TD_##NAME;

static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), \"TD_\" #NAME \" is too large to fit in the lace_task struct!\");

$RTYPE NAME##_CALL(lace_worker*$DECL_ARGS);

static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)
{
    $SAVE_RVAL NAME##_CALL(lace_worker$TASK_GET_FROM_t);
}

static inline LACE_UNUSED
lace_task* NAME##_SPAWN(lace_worker* _lace_worker$FUN_ARGS)
{
    PR_COUNTTASK(_lace_worker);

    lace_task *lace_head = _lace_worker->head;
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();

    TD_##NAME *t;
    TailSplitNA ts;
    uint32_t head, split, newsplit;

    t = (TD_##NAME *)lace_head;
    t->f = &NAME##_WRAP;
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);
    $TASK_INIT
    atomic_thread_fence(memory_order_acquire);

    lace_worker_public *wt = _lace_worker->_public;
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {
        if (wt->movesplit) wt->movesplit = 0;
        head = (uint32_t)(lace_head - _lace_worker->dq);
        ts.ts.tail = head;
        ts.ts.split = head+1;
        wt->ts.v = ts.v;
        wt->allstolen = 0;
        _lace_worker->split = lace_head+1;
        _lace_worker->allstolen = 0;
    } else if (LACE_UNLIKELY(wt->movesplit)) {
        head = (uint32_t)(lace_head - _lace_worker->dq);
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);
        newsplit = (split + head + 2)/2;
        wt->ts.ts.split = newsplit;
        _lace_worker->split = _lace_worker->dq + newsplit;
        wt->movesplit = 0;
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);
    }

    _lace_worker->head = lace_head+1;
    return lace_head;
}

static inline LACE_UNUSED
$RTYPE NAME##_NEWFRAME($RUN_ARGS)
{
    lace_task _t;
    TD_##NAME *t = (TD_##NAME *)&_t;
    t->f = &NAME##_WRAP;
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);
    $TASK_INIT
    lace_run_newframe(&_t);
    return $RETURN_RES;
}

static inline LACE_UNUSED
void NAME##_TOGETHER($RUN_ARGS)
{
    lace_task _t;
    TD_##NAME *t = (TD_##NAME *)&_t;
    t->f = &NAME##_WRAP;
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);
    $TASK_INIT
    lace_run_together(&_t);
}

static inline LACE_UNUSED
$RTYPE NAME($RUN_ARGS)
{
    lace_worker *worker = lace_get_worker();
    if (worker != NULL) {
        ${SS_RETURN}NAME##_CALL(worker$CALL_ARGS);
    } else {
        lace_task _t;
        TD_##NAME *t = (TD_##NAME *)&_t;
        t->f = &NAME##_WRAP;
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);
        $TASK_INIT
        lace_run_task(&_t);
        return $RETURN_RES;
    }
}

static inline LACE_UNUSED
$RTYPE NAME##_RUNEX($RUN_ARGS)
{
    lace_task _t;
    TD_##NAME *t = (TD_##NAME *)&_t;
    t->f = &NAME##_WRAP;
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);
    $TASK_INIT
    lace_run_task_exclusive(&_t);
    return $RETURN_RES;
}

static inline LACE_UNUSED
$RTYPE NAME##_SYNC(lace_worker* _lace_worker)
{
    lace_task* head = _lace_worker->head - 1;
    _lace_worker->head = head;

    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */
    TD_##NAME *t = (TD_##NAME *)head;

    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {
        if (LACE_LIKELY(_lace_worker->split <= head)) {
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);
            ${SS_RETURN}NAME##_CALL(_lace_worker$TASK_GET_FROM_t);
            ${SS_RETURN2}
        }
    }

    if (lace_sync(_lace_worker, head)) {
        ${SS_RETURN}$RETURN_RES;
    } else {
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);
        ${SS_RETURN}NAME##_CALL(_lace_worker$TASK_GET_FROM_t);
    }
}

" \
) | awk '{printf "%-86s\\\n", $0 }'

echo ""

done

done

echo "
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif"
