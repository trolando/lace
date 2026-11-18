/*
 * lace_stop while another thread's external task still runs. The task starts
 * a NEWFRAME and a TOGETHER after lace_stop was called: lace_stop must wait
 * for it, as the frames need every worker. A watchdog ends the test if it
 * hangs.
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
    #include <time.h>
    #include <unistd.h>
#endif

#define TIME_LIMIT_MS 30000

static atomic_int task_entered = 0, task_done = 0, finished = 0;

static void
pause_ms(int ms)
{
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}

VOID_TASK_0(nothing)
{
}

VOID_TASK_0(slow_then_frames)
{
    atomic_store(&task_entered, 1);
    pause_ms(100); // lace_stop is called meanwhile
    NEWFRAME(nothing);
    TOGETHER(nothing);
    atomic_store(&task_done, 1);
}

static void caller_run(void) { RUN(slow_then_frames); }

static void
watchdog_run(void)
{
    for (int ms = 0; !atomic_load(&finished); ms += 10) {
        if (ms > TIME_LIMIT_MS) {
            fprintf(stderr, "lace_stop did not return within %d ms\n", TIME_LIMIT_MS);
            fflush(stderr);
            _Exit(1);
        }
        pause_ms(10);
    }
}

#if LACE_MSVC
static unsigned __stdcall caller(void *arg) { (void)arg; caller_run(); return 0; }
static unsigned __stdcall watchdog(void *arg) { (void)arg; watchdog_run(); return 0; }
#else
static void *caller(void *arg) { (void)arg; caller_run(); return NULL; }
static void *watchdog(void *arg) { (void)arg; watchdog_run(); return NULL; }
#endif

int
main(void)
{
    crash_handler_install();
    lace_start(4, 0);

#if LACE_MSVC
    HANDLE w = (HANDLE)_beginthreadex(NULL, 0, watchdog, NULL, 0, NULL);
    HANDLE c = (HANDLE)_beginthreadex(NULL, 0, caller, NULL, 0, NULL);
    if (w == 0 || c == 0) {
        fprintf(stderr, "unable to create the threads\n");
        return 1;
    }
#else
    pthread_t w, c;
    if (pthread_create(&w, NULL, watchdog, NULL) != 0 || pthread_create(&c, NULL, caller, NULL) != 0) {
        fprintf(stderr, "unable to create the threads\n");
        return 1;
    }
#endif

    while (!atomic_load(&task_entered)) pause_ms(1); // the external task runs now
    lace_stop();

#if LACE_MSVC
    WaitForSingleObject(c, INFINITE);
    CloseHandle(c);
#else
    pthread_join(c, NULL);
#endif
    atomic_store(&finished, 1);
#if LACE_MSVC
    WaitForSingleObject(w, INFINITE);
    CloseHandle(w);
#else
    pthread_join(w, NULL);
#endif

    if (!atomic_load(&task_done)) {
        fprintf(stderr, "the external task did not complete\n");
        return 1;
    }
    printf("lace_stop waited for the external task: ok\n");
    return 0;
}
