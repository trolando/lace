#! /bin/bash

# Copyright notice:
echo "/* 
 * Copyright 2013-2014 Formal Methods and Tools, University of Twente
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
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>

#ifndef __LACE_H__
#define __LACE_H__

/* Some flags */

#ifndef LACE_PIE_TIMES
#define LACE_PIE_TIMES 0
#endif

#ifndef LACE_COUNT_TASKS
#define LACE_COUNT_TASKS 0
#endif

#ifndef LACE_COUNT_STEALS
#define LACE_COUNT_STEALS 0
#endif

#ifndef LACE_COUNT_SPLITS
#define LACE_COUNT_SPLITS 0
#endif

#ifndef LACE_COUNT_EVENTS
#define LACE_COUNT_EVENTS (LACE_PIE_TIMES || LACE_COUNT_TASKS || LACE_COUNT_STEALS || LACE_COUNT_SPLITS)
#endif

/* Common code for atomic operations */

/* Processor cache line size */
#ifndef LINE_SIZE
#define LINE_SIZE 64  /* A common value for current processors */
#endif

/* Ensure a fresh memory read/write */
#ifndef atomic_read
#define atomic_read(v)      (*(volatile typeof(*v) *)(v))
#define atomic_write(v,a)   (*(volatile typeof(*v) *)(v) = (a))
#endif

/* Some fences */
#ifndef compiler_barrier
#define compiler_barrier() { asm volatile("" ::: "memory"); }
#endif

#ifndef mfence
#define mfence() { asm volatile("mfence" ::: "memory"); }
#endif

/* CAS operation */
#ifndef cas
#define cas(ptr, old, new) __sync_bool_compare_and_swap((ptr),(old),(new))
#endif

/* Compilerspecific branch prediction optimization */
#ifndef likely
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)
#endif

/* The size of a pointer, 8 bytes on a 64-bit architecture */
#define P_SZ (sizeof(void *))

#define PAD(x,b) ( ( (b) - ((x)%(b)) ) & ((b)-1) ) /* b must be power of 2 */
#define ROUND(x,b) ( (x) + PAD( (x), (b) ) )

#ifndef LACE_TASKSIZE
#define LACE_TASKSIZE ('$1'+1)*8
#endif

#if LACE_COUNT_EVENTS
typedef uint64_t hrtime_t;
#endif

#if LACE_PIE_TIMES
/* Some code for event counters and timers */

static inline hrtime_t gethrtime()
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

struct _Worker;
struct _Task;

#define THIEF_COMPLETED ((struct _Worker*)0x1)

#define TASK_COMMON_FIELDS(type)                               \
    void (*f)(struct _Worker *, struct _Task *, struct type *);  \
    struct _Worker *thief;

#define LACE_COMMON_FIELD_SIZE sizeof(struct { TASK_COMMON_FIELDS(_Task) })

typedef struct _Task {
    TASK_COMMON_FIELDS(_Task);
    char p1[PAD(LACE_COMMON_FIELD_SIZE, P_SZ)];
    char d[LACE_TASKSIZE];
    char p2[PAD(ROUND(LACE_COMMON_FIELD_SIZE, P_SZ) + LACE_TASKSIZE, LINE_SIZE)];
} Task;

typedef union __attribute__((packed)) {
    struct {
        uint32_t tail;
        uint32_t split;
    } ts;
    uint64_t v;
} TailSplit;

typedef struct _Worker {
    // Thief cache line
    Task *dq;
    TailSplit ts;
    uint8_t allstolen;

    char pad1[PAD(P_SZ+sizeof(TailSplit)+1, LINE_SIZE)];

    // Owner cache line
    Task *o_dq;        // same as dq
    Task *o_split;     // same as dq+ts.ts.split
    Task *o_end;       // dq+dq_size
    int16_t worker;     // what is my worker id?
    uint8_t o_allstolen; // my allstolen

    char pad2[PAD(3*P_SZ+2+1, LINE_SIZE)];

    uint8_t movesplit;

    char pad3[PAD(1, LINE_SIZE)];

#if LACE_COUNT_EVENTS
    uint64_t ctr[CTR_MAX]; // counters
    volatile hrtime_t time;
    volatile int level;
#endif
} Worker;

/**
 * Either use lace_init and lace_exit, or use lace_boot with a callback function.
 * lace_init will start w-1 workers, lace_boot will start w workers and run the callback function in a worker.
 * Use lace_boot is recommended because there is more control over the program stack allocation then.
 */
void lace_boot(int workers, size_t dq_size, size_t stack_size, void (*function)(void));
void lace_init(int workers, size_t dq_size, size_t stack_size);
void lace_exit();
int lace_inited();
size_t lace_workers();

extern void (*lace_cb_stealing)(void);
void lace_set_callback(void (*cb)(void));

Task *lace_get_head();
Worker *lace_get_worker();

#define LACE_STOLEN   0
#define LACE_BUSY     1
#define LACE_NOWORK   2

/*
 * The DISPATCH functions are a trick to allow using
 * the macros SPAWN, SYNC, CALL in code outside Lace code.
 * Note that using SPAWN and SYNC outside Lace code is probably
 * not something you really want.
 *
 * The __lace_worker, __lace_dq_head and __lace_in_task variables
 * are usually set to appropriate values in Lace functions.
 * If using SYNC, SPAWN and CALL outside Lace functions, the default
 * values below are used and the value of __lace_in_task triggers the
 * special behavior from outside Lace functions.
 *
 * The DISPATCH functions are always inlined and due to compiler
 * optimization they do not generate any overhead.
 */

__attribute__((unused))
static const Worker *__lace_worker = NULL;
__attribute__((unused))
static const Task *__lace_dq_head = NULL;
__attribute__((unused))
static const int __lace_in_task = 0;

#define SYNC(f)           ( __lace_dq_head--, SYNC_DISPATCH_##f((Worker *)__lace_worker, (Task *)__lace_dq_head, __lace_in_task))
#define SPAWN(f, ...)     ( SPAWN_DISPATCH_##f((Worker *)__lace_worker, (Task *)__lace_dq_head, __lace_in_task, ##__VA_ARGS__), __lace_dq_head++ )
#define CALL(f, ...)      ( CALL_DISPATCH_##f((Worker *)__lace_worker, (Task *)__lace_dq_head, __lace_in_task, ##__VA_ARGS__) )
#define LACE_WORKER_ID    ( (int16_t) (__lace_worker == NULL ? lace_get_worker()->worker : __lace_worker->worker) )

#if LACE_PIE_TIMES
static void lace_time_event( Worker *w, int event )
{
    hrtime_t now = gethrtime(),
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

static int __attribute__((noinline))
lace_steal(Worker *self, Task *__dq_head, Worker *victim)
{
    if (victim->allstolen) {
        lace_time_event(self, 7);
        return LACE_NOWORK;
    }

    register TailSplit ts = victim->ts;
    if (ts.ts.tail >= ts.ts.split) {
        if (victim->movesplit == 0) {
            victim->movesplit = 1;
            PR_COUNTSPLITS(self, CTR_split_req);
        }
        lace_time_event(self, 7);
        return LACE_NOWORK;
    }

    register TailSplit ts_new = ts;
    ts_new.ts.tail++;
    if (!cas(&victim->ts.v, ts.v, ts_new.v)) {
        lace_time_event(self, 7);
        return LACE_BUSY;
    }

    // Stolen
    Task *t = &victim->dq[ts.ts.tail];
    t->thief = self;
    lace_time_event(self, 1);
    t->f(self, __dq_head, t);
    lace_time_event(self, 2);
    t->thief = THIEF_COMPLETED;
    lace_time_event(self, 8);
    return LACE_STOLEN;
}

'
#
# Create macros for each arity
#

for(( r = 0; r <= $1; r++ )) do

# Extend various argument lists
if ((r)); then
  MACRO_ARGS="$MACRO_ARGS, ATYPE_$r, ARG_$r"
  DECL_ARGS="$DECL_ARGS, ATYPE_$r"
  TASK_FIELDS="$TASK_FIELDS ATYPE_$r arg_$r;"
  TASK_INIT="$TASK_INIT t->d.args.arg_$r = arg_$r;"
  TASK_GET_FROM_t="$TASK_GET_FROM_t, t->d.args.arg_$r"
  CALL_ARGS="$CALL_ARGS, arg_$r"
  FUN_ARGS="$FUN_ARGS, ATYPE_$r arg_$r"
  WORK_ARGS="$WORK_ARGS, ATYPE_$r ARG_$r"
fi

echo
echo "// Task macros for tasks of arity $r"
echo

# Create a void and a non-void version
for isvoid in 0 1; do
if (( isvoid==0 )); then
  DEF_MACRO="#define TASK_$r(RTYPE, NAME$MACRO_ARGS) \
             TASK_DECL_$r(RTYPE, NAME$DECL_ARGS) TASK_IMPL_$r(RTYPE, NAME$MACRO_ARGS)"
  DECL_MACRO="#define TASK_DECL_$r(RTYPE, NAME$DECL_ARGS)"
  IMPL_MACRO="#define TASK_IMPL_$r(RTYPE, NAME$MACRO_ARGS)"
  RTYPE="RTYPE"
  RES_FIELD="$RTYPE res;"
  SAVE_RVAL="t->d.res ="
  RETURN_RES="((TD_##NAME *)t)->d.res"
else
  DEF_MACRO="#define VOID_TASK_$r(NAME$MACRO_ARGS) \
             VOID_TASK_DECL_$r(NAME$DECL_ARGS) VOID_TASK_IMPL_$r(NAME$MACRO_ARGS)"
  DECL_MACRO="#define VOID_TASK_DECL_$r(NAME$DECL_ARGS)"
  IMPL_MACRO="#define VOID_TASK_IMPL_$r(NAME$MACRO_ARGS)"
  RTYPE="void"
  RES_FIELD=""
  SAVE_RVAL=""
  RETURN_RES=""
fi

# Write down the macro for the task declaration
(\
echo "$DECL_MACRO

typedef struct _TD_##NAME {
  TASK_COMMON_FIELDS(_TD_##NAME)
  union {
    struct { $TASK_FIELDS } args;
    $RES_FIELD
  } d;
} TD_##NAME;

/* If this line generates an error, please manually set the define LACE_TASKSIZE to a higher value */
typedef char assertion_failed_task_descriptor_out_of_bounds_##NAME[(sizeof(TD_##NAME)<=sizeof(Task)) ? 0 : -1];

void NAME##_WRAP(Worker *, Task *, TD_##NAME *);
$RTYPE NAME##_CALL(Worker *, Task * $FUN_ARGS);
static inline $RTYPE NAME##_SYNC_FAST(Worker *, Task *);
static $RTYPE NAME##_SYNC_SLOW(Worker *, Task *);

static inline
void NAME##_SPAWN(Worker *w, Task *__dq_head $FUN_ARGS)
{
    PR_COUNTTASK(w);

    TD_##NAME *t;
    TailSplit ts;
    uint32_t head, split, newsplit;

    /* assert(__dq_head < w->o_end); */ /* Assuming to be true */

    t = (TD_##NAME *)__dq_head;
    t->f = &NAME##_WRAP;
    t->thief = 0;
    $TASK_INIT
    compiler_barrier();

    if (unlikely(w->o_allstolen)) {
        if (w->movesplit) w->movesplit = 0;
        head = __dq_head - w->o_dq;
        ts = (TailSplit){{head,head+1}};
        w->ts.v = ts.v;
        compiler_barrier();
        w->allstolen = 0;
        w->o_split = __dq_head+1;
        w->o_allstolen = 0;
    } else if (unlikely(w->movesplit)) {
        head = __dq_head - w->o_dq;
        split = w->o_split - w->o_dq;
        newsplit = (split + head + 2)/2;
        w->ts.ts.split = newsplit;
        w->o_split = w->o_dq + newsplit;
        w->movesplit = 0;
        PR_COUNTSPLITS(w, CTR_split_grow);
    }
}

static int
NAME##_movesplit(Worker *w, Task *__dq_head)
{
    uint32_t tail = w->ts.ts.tail;
    uint32_t head = __dq_head - w->o_dq;
    uint32_t newsplit = (head+tail+1)/2;
    uint32_t oldsplit = w->ts.ts.split;
    
    if (newsplit != oldsplit) {
        w->ts.ts.split = newsplit;
        if (likely(newsplit < oldsplit)) {
            mfence();
            tail = atomic_read(&(w->ts.ts.tail));
            if (tail > newsplit) {
                newsplit = (head+tail+1)/2;
                /* head = head-1 therefore instead of t!=h we do t<=h */
                if (tail <= head) w->ts.ts.split = newsplit;
            }
            PR_COUNTSPLITS(w, CTR_split_shrink);
        } else {
            PR_COUNTSPLITS(w, CTR_split_grow);
        }
        w->o_split = w->o_dq+newsplit;
    }
    /* head = head-1 therefore instead of t==h we to t>h */
    if (tail > head) {
        w->allstolen = w->o_allstolen = 1;
        return 1;
    }
    return 0;
}

static inline void
NAME##_leapfrog(Worker *w, Task *__dq_head)
{
    lace_time_event(w, 3);
    TD_##NAME *t = (TD_##NAME *)__dq_head;
    Worker *thief = t->thief;
    if (thief != THIEF_COMPLETED) {
        while (thief == 0) thief = atomic_read(&(t->thief));

        /* Now leapfrog */
        while (thief != THIEF_COMPLETED) {
            PR_COUNTSTEALS(w, CTR_leap_tries);
            switch (lace_steal(w, __dq_head+1, thief)) {
            case LACE_NOWORK:
                lace_cb_stealing();
                break;
            case LACE_STOLEN:
                PR_COUNTSTEALS(w, CTR_leaps);
                break;
            case LACE_BUSY:
                PR_COUNTSTEALS(w, CTR_leap_busy);
                break;
            default:
                break;
            }
            thief = atomic_read(&(t->thief));
        }
        w->allstolen = 1;
        w->o_allstolen = 1;
    }

    t->f = 0;
    lace_time_event(w, 4);
}

static __attribute__((noinline))
$RTYPE NAME##_SYNC_SLOW(Worker *w, Task *__dq_head)
{
    TD_##NAME *t;

    if (w->o_allstolen) goto lace_allstolen_##NAME;
 
    if (unlikely(w->o_split > __dq_head)) {
        if (unlikely(NAME##_movesplit(w, __dq_head))) {
            goto lace_allstolen_##NAME;
        }
    }

    if (unlikely(w->movesplit)) {
        Task *t = w->o_split;
        size_t diff = __dq_head - t;
        diff = (diff + 1) / 2;
        w->o_split = t + diff;
        w->ts.ts.split += diff;
        w->movesplit = 0;
        PR_COUNTSPLITS(w, CTR_split_grow);
    }

    t = (TD_##NAME *)__dq_head;
    t->f = 0;
    return NAME##_CALL(w, __dq_head $TASK_GET_FROM_t);

lace_allstolen_##NAME:
    NAME##_leapfrog(w, __dq_head);
    t = (TD_##NAME *)__dq_head;
    return $RETURN_RES;
}

static inline
$RTYPE NAME##_SYNC_FAST(Worker *w, Task *__dq_head)
{
    TD_##NAME *t;

    /* assert (head > 0); */  /* Commented out because we assume contract */
    if (likely(0 == w->movesplit && w->o_split <= __dq_head)) {
        t = (TD_##NAME *)__dq_head;
        t->f = 0;
        return NAME##_CALL(w, __dq_head $TASK_GET_FROM_t);
    }

    return NAME##_SYNC_SLOW(w, __dq_head);
}

static inline __attribute__((always_inline))
void SPAWN_DISPATCH_##NAME(Worker *w, Task *__dq_head, int __intask $FUN_ARGS)
{
    if (__intask) return NAME##_SPAWN(w, __dq_head $CALL_ARGS);
    else return NAME##_SPAWN(lace_get_worker(), lace_get_head() $CALL_ARGS);
}

static inline __attribute__((always_inline))
$RTYPE SYNC_DISPATCH_##NAME(Worker *w, Task *__dq_head, int __intask)
{
    if (__intask) return NAME##_SYNC_FAST(w, __dq_head);
    else return NAME##_SYNC_FAST(lace_get_worker(), lace_get_head());
}

static inline __attribute__((always_inline))
$RTYPE CALL_DISPATCH_##NAME(Worker *w, Task *__dq_head, int __intask $FUN_ARGS)
{
    if (__intask) return NAME##_CALL(w, __dq_head $CALL_ARGS);
    else return NAME##_CALL(lace_get_worker(), lace_get_head() $CALL_ARGS);
}


"\
) | awk '{printf "%-86s\\\n", $0 }'

echo " "

(\
echo "$IMPL_MACRO
void NAME##_WRAP(Worker *w, Task *__dq_head, TD_##NAME *t)
{
    $SAVE_RVAL NAME##_CALL(w, __dq_head $TASK_GET_FROM_t);
}

static inline __attribute__((always_inline))
$RTYPE NAME##_WORK(Worker *__lace_worker, Task *__lace_dq_head, int __lace_in_task $DECL_ARGS);

/* NAME##_WORK is inlined in NAME##_CALL and the parameter __lace_in_task will disappear */
$RTYPE NAME##_CALL(Worker *w, Task *__dq_head $FUN_ARGS)
{
    return NAME##_WORK(w, __dq_head, 1 $CALL_ARGS);
}

static inline __attribute__((always_inline))
$RTYPE NAME##_WORK(Worker *__lace_worker, Task *__lace_dq_head, int __lace_in_task $WORK_ARGS)" \
) | awk '{printf "%-86s\\\n", $0 }'

echo " "

echo $DEF_MACRO

echo ""

done

done

echo "#endif"
