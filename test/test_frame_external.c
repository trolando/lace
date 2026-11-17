/*
 * A NEWFRAME or TOGETHER is stop-the-world: no external task of another thread
 * may start inside it (Sylvan's garbage collection runs as a NEWFRAME while
 * other threads call into Lace). Thread A runs ROUNDS NEWFRAMEs and ROUNDS
 * TOGETHERs. Inside each, a frame task (the NEWFRAME's task, or the TOGETHER's
 * task on worker 0 while the other workers wait in the frame) asks thread B
 * for an external call, waits until B is about to submit it, and holds the
 * frame for FRAME_US more. The call must not start before the frame has ended,
 * and must be done within SERVE_LIMIT seconds after it (it is not left waiting
 * for a timeout). A watchdog ends the test after TIME_LIMIT seconds, lace_stop
 * included. At most as many workers as CPUs; skipped with fewer than 2.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

#include <lace.h>

#include "test_crash_handler.h"

#if LACE_MSVC
    #include <process.h>
#else
    #include <pthread.h>
#endif
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include <sched.h>
    #include <time.h>
    #include <unistd.h>
#endif

#define ROUNDS      20
#define FRAME_US    3000
#define SERVE_LIMIT 5.0
#define TIME_LIMIT  120.0

static double
now(void)
{
#if defined(_WIN32)
    LARGE_INTEGER t, freq;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&freq);
    return (double)t.QuadPart / (double)freq.QuadPart;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
#endif
}

static void
pause_ms(int ms)
{
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec t = { 0, (long)ms * 1000000L };
    nanosleep(&t, NULL);
#endif
}

static void
yield_cpu(void)
{
#if defined(_WIN32)
    SwitchToThread();
#else
    sched_yield();
#endif
}

static void
busy_us(int us)
{
    volatile unsigned long spin = 0;
    const double t0 = now();
    while (now() - t0 < (double)us * 1e-6) spin++;
}

static atomic_int in_frame = 0;   // a frame task holds the frame
static atomic_int violations[2];  // external calls that started while one did: in a TOGETHER, a NEWFRAME
static atomic_int requested = 0;  // the call that B is to submit
static atomic_int submitting = 0; // the call that B is about to submit
static atomic_int completed = 0;  // the last call of B that ran
static atomic_int b_ready = 0, b_stop = 0, finished = 0;
static int a_failed = 0;
static double longest_wait = 0; // from the end of a frame until its waiting call was done

/* In a frame: ask thread B for call number `call`, wait until B is about to
 * submit it, and hold the frame for FRAME_US while the call waits */
static void
frame_hold(int call)
{
    atomic_store(&in_frame, 1);
    atomic_store(&requested, call);
    while (atomic_load(&submitting) != call) yield_cpu();
    busy_us(FRAME_US);
    atomic_store(&in_frame, 0);
}

TASK(void, gc, int, call)
{
    frame_hold(call);
}

TASK(void, gc_start, int, call)
{
    NEWFRAME(gc, call);
}

/* On every worker; worker 0 holds the frame while the others wait in it */
TASK(void, together, int, call)
{
    if (__lace_worker->worker == 0) frame_hold(call);
}

TASK(void, together_start, int, call)
{
    TOGETHER(together, call);
}

TASK(void, external, int, call)
{
    if (atomic_load(&in_frame)) atomic_fetch_add(&violations[call % 2], 1);
    atomic_store(&completed, call);
}

static void
thread_a_run(void)
{
    while (!atomic_load(&b_ready)) yield_cpu();
    for (int call = 1; call <= 2 * ROUNDS; call++) {
        if (call % 2) RUN(gc_start, call);
        else RUN(together_start, call);
        const double t0 = now();
        while (atomic_load(&completed) != call) {
            if (now() - t0 > SERVE_LIMIT) {
                fprintf(stderr, "the external call that waited through %s %d was not done %.0f s after it\n",
                        call % 2 ? "NEWFRAME" : "TOGETHER", call, SERVE_LIMIT);
                a_failed = 1;
                atomic_store(&b_stop, 1);
                return;
            }
            yield_cpu();
        }
        if (now() - t0 > longest_wait) longest_wait = now() - t0;
    }
    atomic_store(&b_stop, 1);
}

static void
thread_b_run(void)
{
    atomic_store(&b_ready, 1);
    for (int call = 1; ; call++) {
        while (atomic_load(&requested) != call) {
            if (atomic_load(&b_stop)) return;
            yield_cpu();
        }
        atomic_store(&submitting, call);
        RUN(external, call);
    }
}

static void
watchdog_run(void)
{
    const double t0 = now();
    while (!atomic_load(&finished)) {
        if (now() - t0 > TIME_LIMIT) {
            fprintf(stderr, "not done after %.0f s (lace_stop included)\n", TIME_LIMIT);
            fflush(stderr);
            _Exit(1);
        }
        pause_ms(10);
    }
}

#if LACE_MSVC
static unsigned __stdcall thread_a(void *arg) { (void)arg; thread_a_run(); return 0; }
static unsigned __stdcall thread_b(void *arg) { (void)arg; thread_b_run(); return 0; }
static unsigned __stdcall watchdog(void *arg) { (void)arg; watchdog_run(); return 0; }
#else
static void *thread_a(void *arg) { (void)arg; thread_a_run(); return NULL; }
static void *thread_b(void *arg) { (void)arg; thread_b_run(); return NULL; }
static void *watchdog(void *arg) { (void)arg; watchdog_run(); return NULL; }
#endif

int
main(void)
{
    crash_handler_install();
    unsigned int workers = 4;
#if defined(_WIN32)
    const unsigned int ncpu = (unsigned int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
#else
    const long np = sysconf(_SC_NPROCESSORS_ONLN);
    const unsigned int ncpu = np > 0 ? (unsigned int)np : 1;
#endif
    if (ncpu < workers) workers = ncpu;
    if (workers < 2) {
        printf("fewer than 2 CPUs: nothing to test\n");
        return 0;
    }

    const double t0 = now();
#if LACE_MSVC
    HANDLE w = (HANDLE)_beginthreadex(NULL, 0, watchdog, NULL, 0, NULL);
    if (w == 0) {
        fprintf(stderr, "unable to create the threads\n");
        return 1;
    }
    lace_start(workers, 0);
    HANDLE b = (HANDLE)_beginthreadex(NULL, 0, thread_b, NULL, 0, NULL);
    HANDLE a = (HANDLE)_beginthreadex(NULL, 0, thread_a, NULL, 0, NULL);
    if (a == 0 || b == 0) {
        fprintf(stderr, "unable to create the threads\n");
        return 1;
    }
    WaitForSingleObject(a, INFINITE);
    WaitForSingleObject(b, INFINITE);
    CloseHandle(a);
    CloseHandle(b);
#else
    pthread_t w, a, b;
    if (pthread_create(&w, NULL, watchdog, NULL) != 0) {
        fprintf(stderr, "unable to create the threads\n");
        return 1;
    }
    lace_start(workers, 0);
    if (pthread_create(&b, NULL, thread_b, NULL) != 0 || pthread_create(&a, NULL, thread_a, NULL) != 0) {
        fprintf(stderr, "unable to create the threads\n");
        return 1;
    }
    pthread_join(a, NULL);
    pthread_join(b, NULL);
#endif
    lace_stop();
    atomic_store(&finished, 1);
#if LACE_MSVC
    WaitForSingleObject(w, INFINITE);
    CloseHandle(w);
#else
    pthread_join(w, NULL);
#endif

    const int v_newframe = atomic_load(&violations[1]), v_together = atomic_load(&violations[0]);
    printf("%d NEWFRAMEs and %d TOGETHERs, %u workers, %.2f s: external calls started during %d NEWFRAMEs and %d TOGETHERs; "
           "done at most %.2f ms after a frame\n", ROUNDS, ROUNDS, workers, now() - t0, v_newframe, v_together, longest_wait * 1e3);
    if (v_newframe != 0 || v_together != 0) {
        fprintf(stderr, "external tasks started during a frame\n");
        return 1;
    }
    return a_failed;
}
