/* 
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
 */

#pragma once

/**
 * @file
 * @brief Lace: a work-stealing framework for multi-core fork-join parallelism.
 *
 * Lace provides lightweight task-based parallelism using work-stealing deques.
 * Tasks are defined with the TASK() macro and executed across a
 * pool of worker threads. Each worker maintains a private deque; idle workers
 * steal tasks from busy ones.
 *
 * Quick start:
 * @code
 * #include <lace.h>
 *
 * TASK(int, fibonacci, int, n)
 * int fibonacci_CALL(lace_worker* lw, int n) {
 *     if (n < 2) return n;
 *     fibonacci_SPAWN(lw, n-1);
 *     int a = fibonacci_CALL(lw, n-2);
 *     int b = fibonacci_SYNC(lw);
 *     return a + b;
 * }
 *
 * int main(void) {
 *     lace_start(0, 0, 0);
 *     printf("fib(42) = %d\n", fibonacci(42));
 *     lace_stop();
 * }
 * @endcode
 *
 * @see @ref lace_lifecycle "Lifecycle" for starting and stopping Lace.
 * @see @ref lace_task_macros "Task Macros" for defining parallel tasks.
 * @see @ref lace_worker_ctx "Worker Context" for thread-local queries.
 */

// Lace version
#define LACE_VERSION_MAJOR 2
#define LACE_VERSION_MINOR 3
#define LACE_VERSION_PATCH 2

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

#if defined(__has_feature)
    #if __has_feature(thread_sanitizer)
        #define LACE_NO_SANITIZE_THREAD __attribute__((no_sanitize("thread")))
    #else
        #define LACE_NO_SANITIZE_THREAD
    #endif
#else
    #define LACE_NO_SANITIZE_THREAD
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
#define LACE_TASKSIZE (64)
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// Forward declarations
typedef struct lace_worker lace_worker;
typedef struct lace_task lace_task;

/**
 * @defgroup lace_lifecycle Lifecycle
 * @brief Start and stop the Lace worker pool.
 *
 * These functions manage the Lace runtime. Call lace_start() to spawn worker
 * threads and lace_stop() to shut them down. Both must be called from outside
 * a Lace worker thread.
 * @{
 */

/**
 * Start Lace and spawn worker threads.
 *
 * Allocates per-worker deques and launches @p n_workers threads.
 * Workers begin busy-waiting for tasks immediately. If LACE_BACKOFF
 * is enabled (the default), CPU usage drops to near zero after roughly
 * one second of inactivity.
 *
 * Deques are allocated as virtual memory and physical pages are committed
 * lazily, so a large @p dqsize has no upfront memory cost.
 *
 * When LACE_USE_HWLOC is enabled, worker threads are pinned to CPU cores.
 *
 * @param n_workers  Number of worker threads. Pass 0 to auto-detect
 *                   available cores.
 * @param dqsize     Task deque size per worker (number of task slots).
 *                   Pass 0 for a default of 100 000. Each live SPAWN that
 *                   has not yet been SYNCed occupies one slot.
 * @param stacksize  Worker thread stack size in bytes. Pass 0 for the
 *                   minimum of 16 MB and the calling thread's stack size.
 *
 * @see lace_stop
 */
void lace_start(unsigned int n_workers, size_t dqsize, size_t stacksize);

/**
 * Stop all workers and free resources.
 *
 * Must be called from a thread that is not a Lace worker.
 * Do not call from a signal handler.
 *
 * @see lace_start
 */
void lace_stop(void);

/**
 * Check whether the Lace runtime is currently active.
 *
 * @return 1 if Lace is running, 0 otherwise
 */
int lace_is_running(void);

/**
 * Set the verbosity level for Lace startup messages.
 *
 * Call before lace_start(). Level 0 (default) suppresses output;
 * level 1 prints startup diagnostics.
 *
 * @param level  Verbosity level (0 = silent, 1 = verbose)
 */
void lace_set_verbosity(int level);

/** @} */ /* end lace_lifecycle */

/**
 * @defgroup lace_worker_ctx Worker Context
 * @brief Query worker identity and thread-local state.
 * @{
 */

/**
 * Get the number of Lace worker threads.
 *
 * @return Number of workers started by lace_start()
 */
unsigned int lace_worker_count(void);

/**
 * Check whether the calling thread is a Lace worker.
 *
 * @return 1 if called from a Lace worker thread, 0 otherwise
 */
static inline int lace_is_worker(void) LACE_UNUSED;

/**
 * Get the calling worker's private data.
 *
 * @return Pointer to the current lace_worker, or NULL if not a worker
 */
static inline lace_worker* lace_get_worker(void) LACE_UNUSED;

/**
 * Get the calling worker's integer ID.
 *
 * @return Worker ID (0-based), or -1 if not called from a Lace worker
 */
static inline int lace_worker_id(void) LACE_UNUSED;

/**
 * Thread-local pseudo-random number generator (xoroshiro128**).
 *
 * Each worker has its own RNG state, so this function is contention-free.
 *
 * @param lw  Pointer to the current worker (from lace_get_worker())
 * @return    A pseudo-random 64-bit value
 */
static inline uint64_t lace_rng(lace_worker *lw) LACE_UNUSED;

/** @} */ /* end lace_worker_ctx */

/**
 * @defgroup lace_task_ops Task Operations
 * @brief Barriers, steal control, and task inspection.
 *
 * These functions provide low-level control over task execution.
 * For normal fork-join parallelism, use the SPAWN / SYNC / DROP
 * functions generated by the TASK_N() macros instead.
 * @{
 */

/**
 * Collective barrier across all workers.
 *
 * All workers must reach this call before any of them returns from it.
 * Must be called from inside a Lace task.
 *
 * @warning Deadlock will occur if not all workers reach the lace_barrier().
 */
void lace_barrier(void);

/**
 * Drop the last spawned task without retrieving its result.
 *
 * If the task has not been stolen, it is cancelled. If it has already been
 * stolen, the thief will complete it but the result is discarded. Must follow
 * LIFO order relative to other SPAWN/SYNC/DROP calls.
 *
 * @param lw  Pointer to the current worker
 */
void lace_drop(lace_worker *lw);

/**
 * Check whether a task has been stolen by another worker.
 *
 * @param t  Pointer to a task (as returned by NAME_SPAWN)
 * @return   1 if stolen, 0 otherwise
 */
static inline int lace_is_stolen_task(lace_task* t) LACE_UNUSED;

/**
 * Check whether a task has been completed.
 *
 * @param t  Pointer to a task (as returned by NAME_SPAWN)
 * @return   1 if completed, 0 otherwise
 */
static inline int lace_is_completed_task(lace_task* t) LACE_UNUSED;

/**
 * Retrieve a pointer to the result storage inside a completed task.
 *
 * The result is available after lace_is_completed_task() returns 1.
 *
 * @param t  Pointer to a completed lace_task
 */
static inline void* lace_task_result(lace_task* t) LACE_UNUSED;

/**
 * Attempt to steal and execute a random task from another worker.
 *
 * This is a low-level function for the uncommon case where a task
 * needs to block on an external condition and wants to keep its
 * worker productive. In normal fork-join code the framework handles
 * work distribution through SYNC automatically.
 *
 * @param lw  Pointer to the current worker
 */
void lace_steal_random(lace_worker* lw);

/**
 * Check for pending NEWFRAME/TOGETHER interruptions and yield if needed.
 *
 * Call periodically from long-running tasks to cooperate with
 * interruptions (e.g. stop-the-world garbage collection).
 *
 * @param lw  Pointer to the current worker
 * @return    1 if yielded to an interruption, 0 otherwise.
 */
static inline int lace_check_yield(lace_worker* lw) LACE_UNUSED;

/**
 * Make all tasks on the current worker's deque stealable.
 *
 * Normally only tasks up to the split point are visible to thieves.
 * This moves the split to the head, exposing all pending tasks.
 */
static inline void lace_make_all_shared(void) LACE_UNUSED;

/**
 * Get the current head pointer of the calling worker's deque.
 *
 * @return Pointer to the task at the head of the deque
 */
static inline lace_task *lace_get_head(void) LACE_UNUSED;

/** @} */ /* end lace_task_ops */

/**
 * @defgroup lace_stats Statistics
 * @brief Optional runtime statistics counters.
 *
 * These functions report data collected by the LACE_COUNT_TASKS,
 * LACE_COUNT_STEALS, LACE_COUNT_SPLITS, and LACE_PIE_TIMES build
 * options. If none are enabled, reports will be empty.
 * @{
 */

/**
 * Reset all internal statistics counters.
 */
void lace_count_reset(void);

/**
 * Write a statistics report to the given file.
 *
 * @param file  Output stream (e.g. stdout or an open FILE*)
 */
void lace_count_report_file(FILE *file);

/**
 * Write a statistics report to stdout.
 *
 * Convenience wrapper around lace_count_report_file().
 */
static inline LACE_UNUSED void lace_count_report(void)
{
    lace_count_report_file(stdout);
}

/** @} */ /* end lace_stats */

/**
 * @defgroup lace_misc Miscellaneous
 * @{
 */

#if defined(_WIN32)
    /**
     * Sleep for the given number of microseconds.
     *
     * On Windows, resolution is limited to whole milliseconds.
     *
     * @param microseconds  Duration to sleep
     */
    // not inline, because we do not want to pull in windows.h here
    // also Windows sleep has a ms resolution, so it is not very practical anyway...
    void lace_sleep_us(int64_t microseconds);
#else
    #include <time.h>
    /**
     * Sleep for the given number of microseconds.
     *
     * Uses nanosleep() and is precise to the microsecond on POSIX systems.
     *
     * @param microseconds  Duration to sleep
     */
    static inline void lace_sleep_us(int64_t microseconds) {
        if (microseconds <= 0) return;
        struct timespec ts;
        ts.tv_sec = (time_t)(microseconds / 1000000);
        ts.tv_nsec = (long)((microseconds % 1000000) * 1000);
        nanosleep(&ts, NULL);
    }
#endif

/** @} */ /* end lace_misc */

/**
 * @defgroup lace_internals Internals
 * @brief Internal data structures; not part of the public API contract.
 * @{
 */

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

typedef struct lace_worker_public lace_worker_public;

#define TASK_COMMON_FIELDS                      \
    void (*f)(lace_worker *, lace_task *);      \
    _Atomic(struct lace_worker_public*) thief;

typedef struct lace_task {
    TASK_COMMON_FIELDS
    char d[LACE_TASKSIZE-sizeof(void*)-sizeof(struct lace_worker_public*)];
} lace_task;

static_assert(LACE_PADDING_TARGET % 32 == 0, "LACE_PADDING_TARGET must be a multiple of 32");
static_assert(sizeof(lace_task) == 64, "A Lace task should be 64 bytes.");

typedef union {
    struct {
        _Atomic(uint32_t) tail;
        _Atomic(uint32_t) split;
    } ts;
    LACE_ALIGN(8) _Atomic(uint64_t) v;
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

typedef struct lace_worker_public {
    lace_task *dq;
    TailSplit ts;
    uint8_t allstolen;

    LACE_ALIGN(LACE_PADDING_TARGET) uint8_t movesplit;
} lace_worker_public;

#if LACE_MSVC
#pragma warning(pop)
#endif

typedef struct { uint64_t s0, s1; } lace_rng_state;

typedef struct lace_worker {
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

/** @} */ /* end lace_internals */

/* Implementations of inline functions declared above */

static inline lace_worker* lace_get_worker(void)
{
    return lace_thread_worker;
}

static inline int lace_is_worker(void)
{
    return lace_get_worker() != NULL ? 1 : 0;
}

static inline lace_task *lace_get_head(void)
{
    return lace_get_worker()->head;
}

/**
 * @ingroup lace_internals
 * Helper function to execute a task from outside a Lace worker.
 * @param task  Pointer to a prepared lace_task
 */
void lace_run_task(lace_task *task);

/**
 * @ingroup lace_internals
 * Start a new task frame (used by NAME_NEWFRAME).
 *
 * All workers suspend their current frame and cooperatively execute
 * @p task. Normal execution resumes once the new frame completes.
 *
 * @param task  Pointer to a prepared lace_task
 */
void lace_run_newframe(lace_task *task);

/**
 * @ingroup lace_internals
 * Run a task on every worker simultaneously (used by NAME_TOGETHER).
 *
 * All workers enter a barrier, execute a copy of @p task, and exit
 * through a second barrier.
 *
 * @param task  Pointer to a prepared lace_task
 */
void lace_run_together(lace_task *task);

static inline int lace_worker_id(void)
{
    return lace_get_worker() == NULL ? -1 : lace_get_worker()->worker;
}

static inline int lace_is_stolen_task(lace_task* t)
{
    return ((size_t)(lace_worker_public*)atomic_load_explicit(&t->thief, memory_order_relaxed) > 1) ? 1 : 0;
}

static inline int lace_is_completed_task(lace_task* t)
{
    return ((size_t)(lace_worker_public*)atomic_load_explicit(&t->thief, memory_order_relaxed) == 2) ? 1 : 0;
}

static inline void* lace_task_result(lace_task* t)
{
    return (void*)&t->d[0];
}

static inline uint64_t lace_rotl64(uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

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
#if defined(CLOCK_MONOTONIC_RAW)
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
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

#define THIEF_EMPTY     ((struct lace_worker_public*)0x0)
#define THIEF_TASK      ((struct lace_worker_public*)0x1)
#define THIEF_COMPLETED ((struct lace_worker_public*)0x2)

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
 * @ingroup lace_internals
 * Called when a deque overflow is detected. Prints an error and aborts.
 */
LACE_NORETURN void lace_abort_stack_overflow(void);

/**
 * @ingroup lace_internals
 * Support for interrupting Lace workers.
 */

typedef struct
{
    _Atomic(lace_task*) t;
    char pad[LACE_PADDING_TARGET-sizeof(lace_task *)];
} lace_newframe_t;

extern lace_newframe_t lace_newframe;

/**
 * @ingroup lace_internals
 * Yield the current worker to handle a pending NEWFRAME interruption.
 * @param lw  Pointer to the current worker
 */
void lace_yield(lace_worker* lw);

static inline int lace_check_yield(lace_worker *lw)
{
    if (LACE_UNLIKELY(atomic_load_explicit(&lace_newframe.t, memory_order_relaxed) != NULL)) {
        atomic_thread_fence(memory_order_acquire);
        lace_yield(lw);
        return 1;
    } else {
        return 0;
    }
}

static inline void lace_make_all_shared(void)
{
    lace_worker* w = lace_get_worker();
    if (w->split != w->head) {
        w->split = w->head;
        atomic_store_explicit(&w->_public->ts.ts.split, (uint32_t)(w->head - w->dq), memory_order_relaxed);
    }
}

/**
 * @ingroup lace_internals
 * Helper for SYNC implementations. Handles the slow path when a task
 * may have been stolen.
 *
 * @param w     Pointer to the current worker
 * @param head  Pointer to the task being synced
 * @return      1 if the task was completed by a thief, 0 if it should
 *              be executed locally
 */
int lace_sync(lace_worker *w, lace_task *head);

#define LACE_PASTE_(a, b) a ## b
#define LACE_PASTE(a, b)  LACE_PASTE_(a, b)

#define LACE_NARG(...) LACE_NARG_(__VA_ARGS__ __VA_OPT__(,) \
    30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0)
#define LACE_NARG_( \
    _1,_2,_3,_4,_5,_6,_7,_8,_9,_10, \
    _11,_12,_13,_14,_15,_16,_17,_18,_19,_20,_21,_22,_23,_24,_25,_26,_27,_28,_29,_30,N,...) N

#define LACE_SECOND_(a, b, ...) b
#define LACE_PROBE() ~, 1
#define LACE_IS_PROBE(...) LACE_SECOND_(__VA_ARGS__, 0, ~)
#define LACE_IS_VOID(T) LACE_IS_PROBE(LACE_PASTE(LACE_IS_VOID_HELPER_, T))
#define LACE_IS_VOID_HELPER_void LACE_PROBE()

#define LACE_ARITY(n) LACE_ARITY_I(n)
#define LACE_ARITY_I(n) LACE_ARITY_##n
#define LACE_ARITY_2  0
#define LACE_ARITY_4  1
#define LACE_ARITY_6  2
#define LACE_ARITY_8  3
#define LACE_ARITY_10 4
#define LACE_ARITY_12 5
#define LACE_ARITY_14 6
#define LACE_ARITY_16 7
#define LACE_ARITY_18 8
#define LACE_ARITY_20 9
#define LACE_ARITY_22 10
#define LACE_ARITY_24 11
#define LACE_ARITY_26 12
#define LACE_ARITY_28 13
#define LACE_ARITY_30 14

#define LACE_VARITY(n) LACE_VARITY_I(n)
#define LACE_VARITY_I(n) LACE_VARITY_##n
#define LACE_VARITY_1  0
#define LACE_VARITY_3  1
#define LACE_VARITY_5  2
#define LACE_VARITY_7  3
#define LACE_VARITY_9  4
#define LACE_VARITY_11 5
#define LACE_VARITY_13 6
#define LACE_VARITY_15 7
#define LACE_VARITY_17 8
#define LACE_VARITY_19 9
#define LACE_VARITY_21 10
#define LACE_VARITY_23 11
#define LACE_VARITY_25 12
#define LACE_VARITY_27 13
#define LACE_VARITY_29 14

#define TASK(RTYPE, ...) LACE_PASTE(LACE_TASK_V_, LACE_IS_VOID(RTYPE))(RTYPE, __VA_ARGS__)
#define LACE_TASK_V_0(RTYPE, ...) \
    LACE_PASTE(TASK_, LACE_ARITY(LACE_NARG(RTYPE, __VA_ARGS__)))(RTYPE, __VA_ARGS__)
#define LACE_TASK_V_1(RTYPE, ...) \
    LACE_PASTE(VOID_TASK_, LACE_VARITY(LACE_NARG(__VA_ARGS__)))(__VA_ARGS__)


/**
 * @defgroup lace_task_macros Task Macros
 * @brief Macros for defining parallel tasks of varying arity.
 *
 * Use TASK_N(RTYPE, NAME, ...) to declare a task with N parameters and
 * return type RTYPE. Use VOID_TASK_N(NAME, ...) for void tasks.
 *
 * Each macro generates the following functions:
 * - \b NAME_CALL(lw, ...)   — the task body; you implement this.
 * - \b NAME(...)            — run the task (works from any thread).
 * - \b NAME_SPAWN(lw, ...)  — fork: push task onto the deque.
 * - \b NAME_SYNC(lw)        — join: retrieve spawned result (LIFO order).
 * - \b NAME_DROP(lw)        — cancel or discard the last spawned task.
 * - \b NAME_NEWFRAME(...)   — interrupt all workers and run this task.
 * - \b NAME_TOGETHER(...)   — interrupt all workers and run on each.
 *
 * SPAWN and SYNC must be matched in strict LIFO order.
 * @{
 */


// Task macros for arity 0

#define TASK_0(RTYPE, NAME)                                                           \
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union {  RTYPE res; } d;                                                            \
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*);                                                      \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
    ((task_##NAME*)t)->d.res = NAME##_CALL(lw);                                       \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker)                                    \
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(void)                                                           \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_newframe(&_t);                                                           \
    return ((task_##NAME *)t)->d.res;                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(void)                                                            \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
RTYPE NAME(void)                                                                      \
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        return NAME##_CALL(worker);                                                   \
    } else {                                                                          \
        lace_task _t;                                                                 \
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
                                                                                      \
        lace_run_task(&_t);                                                           \
        return ((task_##NAME *)t)->d.res;                                             \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
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
        return ((task_##NAME *)t)->d.res;                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker);                                             \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_0(NAME)                                                             \
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
                                                                                      \
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*);                                                       \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
     NAME##_CALL(lw);                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker)                                    \
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(void)                                                            \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(void)                                                            \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline LACE_UNUSED                                                             \
void NAME(void)                                                                       \
{                                                                                     \
    lace_worker *worker = lace_get_worker();                                          \
    if (worker != NULL) {                                                             \
        NAME##_CALL(worker);                                                          \
    } else {                                                                          \
        lace_task _t;                                                                 \
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
                                                                                      \
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
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
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for arity 1

#define TASK_1(RTYPE, NAME, ATYPE_1, ARG_1)                                           \
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; } args; RTYPE res; } d;                            \
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1);                                             \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
    ((task_##NAME*)t)->d.res = NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1);      \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1)                     \
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1)                                                  \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_newframe(&_t);                                                           \
    return ((task_##NAME *)t)->d.res;                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1)                                                   \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1;                                                     \
        lace_run_task(&_t);                                                           \
        return ((task_##NAME *)t)->d.res;                                             \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1);        \
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((task_##NAME *)t)->d.res;                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1);            \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_1(NAME, ATYPE_1, ARG_1)                                             \
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; } args; } d;                                       \
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1);                                              \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
     NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1);                                \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1)                     \
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1)                                                   \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1)                                                   \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1;                                                     \
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1);               \
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1);                   \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for arity 2

#define TASK_2(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2)                           \
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; } args; RTYPE res; } d;             \
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2);                                    \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
    ((task_##NAME*)t)->d.res = NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2);\
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2)      \
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2)                                   \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_newframe(&_t);                                                           \
    return ((task_##NAME *)t)->d.res;                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2)                                    \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                            \
        lace_run_task(&_t);                                                           \
        return ((task_##NAME *)t)->d.res;                                             \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((task_##NAME *)t)->d.res;                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_2(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2)                             \
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; } args; } d;                        \
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2);                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
     NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2);\
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2)      \
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2)                                    \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2)                                    \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                            \
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for arity 3

#define TASK_3(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3)           \
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; } args; RTYPE res; } d;\
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3);                           \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
    ((task_##NAME*)t)->d.res = NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3);\
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                    \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_newframe(&_t);                                                           \
    return ((task_##NAME *)t)->d.res;                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                     \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;   \
        lace_run_task(&_t);                                                           \
        return ((task_##NAME *)t)->d.res;                                             \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((task_##NAME *)t)->d.res;                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_3(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3)             \
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; } args; } d;         \
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3);                            \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
     NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3);\
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                     \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                     \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;   \
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for arity 4

#define TASK_4(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4)\
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; } args; RTYPE res; } d;\
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4);                  \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
    ((task_##NAME*)t)->d.res = NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4);\
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)     \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_newframe(&_t);                                                           \
    return ((task_##NAME *)t)->d.res;                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)      \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
        lace_run_task(&_t);                                                           \
        return ((task_##NAME *)t)->d.res;                                             \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((task_##NAME *)t)->d.res;                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_4(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4)\
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; } args; } d;\
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4);                   \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
     NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4);\
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)      \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)      \
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for arity 5

#define TASK_5(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5)\
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; } args; RTYPE res; } d;\
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5);         \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
    ((task_##NAME*)t)->d.res = NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5);\
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_newframe(&_t);                                                           \
    return ((task_##NAME *)t)->d.res;                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
        lace_run_task(&_t);                                                           \
        return ((task_##NAME *)t)->d.res;                                             \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((task_##NAME *)t)->d.res;                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_5(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5)\
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; } args; } d;\
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5);          \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
     NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5);\
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for arity 6

#define TASK_6(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6)\
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; } args; RTYPE res; } d;\
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6);\
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
    ((task_##NAME*)t)->d.res = NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6);\
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_newframe(&_t);                                                           \
    return ((task_##NAME *)t)->d.res;                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
        lace_run_task(&_t);                                                           \
        return ((task_##NAME *)t)->d.res;                                             \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((task_##NAME *)t)->d.res;                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_6(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6)\
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; } args; } d;\
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6); \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
     NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6);\
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for arity 7

#define TASK_7(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7)\
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; } args; RTYPE res; } d;\
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7);\
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
    ((task_##NAME*)t)->d.res = NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7);\
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
    lace_run_newframe(&_t);                                                           \
    return ((task_##NAME *)t)->d.res;                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
        lace_run_task(&_t);                                                           \
        return ((task_##NAME *)t)->d.res;                                             \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((task_##NAME *)t)->d.res;                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_7(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7)\
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; } args; } d;\
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7);\
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
     NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7);\
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for arity 8

#define TASK_8(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8)\
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; } args; RTYPE res; } d;\
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8);\
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
    ((task_##NAME*)t)->d.res = NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7, ((task_##NAME*)t)->d.args.arg_8);\
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
    lace_run_newframe(&_t);                                                           \
    return ((task_##NAME *)t)->d.res;                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
        lace_run_task(&_t);                                                           \
        return ((task_##NAME *)t)->d.res;                                             \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7, ((task_##NAME*)t)->d.args.arg_8);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((task_##NAME *)t)->d.res;                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7, ((task_##NAME*)t)->d.args.arg_8);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_8(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8)\
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; } args; } d;\
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8);\
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
     NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7, ((task_##NAME*)t)->d.args.arg_8);\
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7, ((task_##NAME*)t)->d.args.arg_8);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7, ((task_##NAME*)t)->d.args.arg_8);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for arity 9

#define TASK_9(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9)\
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; } args; RTYPE res; } d;\
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9);\
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
    ((task_##NAME*)t)->d.res = NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7, ((task_##NAME*)t)->d.args.arg_8, ((task_##NAME*)t)->d.args.arg_9);\
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
    lace_run_newframe(&_t);                                                           \
    return ((task_##NAME *)t)->d.res;                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
        lace_run_task(&_t);                                                           \
        return ((task_##NAME *)t)->d.res;                                             \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7, ((task_##NAME*)t)->d.args.arg_8, ((task_##NAME*)t)->d.args.arg_9);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((task_##NAME *)t)->d.res;                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7, ((task_##NAME*)t)->d.args.arg_8, ((task_##NAME*)t)->d.args.arg_9);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_9(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9)\
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; } args; } d;\
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9);\
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
     NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7, ((task_##NAME*)t)->d.args.arg_8, ((task_##NAME*)t)->d.args.arg_9);\
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7, ((task_##NAME*)t)->d.args.arg_8, ((task_##NAME*)t)->d.args.arg_9);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7, ((task_##NAME*)t)->d.args.arg_8, ((task_##NAME*)t)->d.args.arg_9);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for arity 10

#define TASK_10(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9, ATYPE_10, ARG_10)\
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; ATYPE_10 arg_10; } args; RTYPE res; } d;\
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
RTYPE NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10);\
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
    ((task_##NAME*)t)->d.res = NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7, ((task_##NAME*)t)->d.args.arg_8, ((task_##NAME*)t)->d.args.arg_9, ((task_##NAME*)t)->d.args.arg_10);\
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
    lace_run_newframe(&_t);                                                           \
    return ((task_##NAME *)t)->d.res;                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
        lace_run_task(&_t);                                                           \
        return ((task_##NAME *)t)->d.res;                                             \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
RTYPE NAME##_SYNC(lace_worker* _lace_worker)                                          \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7, ((task_##NAME*)t)->d.args.arg_8, ((task_##NAME*)t)->d.args.arg_9, ((task_##NAME*)t)->d.args.arg_10);\
                                                                                      \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        return ((task_##NAME *)t)->d.res;                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7, ((task_##NAME*)t)->d.args.arg_8, ((task_##NAME*)t)->d.args.arg_9, ((task_##NAME*)t)->d.args.arg_10);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \

#define VOID_TASK_10(NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9, ATYPE_10, ARG_10)\
                                                                                      \
typedef struct task_##NAME {                                                          \
  TASK_COMMON_FIELDS                                                                  \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; ATYPE_10 arg_10; } args; } d;\
} task_##NAME;                                                                        \
                                                                                      \
static_assert(sizeof(task_##NAME) <= sizeof(lace_task), "task_" #NAME " is too large to fit in the lace_task struct!");\
                                                                                      \
void NAME##_CALL(lace_worker*, ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10);\
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static void NAME##_WRAP(lace_worker* lw, lace_task* t)                                \
{                                                                                     \
    (void)t;                                                                          \
     NAME##_CALL(lw, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7, ((task_##NAME*)t)->d.args.arg_8, ((task_##NAME*)t)->d.args.arg_9, ((task_##NAME*)t)->d.args.arg_10);\
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
lace_task* NAME##_SPAWN(lace_worker* _lace_worker, ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    PR_COUNTTASK(_lace_worker);                                                       \
                                                                                      \
    lace_task *lace_head = _lace_worker->head;                                        \
    if (lace_head == _lace_worker->end) lace_abort_stack_overflow();                  \
                                                                                      \
    task_##NAME *t;                                                                   \
    TailSplitNA ts;                                                                   \
    uint32_t head, split, newsplit;                                                   \
                                                                                      \
    t = (task_##NAME *)lace_head;                                                     \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
    atomic_thread_fence(memory_order_release);                                        \
    /* do not allow later stores to float above */                                    \
                                                                                      \
    lace_worker_public *wt = _lace_worker->_public;                                   \
    if (LACE_UNLIKELY(_lace_worker->allstolen)) {                                     \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        ts.ts.tail = head;                                                            \
        ts.ts.split = head+1;                                                         \
        atomic_store_explicit(&wt->ts.v, ts.v, memory_order_relaxed);;                \
        wt->allstolen = 0;                                                            \
        _lace_worker->split = lace_head+1;                                            \
        _lace_worker->allstolen = 0;                                                  \
    } else if (LACE_UNLIKELY(wt->movesplit)) {                                        \
        head = (uint32_t)(lace_head - _lace_worker->dq);                              \
        split = (uint32_t)(_lace_worker->split - _lace_worker->dq);                   \
        newsplit = (split + head + 2)/2;                                              \
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed);      \
        _lace_worker->split = _lace_worker->dq + newsplit;                            \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(_lace_worker, CTR_split_grow);                                 \
    }                                                                                 \
                                                                                      \
    _lace_worker->head = lace_head+1;                                                 \
    return lace_head;                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    lace_task _t;                                                                     \
    task_##NAME *t = (task_##NAME *)&_t;                                              \
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
        task_##NAME *t = (task_##NAME *)&_t;                                          \
        t->f = &NAME##_WRAP;                                                          \
        atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);           \
         t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
        lace_run_task(&_t);                                                           \
        return ;                                                                      \
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_SYNC(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_task* head = _lace_worker->head - 1;                                         \
    _lace_worker->head = head;                                                        \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    task_##NAME *t = (task_##NAME *)head;                                             \
                                                                                      \
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {                         \
        if (LACE_LIKELY(_lace_worker->split <= head)) {                               \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7, ((task_##NAME*)t)->d.args.arg_8, ((task_##NAME*)t)->d.args.arg_9, ((task_##NAME*)t)->d.args.arg_10);\
            return;                                                                   \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(_lace_worker, head)) {                                              \
        ;                                                                             \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        NAME##_CALL(_lace_worker, ((task_##NAME*)t)->d.args.arg_1, ((task_##NAME*)t)->d.args.arg_2, ((task_##NAME*)t)->d.args.arg_3, ((task_##NAME*)t)->d.args.arg_4, ((task_##NAME*)t)->d.args.arg_5, ((task_##NAME*)t)->d.args.arg_6, ((task_##NAME*)t)->d.args.arg_7, ((task_##NAME*)t)->d.args.arg_8, ((task_##NAME*)t)->d.args.arg_9, ((task_##NAME*)t)->d.args.arg_10);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
LACE_NO_SANITIZE_THREAD                                                               \
static inline LACE_UNUSED                                                             \
void NAME##_DROP(lace_worker* _lace_worker)                                           \
{                                                                                     \
    lace_drop(_lace_worker);                                                          \
}                                                                                     \
                                                                                      \
                                                                                      \


/** @} */ /* end lace_task_macros */

#ifdef __cplusplus
}
#endif /* __cplusplus */
