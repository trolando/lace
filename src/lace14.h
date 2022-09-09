/* 
 * Copyright 2013-2016 Formal Methods and Tools, University of Twente
 * Copyright 2016-2017 Tom van Dijk, Johannes Kepler University Linz
 * Copyright 2019-2022 Tom van Dijk, Formal Methods and Tools, University of Twente
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

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h> /* for pthread_t */

#ifndef __cplusplus
  #include <stdatomic.h>
#else
  // Compatibility with C11
  #include <atomic>
  #define _Atomic(T) std::atomic<T>
  using std::memory_order_relaxed;
  using std::memory_order_acquire;
  using std::memory_order_release;
  using std::memory_order_seq_cst;
#endif

#include <lace_config.h>

#ifndef __LACE_H__
#define __LACE_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Type definitions used in the functions below.
 * - WorkerP contains the (private) Worker data
 * - Task contains a single Task
 */
typedef struct _WorkerP WorkerP;
typedef struct _Task Task;

/* Typical cacheline size of system architectures */
#ifndef LINE_SIZE
#define LINE_SIZE 64
#endif

/* The size of a pointer, 8 bytes on a 64-bit architecture */
#define P_SZ (sizeof(void *))

#define PAD(x,b) ( ( (b) - ((x)%(b)) ) & ((b)-1) ) /* b must be power of 2 */
#define ROUND(x,b) ( (x) + PAD( (x), (b) ) )

/* The size is in bytes. Note that this is without the extra overhead from Lace.
   The value must be greater than or equal to the maximum size of your tasks.
   The task size is the maximum of the size of the result or of the sum of the parameter sizes. */
#ifndef LACE_TASKSIZE
#define LACE_TASKSIZE (14)*P_SZ
#endif

#define TASK_COMMON_FIELDS(type)     \
    void (*f)(struct type *);        \
    _Atomic(struct _Worker*) thief;

struct __lace_common_fields_only { TASK_COMMON_FIELDS(_Task) };
#define LACE_COMMON_FIELD_SIZE sizeof(struct __lace_common_fields_only)

typedef struct _Task {
    TASK_COMMON_FIELDS(_Task);
    char p1[PAD(LACE_COMMON_FIELD_SIZE, P_SZ)];
    char d[LACE_TASKSIZE];
    char p2[PAD(ROUND(LACE_COMMON_FIELD_SIZE, P_SZ) + LACE_TASKSIZE, LINE_SIZE)];
} Task;

/* hopefully packed? */
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

typedef struct _Worker {
    Task *dq;
    TailSplit ts;
    uint8_t allstolen;

    char pad1[PAD(P_SZ+sizeof(TailSplit)+1, LINE_SIZE)];

    uint8_t movesplit;
} Worker;

typedef struct _WorkerP {
    Task *head;                 // my head
    Task *split;                // same as dq+ts.ts.split
    Task *end;                  // dq+dq_size
    Task *dq;                   // my queue
    Worker *_public;            // pointer to public Worker struct
    uint64_t rng;               // my random seed (for lace_trng)
    uint32_t seed;              // my random seed (for lace_steal_random)
    uint16_t worker;            // what is my worker id?
    uint8_t allstolen;          // my allstolen

#if LACE_COUNT_EVENTS
    uint64_t ctr[CTR_MAX];      // counters
    uint64_t time;
    int level;
#endif

    int16_t pu;                 // my pu (for HWLOC)
} WorkerP;

#ifdef __linux__
extern __thread WorkerP *lace_thread_worker;
#else
extern pthread_key_t lace_thread_worker_key;
#endif

/**
 * Set verbosity level (0 = no startup messages, 1 = startup messages)
 * Default level: 0
 */
void lace_set_verbosity(int level);

/**
 * Set the program stack size of Lace worker threads. (Not really needed, default is OK.)
 */
void lace_set_stacksize(size_t stacksize);

/**
 * Get the program stack size of Lace worker threads.
 * If this returns 0, it uses the default.
 */
size_t lace_get_stacksize(void);

/**
 * Get the number of available PUs (hardware threads)
 */
unsigned int lace_get_pu_count(void);

/**
 * Start Lace with <n_workers> workers and a a task deque size of <dqsize> per worker.
 * If <n_workers> is set to 0, automatically detects available cores.
 * If <dqsize> is est to 0, uses a reasonable default value.
 */
void lace_start(unsigned int n_workers, size_t dqsize);

/**
 * Suspend all workers.
 * Call this method from outside Lace threads.
 */
void lace_suspend(void);

/**
 * Resume all workers.
 * Call this method from outside Lace threads.
 */
void lace_resume(void);

/**
 * Stop Lace.
 * Call this method from outside Lace threads.
 */
void lace_stop(void);

/**
 * Steal a random task.
 * Only use this from inside a Lace task.
 */
void lace_steal_random(void);

/**
 * Enter the Lace barrier. (all active workers must enter it before we can continue)
 * Only run this from inside a Lace task.
 */
void lace_barrier(void);

/**
 * Retrieve the number of Lace workers
 */
unsigned int lace_worker_count(void);

/**
 * Retrieve the current worker data.
 * Only run this from inside a Lace task.
 * (Used by LACE_VARS)
 */
static inline WorkerP*
lace_get_worker(void)
{ 
#ifdef __linux__
    return lace_thread_worker;
#else
    return (WorkerP*)pthread_getspecific(lace_thread_worker_key);
#endif
}

/**
 * Retrieve whether we are running in a Lace worker. Returns 1 if this is the case, 0 otherwise.
 */
static inline int lace_is_worker(void) { return lace_get_worker() != NULL ? 1 : 0; }

/**
 * Retrieve the current head of the deque of the worker.
 */
 static inline Task *lace_get_head(void) { return lace_get_worker()->head; }

/**
 * Helper function to call from outside Lace threads.
 * This helper function is used by the _RUN methods for the RUN() macro.
 */
void lace_run_task(Task *task);

/**
 * Helper function to start a new task execution (task frame) on a given task.
 * This helper function is used by the _NEWFRAME methods for the NEWFRAME() macro
 * Only when the task is done, do workers continue with the previous task frame.
 */
void lace_run_newframe(Task *task);

/**
 * Helper function to make all run a given task together.
 * This helper function is used by the _TOGETHER methods for the TOGETHER() macro
 * They all start the task in a lace_barrier and complete it with a lace barrier.
 * Meaning they all start together, and all end together.
 */
void lace_run_together(Task *task);

/**
 * Instead of SYNCing on the next task, drop the task (unless stolen already)
 */
void lace_drop(void);

/**
 * Get the current worker id.
 */
static inline int lace_worker_id() { return lace_get_worker() == NULL ? -1 : lace_get_worker()->worker; }

/**
 * Get the core where the current worker is pinned.
 */
static inline int lace_worker_pu() { return lace_get_worker() == NULL ? -1 : lace_get_worker()->pu; }

/**
 * True if the given task is stolen, False otherwise.
 */
static inline int lace_is_stolen_task(Task* t) { return ((size_t)(Worker*)t->thief > 1) ? 1 : 0; }

/**
 * True if the given task is completed, False otherwise.
 */
static inline int lace_is_completed_task(Task* t) { return ((size_t)(Worker*)t->thief == 2) ? 1 : 0; }

/**
 * Retrieves a pointer to the result of the given task.
 */
#define lace_task_result(t) (&t->d[0])

/**
 * Check if current tasks must be interrupted, and if so, interrupt.
 */
static inline void lace_check_yield(void);

/**
 * Make all tasks of the current worker shared.
 */
static inline void lace_make_all_shared(void);

/**
 * Compute a random number, thread-local (so scalable)
 */
#define LACE_TRNG (__lace_worker->rng = 2862933555777941757ULL * __lace_worker->rng + 3037000493ULL)

/* Some flags that influence Lace behavior */

#ifndef LACE_COUNT_EVENTS
#define LACE_COUNT_EVENTS (LACE_PIE_TIMES || LACE_COUNT_TASKS || LACE_COUNT_STEALS || LACE_COUNT_SPLITS)
#endif

#if LACE_PIE_TIMES
/* High resolution timer */
static inline uint64_t gethrtime()
{
    uint32_t hi, lo;
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return (uint64_t)hi<<32 | lo;
}
#endif

#if LACE_COUNT_EVENTS
void lace_count_reset();
void lace_count_report_file(FILE *file);
#endif

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
#endif
    CTR_MAX
} CTR_index;

#define THIEF_EMPTY     ((struct _Worker*)0x0)
#define THIEF_TASK      ((struct _Worker*)0x1)
#define THIEF_COMPLETED ((struct _Worker*)0x2)

#define LACE_STOLEN   ((Worker*)0)
#define LACE_BUSY     ((Worker*)1)
#define LACE_NOWORK   ((Worker*)2)

#if LACE_PIE_TIMES
static void lace_time_event( WorkerP *w, int event )
{
    uint64_t now = gethrtime(),
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
 * Helper function when a Task stack overflow is detected.
 */
void lace_abort_stack_overflow(void) __attribute__((noreturn));

/**
 * Support for interrupting Lace workers
 */

typedef struct
{
    _Atomic(Task*) t;
    char pad[LINE_SIZE-sizeof(Task *)];
} lace_newframe_t;

extern lace_newframe_t lace_newframe;

/**
 * Interrupt the current worker and run a task in a new frame
 */
void lace_yield(void);

/**
 * Check if current tasks must be interrupted, and if so, interrupt.
 */
static inline void lace_check_yield(void) { if (__builtin_expect(atomic_load_explicit(&lace_newframe.t, memory_order_relaxed) != NULL, 0)) lace_yield(); }

/**
 * Make all tasks of the current worker shared.
 */
static inline void __attribute__((unused))
lace_make_all_shared(void)
{
    WorkerP* w = lace_get_worker();
    if (w->split != w->head) {
        w->split = w->head;
        w->_public->ts.ts.split = w->head - w->dq;
    }
}

/**
 * Helper function for _SYNC implementations
 */
int lace_sync(WorkerP *w, Task *head);


// Task macros for tasks of arity 0

#define TASK_0(RTYPE, NAME)                                                           \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union {  RTYPE res; } d;                                                            \
} TD_##NAME;                                                                          \
                                                                                      \
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
RTYPE NAME();                                                                         \
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
    t->d.res = NAME();                                                                \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN()                                                                   \
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME()                                                               \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER()                                                                \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUN()                                                                    \
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME();                                                                \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_task(&_t);                                                               \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC()                                                                   \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME();                                                            \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME();                                                                \
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
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
void NAME();                                                                          \
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
     NAME();                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN()                                                                   \
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME()                                                                \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER()                                                                \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUN()                                                                     \
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME();                                                                \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
                                                                                      \
    lace_run_task(&_t);                                                               \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC()                                                                    \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME();                                                            \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ;                                                                      \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME();                                                                \
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 1

#define TASK_1(RTYPE, NAME, ATYPE_1, ARG_1)                                           \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; } args; RTYPE res; } d;                            \
} TD_##NAME;                                                                          \
                                                                                      \
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
RTYPE NAME(ATYPE_1);                                                                  \
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
    t->d.res = NAME(t->d.args.arg_1);                                                 \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1)                                                      \
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1)                                                  \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1)                                                   \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUN(ATYPE_1 arg_1)                                                       \
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1);                                                           \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_task(&_t);                                                               \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC()                                                                   \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1);                                             \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1);                                                 \
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
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
void NAME(ATYPE_1);                                                                   \
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
     NAME(t->d.args.arg_1);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1)                                                      \
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1)                                                   \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1)                                                   \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUN(ATYPE_1 arg_1)                                                        \
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1);                                                           \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1;                                                         \
    lace_run_task(&_t);                                                               \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC()                                                                    \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1);                                             \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ;                                                                      \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1);                                                 \
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 2

#define TASK_2(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2)                           \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; } args; RTYPE res; } d;             \
} TD_##NAME;                                                                          \
                                                                                      \
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
RTYPE NAME(ATYPE_1, ATYPE_2);                                                         \
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
    t->d.res = NAME(t->d.args.arg_1, t->d.args.arg_2);                                \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2)                                       \
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2)                                   \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2)                                    \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2)                                        \
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2);                                                    \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_task(&_t);                                                               \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC()                                                                   \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2);                            \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2);                                \
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
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
void NAME(ATYPE_1, ATYPE_2);                                                          \
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
     NAME(t->d.args.arg_1, t->d.args.arg_2);                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2)                                       \
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2)                                    \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2)                                    \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2)                                         \
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2);                                                    \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2;                                \
    lace_run_task(&_t);                                                               \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC()                                                                    \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2);                            \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ;                                                                      \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2);                                \
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 3

#define TASK_3(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3)           \
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
RTYPE NAME(ATYPE_1, ATYPE_2, ATYPE_3);                                                \
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
    t->d.res = NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3);               \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                        \
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                    \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                     \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                         \
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3);                                             \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_task(&_t);                                                               \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC()                                                                   \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3);           \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3);               \
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
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
void NAME(ATYPE_1, ATYPE_2, ATYPE_3);                                                 \
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
     NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3);                         \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                        \
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                     \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                     \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3)                          \
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3);                                             \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3;       \
    lace_run_task(&_t);                                                               \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC()                                                                    \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3);           \
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ;                                                                      \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3);               \
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 4

#define TASK_4(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
RTYPE NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4);                                       \
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
    t->d.res = NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)         \
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)     \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)      \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)          \
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4);                                      \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_task(&_t);                                                               \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC()                                                                   \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4);\
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
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
void NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4);                                        \
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
     NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4);        \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)         \
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)      \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)      \
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4)           \
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4);                                      \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4;\
    lace_run_task(&_t);                                                               \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC()                                                                    \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ;                                                                      \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 5

#define TASK_5(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
RTYPE NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5);                              \
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
    t->d.res = NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5);                               \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_task(&_t);                                                               \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC()                                                                   \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5);\
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
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
void NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5);                               \
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
     NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5);                               \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5;\
    lace_run_task(&_t);                                                               \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC()                                                                    \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ;                                                                      \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 6

#define TASK_6(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
RTYPE NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6);                     \
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
    t->d.res = NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);                        \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_task(&_t);                                                               \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC()                                                                   \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6);\
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
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
void NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6);                      \
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
     NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);                        \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6;\
    lace_run_task(&_t);                                                               \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC()                                                                    \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ;                                                                      \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 7

#define TASK_7(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
RTYPE NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7);            \
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
    t->d.res = NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7);                 \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
    lace_run_task(&_t);                                                               \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC()                                                                   \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7);\
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
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
void NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7);             \
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
     NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7);                 \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7;\
    lace_run_task(&_t);                                                               \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC()                                                                    \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ;                                                                      \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 8

#define TASK_8(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
RTYPE NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8);   \
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
    t->d.res = NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8);          \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
    lace_run_task(&_t);                                                               \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC()                                                                   \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8);\
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
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
void NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8);    \
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
     NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8);          \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8;\
    lace_run_task(&_t);                                                               \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC()                                                                    \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ;                                                                      \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 9

#define TASK_9(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
RTYPE NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9);\
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
    t->d.res = NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9);   \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
    lace_run_task(&_t);                                                               \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC()                                                                   \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9);\
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
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
void NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9);\
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
     NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9);   \
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9;\
    lace_run_task(&_t);                                                               \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC()                                                                    \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ;                                                                      \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 10

#define TASK_10(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9, ATYPE_10, ARG_10)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; ATYPE_10 arg_10; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
RTYPE NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10);\
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
    t->d.res = NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10);\
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
    lace_run_task(&_t);                                                               \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC()                                                                   \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10);\
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
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
void NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10);\
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
     NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10);\
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10;\
    lace_run_task(&_t);                                                               \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC()                                                                    \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ;                                                                      \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 11

#define TASK_11(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9, ATYPE_10, ARG_10, ATYPE_11, ARG_11)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; ATYPE_10 arg_10; ATYPE_11 arg_11; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
RTYPE NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10, ATYPE_11);\
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
    t->d.res = NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, arg_11);\
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11;\
    lace_run_task(&_t);                                                               \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC()                                                                   \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11);\
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
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
void NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10, ATYPE_11);\
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
     NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, arg_11);\
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11;\
    lace_run_task(&_t);                                                               \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC()                                                                    \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ;                                                                      \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 12

#define TASK_12(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9, ATYPE_10, ARG_10, ATYPE_11, ARG_11, ATYPE_12, ARG_12)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; ATYPE_10 arg_10; ATYPE_11 arg_11; ATYPE_12 arg_12; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
RTYPE NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10, ATYPE_11, ATYPE_12);\
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
    t->d.res = NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, arg_11, arg_12);\
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12;\
    lace_run_task(&_t);                                                               \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC()                                                                   \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12);\
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
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
void NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10, ATYPE_11, ATYPE_12);\
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
     NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, arg_11, arg_12);\
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12;\
    lace_run_task(&_t);                                                               \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC()                                                                    \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ;                                                                      \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 13

#define TASK_13(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9, ATYPE_10, ARG_10, ATYPE_11, ARG_11, ATYPE_12, ARG_12, ATYPE_13, ARG_13)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; ATYPE_10 arg_10; ATYPE_11 arg_11; ATYPE_12 arg_12; ATYPE_13 arg_13; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
RTYPE NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10, ATYPE_11, ATYPE_12, ATYPE_13);\
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
    t->d.res = NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, arg_11, arg_12, arg_13);\
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13;\
    lace_run_task(&_t);                                                               \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC()                                                                   \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13);\
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
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
void NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10, ATYPE_11, ATYPE_12, ATYPE_13);\
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
     NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, arg_11, arg_12, arg_13);\
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13;\
    lace_run_task(&_t);                                                               \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC()                                                                    \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ;                                                                      \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


// Task macros for tasks of arity 14

#define TASK_14(RTYPE, NAME, ATYPE_1, ARG_1, ATYPE_2, ARG_2, ATYPE_3, ARG_3, ATYPE_4, ARG_4, ATYPE_5, ARG_5, ATYPE_6, ARG_6, ATYPE_7, ARG_7, ATYPE_8, ARG_8, ATYPE_9, ARG_9, ATYPE_10, ARG_10, ATYPE_11, ARG_11, ATYPE_12, ARG_12, ATYPE_13, ARG_13, ATYPE_14, ARG_14)\
                                                                                      \
typedef struct _TD_##NAME {                                                           \
  TASK_COMMON_FIELDS(_TD_##NAME)                                                      \
  union { struct {  ATYPE_1 arg_1; ATYPE_2 arg_2; ATYPE_3 arg_3; ATYPE_4 arg_4; ATYPE_5 arg_5; ATYPE_6 arg_6; ATYPE_7 arg_7; ATYPE_8 arg_8; ATYPE_9 arg_9; ATYPE_10 arg_10; ATYPE_11 arg_11; ATYPE_12 arg_12; ATYPE_13 arg_13; ATYPE_14 arg_14; } args; RTYPE res; } d;\
} TD_##NAME;                                                                          \
                                                                                      \
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
RTYPE NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10, ATYPE_11, ATYPE_12, ATYPE_13, ATYPE_14);\
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
    t->d.res = NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13, t->d.args.arg_14);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13, ATYPE_14 arg_14)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13, ATYPE_14 arg_14)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13; t->d.args.arg_14 = arg_14;\
    lace_run_newframe(&_t);                                                           \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13, ATYPE_14 arg_14)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13; t->d.args.arg_14 = arg_14;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13, ATYPE_14 arg_14)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, arg_11, arg_12, arg_13, arg_14);\
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13; t->d.args.arg_14 = arg_14;\
    lace_run_task(&_t);                                                               \
    return ((TD_##NAME *)t)->d.res;                                                   \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
RTYPE NAME##_SYNC()                                                                   \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13, t->d.args.arg_14);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ((TD_##NAME *)t)->d.res;                                               \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13, t->d.args.arg_14);\
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
/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */\
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];\
                                                                                      \
void NAME(ATYPE_1, ATYPE_2, ATYPE_3, ATYPE_4, ATYPE_5, ATYPE_6, ATYPE_7, ATYPE_8, ATYPE_9, ATYPE_10, ATYPE_11, ATYPE_12, ATYPE_13, ATYPE_14);\
                                                                                      \
static void NAME##_WRAP(TD_##NAME *t __attribute__((unused)))                         \
{                                                                                     \
     NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13, t->d.args.arg_14);\
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SPAWN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13, ATYPE_14 arg_14)\
{                                                                                     \
    PR_COUNTTASK(w);                                                                  \
                                                                                      \
    WorkerP *w = lace_get_worker();                                                   \
    Task *lace_head = w->head;                                                        \
    if (lace_head == w->end) lace_abort_stack_overflow();                             \
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
    Worker *wt = w->_public;                                                          \
    if (__builtin_expect(w->allstolen, 0)) {                                          \
        if (wt->movesplit) wt->movesplit = 0;                                         \
        head = lace_head - w->dq;                                                     \
        ts = (TailSplitNA){{head,head+1}};                                            \
        wt->ts.v = ts.v;                                                              \
        wt->allstolen = 0;                                                            \
        w->split = lace_head+1;                                                       \
        w->allstolen = 0;                                                             \
    } else if (__builtin_expect(wt->movesplit, 0)) {                                  \
        head = lace_head - w->dq;                                                     \
        split = w->split - w->dq;                                                     \
        newsplit = (split + head + 2)/2;                                              \
        wt->ts.ts.split = newsplit;                                                   \
        w->split = w->dq + newsplit;                                                  \
        wt->movesplit = 0;                                                            \
        PR_COUNTSPLITS(w, CTR_split_grow);                                            \
    }                                                                                 \
                                                                                      \
    w->head = lace_head+1;                                                            \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_NEWFRAME(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13, ATYPE_14 arg_14)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13; t->d.args.arg_14 = arg_14;\
    lace_run_newframe(&_t);                                                           \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_TOGETHER(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13, ATYPE_14 arg_14)\
{                                                                                     \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13; t->d.args.arg_14 = arg_14;\
    lace_run_together(&_t);                                                           \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_RUN(ATYPE_1 arg_1, ATYPE_2 arg_2, ATYPE_3 arg_3, ATYPE_4 arg_4, ATYPE_5 arg_5, ATYPE_6 arg_6, ATYPE_7 arg_7, ATYPE_8 arg_8, ATYPE_9 arg_9, ATYPE_10 arg_10, ATYPE_11 arg_11, ATYPE_12 arg_12, ATYPE_13 arg_13, ATYPE_14 arg_14)\
{                                                                                     \
    if (lace_is_worker()) {                                                           \
        return NAME(arg_1, arg_2, arg_3, arg_4, arg_5, arg_6, arg_7, arg_8, arg_9, arg_10, arg_11, arg_12, arg_13, arg_14);\
    }                                                                                 \
    Task _t;                                                                          \
    TD_##NAME *t = (TD_##NAME *)&_t;                                                  \
    t->f = &NAME##_WRAP;                                                              \
    atomic_store_explicit(&t->thief, THIEF_TASK, memory_order_relaxed);               \
     t->d.args.arg_1 = arg_1; t->d.args.arg_2 = arg_2; t->d.args.arg_3 = arg_3; t->d.args.arg_4 = arg_4; t->d.args.arg_5 = arg_5; t->d.args.arg_6 = arg_6; t->d.args.arg_7 = arg_7; t->d.args.arg_8 = arg_8; t->d.args.arg_9 = arg_9; t->d.args.arg_10 = arg_10; t->d.args.arg_11 = arg_11; t->d.args.arg_12 = arg_12; t->d.args.arg_13 = arg_13; t->d.args.arg_14 = arg_14;\
    lace_run_task(&_t);                                                               \
    return ;                                                                          \
}                                                                                     \
                                                                                      \
static inline __attribute__((unused))                                                 \
void NAME##_SYNC()                                                                    \
{                                                                                     \
    WorkerP* w = lace_get_worker();                                                   \
    Task* head = w->head - 1;                                                         \
    w->head = head;                                                                   \
                                                                                      \
    /* assert (__dq_head > 0); */  /* Commented out because we assume contract */     \
    TD_##NAME *t = (TD_##NAME *)head;                                                 \
                                                                                      \
    if (__builtin_expect(0 == w->_public->movesplit, 1)) {                            \
        if (__builtin_expect(w->split <= head, 1)) {                                  \
            atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);      \
            return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13, t->d.args.arg_14);\
        }                                                                             \
    }                                                                                 \
                                                                                      \
    if (lace_sync(w, head)) {                                                         \
        return ;                                                                      \
    } else {                                                                          \
        atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);          \
        return NAME(t->d.args.arg_1, t->d.args.arg_2, t->d.args.arg_3, t->d.args.arg_4, t->d.args.arg_5, t->d.args.arg_6, t->d.args.arg_7, t->d.args.arg_8, t->d.args.arg_9, t->d.args.arg_10, t->d.args.arg_11, t->d.args.arg_12, t->d.args.arg_13, t->d.args.arg_14);\
    }                                                                                 \
}                                                                                     \
                                                                                      \
                                                                                      \


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
