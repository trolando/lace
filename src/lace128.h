/* 
 * Copyright 2013-2016 Formal Methods and Tools, University of Twente
 * Copyright 2016-2018 Tom van Dijk, Johannes Kepler University Linz
 * Copyright 2019-2026 Tom van Dijk, Formal Methods and Tools, University of Twente
 *
 * Licensed under the Apache License, Version 2.0 (the License);
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
#define LACE_TASKSIZE (128)
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
static_assert(sizeof(lace_task) == 128, "A Lace task should be 128 bytes.");

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


// lace_task macros for tasks of arity 0

#define TASK_0(RTYPE, NAME)                                                           \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union {  RTYPE res; } d;                                                            \
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*);                                                      \
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker);                                              \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker)                                    \
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME()                                                               \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER()                                                                \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME()                                                                          \
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker);                                                   \
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
                                                                                      \
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_RUNEX()                                                                  \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker);                                         \
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker);                                             \
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_0(NAME)                                                             \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
                                                                                      \
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*);                                                       \
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
     NAME##_CALL(lace_worker);                                                        \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker)                                    \
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME()                                                                \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER()                                                                \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME()                                                                           \
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker);                                                          \
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
                                                                                      \
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_RUNEX()                                                                   \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker);                                                \
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker);                                                    \
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// lace_task macros for tasks of arity 1

#define TASK_1(RTYPE, NAME, ATYPE_1, ARG_1)                                           \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; } args; RTYPE res; } d;                            \
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1);                                             \
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1);                             \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1)                     \
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1)                                                  \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1)                                                   \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME(ATYPE_1 arg_1)                                                             \
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1);                                            \
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1;                                                     \
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1)                                                     \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1);                        \
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1);                            \
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_1(NAME, ATYPE_1, ARG_1)                                             \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; } args; } d;                                       \
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1);                                              \
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1);                                       \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1)                     \
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1)                                                   \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1)                                                   \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME(ATYPE_1 arg_1)                                                              \
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1);                                                   \
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1;                                                     \
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_RUNEX(ATYPE_1 arg_1)                                                      \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1);                               \
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1);                                   \
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// lace_task macros for tasks of arity 2

#define TASK_2(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2)                           \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; } args; RTYPE res; } d;             \
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2);                                    \
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2);            \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2)      \
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2)                                   \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2)                                    \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME(ATYPE_1 arg_1, ATYPE_2 arg_2)                                              \
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1, arg_2);                                     \
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                            \
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2)                                      \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2);       \
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2);           \
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_2(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2)                             \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; } args; } d;                        \
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2);                                     \
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2);                      \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2)      \
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2)                                    \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2)                                    \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME(ATYPE_1 arg_1, ATYPE_2 arg_2)                                               \
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1, arg_2);                                            \
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                            \
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2)                                       \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2);              \
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2);                  \
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// lace_task macros for tasks of arity 3

#define TASK_3(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3)           \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3);                           \
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                    \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                     \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                               \
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1, arg_2, arg_3);                              \
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;   \
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                       \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_3(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3)             \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; } args; } d;         \
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3);                            \
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3);     \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                     \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                     \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                                \
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1, arg_2, arg_3);                                     \
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;   \
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                        \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3); \
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// lace_task macros for tasks of arity 4

#define TASK_4(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4);                  \
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)     \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)      \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)                \
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4);                       \
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)        \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_4(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; } args; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4);                   \
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)      \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)      \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)                 \
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4);                              \
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)         \
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// lace_task macros for tasks of arity 5

#define TASK_5(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5);         \
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5) \
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5);                \
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_5(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; } args; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5);          \
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)  \
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5);                       \
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// lace_task macros for tasks of arity 6

#define TASK_6(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6);\
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);         \
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_6(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; } args; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6); \
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);                \
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// lace_task macros for tasks of arity 7

#define TASK_7(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7);\
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7);  \
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_7(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; } args; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7);\
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7);         \
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// lace_task macros for tasks of arity 8

#define TASK_8(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8);\
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8);\
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_8(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; } args; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8);\
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8);  \
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// lace_task macros for tasks of arity 9

#define TASK_9(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9);\
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9);\
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_9(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; } args; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9);\
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9);\
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// lace_task macros for tasks of arity 10

#define TASK_10(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9, ATYPE_10, ARG_10)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; ATYPE_10 arg_10; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10);\
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10);\
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_10(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9, ATYPE_10, ARG_10)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; ATYPE_10 arg_10; } args; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10);\
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10);\
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// lace_task macros for tasks of arity 11

#define TASK_11(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9, ATYPE_10, ARG_10, ATYPE_11, ARG_11)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; ATYPE_10 arg_10; ATYPE_11 arg_11; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10, ATYPE_11);\
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11)\
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, arg_11);\
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11;\
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11;\
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_11(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9, ATYPE_10, ARG_10, ATYPE_11, ARG_11)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; ATYPE_10 arg_10; ATYPE_11 arg_11; } args; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10, ATYPE_11);\
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11)\
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, arg_11);\
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11;\
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// lace_task macros for tasks of arity 12

#define TASK_12(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9, ATYPE_10, ARG_10, ATYPE_11, ARG_11, ATYPE_12, ARG_12)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; ATYPE_10 arg_10; ATYPE_11 arg_11; ATYPE_12 arg_12; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10, ATYPE_11, ATYPE_12);\
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12)\
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, arg_11, arg_12);\
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12;\
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12;\
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_12(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9, ATYPE_10, ARG_10, ATYPE_11, ARG_11, ATYPE_12, ARG_12)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; ATYPE_10 arg_10; ATYPE_11 arg_11; ATYPE_12 arg_12; } args; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10, ATYPE_11, ATYPE_12);\
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12)\
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, arg_11, arg_12);\
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12;\
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// lace_task macros for tasks of arity 13

#define TASK_13(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9, ATYPE_10, ARG_10, ATYPE_11, ARG_11, ATYPE_12, ARG_12, ATYPE_13, ARG_13)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; ATYPE_10 arg_10; ATYPE_11 arg_11; ATYPE_12 arg_12; ATYPE_13 arg_13; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10, ATYPE_11, ATYPE_12, ATYPE_13);\
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13)\
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, arg_11, arg_12, arg_13);\
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13;\
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13;\
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_13(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9, ATYPE_10, ARG_10, ATYPE_11, ARG_11, ATYPE_12, ARG_12, ATYPE_13, ARG_13)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; ATYPE_10 arg_10; ATYPE_11 arg_11; ATYPE_12 arg_12; ATYPE_13 arg_13; } args; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10, ATYPE_11, ATYPE_12, ATYPE_13);\
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13)\
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, arg_11, arg_12, arg_13);\
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13;\
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// lace_task macros for tasks of arity 14

#define TASK_14(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9, ATYPE_10, ARG_10, ATYPE_11, ARG_11, ATYPE_12, ARG_12, ATYPE_13, ARG_13, ATYPE_14, ARG_14)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; ATYPE_10 arg_10; ATYPE_11 arg_11; ATYPE_12 arg_12; ATYPE_13 arg_13; ATYPE_14 arg_14; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10, ATYPE_11, ATYPE_12, ATYPE_13, ATYPE_14);\
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
    t->d.res = NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13, t->d.args.arg_14);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13, ATYPE_14 arg_14)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13; t->d.args.arg_14 = arg_14;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13, ATYPE_14 arg_14)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13; t->d.args.arg_14 = arg_14;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13, ATYPE_14 arg_14)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13; t->d.args.arg_14 = arg_14;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13, ATYPE_14 arg_14)\
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, arg_11, arg_12, arg_13, arg_14);\
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13; t->d.args.arg_14 = arg_14;\
        lace_run_task(&_t);                                                           \
        return ((TD_##NAME *)t)->d.res;                                               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13, ATYPE_14 arg_14)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13; t->d.args.arg_14 = arg_14;\
    lace_run_task_exclusive(&_t);                                                     \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13, t->d.args.arg_14);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13, t->d.args.arg_14);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_14(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9, ATYPE_10, ARG_10, ATYPE_11, ARG_11, ATYPE_12, ARG_12, ATYPE_13, ARG_13, ATYPE_14, ARG_14)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; ATYPE_10 arg_10; ATYPE_11 arg_11; ATYPE_12 arg_12; ATYPE_13 arg_13; ATYPE_14 arg_14; } args; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
static_assert(sizeof(TD_##NAME) <= sizeof(lace_task), "TD_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10, ATYPE_11, ATYPE_12, ATYPE_13, ATYPE_14);\
                                                                                      \
static void NAME##_WRAP(lace_worker* lace_worker, TD_##NAME *t LACE_UNUSED)           \
{                                                                                     \
     NAME##_CALL(lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13, t->d.args.arg_14);\
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13, ATYPE_14 arg_14)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    TD_##NAME *t;                                                                     \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (TD_##NAME *)lace_head;                                                       \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13; t->d.args.arg_14 = arg_14;\
    atomic_thread_fence(memory_order_acquire);                                        \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13, ATYPE_14 arg_14)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13; t->d.args.arg_14 = arg_14;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13, ATYPE_14 arg_14)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13; t->d.args.arg_14 = arg_14;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13, ATYPE_14 arg_14)\
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, arg_11, arg_12, arg_13, arg_14);\
    } else {                                                                          \
        lace_task _t;                                                                 \
        TD_##NAME *t = (TD_##NAME *)&_t;                                              \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13; t->d.args.arg_14 = arg_14;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_RUNEX(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13, ATYPE_14 arg_14)\
{                                                                                     \
    lace_task _t;                                                                     \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13; t->d.args.arg_14 = arg_14;\
    lace_run_task_exclusive(&_t);                                                     \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13, t->d.args.arg_14);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13, t->d.args.arg_14);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
