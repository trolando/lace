/*
 * First, lace_stop while another thread's external task still runs. The
 * task starts a NEWFRAME and a TOGETHER after lace_stop was called:
 * lace_stop must wait for it, as the frames need every worker. Then, after
 * a new lace_start, lace_stop while another thread calls lace_release_memory
 * over and over: the releases must not keep lace_stop waiting, nor run on
 * after it. A watchdog ends the test if it hangs.
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

static atomic_int task_entered = 0, task_done = 0, released = 0, stopped = 0, finished = 0;

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
releaser_run(void)
{
    while (!atomic_load(&stopped)) {
        lace_release_memory();
        atomic_store(&released, 1);
    }
}

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
typedef HANDLE test_thread;
static unsigned __stdcall caller(void *arg) { (void)arg; caller_run(); return 0; }
static unsigned __stdcall releaser(void *arg) { (void)arg; releaser_run(); return 0; }
static unsigned __stdcall watchdog(void *arg) { (void)arg; watchdog_run(); return 0; }
static int
start_thread(test_thread *t, unsigned (__stdcall *f)(void *))
{
    *t = (HANDLE)_beginthreadex(NULL, 0, f, NULL, 0, NULL);
    return *t != 0;
}
static void
join_thread(test_thread t)
{
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}
#else
typedef pthread_t test_thread;
static void *caller(void *arg) { (void)arg; caller_run(); return NULL; }
static void *releaser(void *arg) { (void)arg; releaser_run(); return NULL; }
static void *watchdog(void *arg) { (void)arg; watchdog_run(); return NULL; }
static int
start_thread(test_thread *t, void *(*f)(void *))
{
    return pthread_create(t, NULL, f, NULL) == 0;
}
static void
join_thread(test_thread t)
{
    pthread_join(t, NULL);
}
#endif

int
main(void)
{
    crash_handler_install();
    test_thread w, c, r;
    if (!start_thread(&w, watchdog)) {
        fprintf(stderr, "unable to create the threads\n");
        return 1;
    }

    // lace_stop while the external task runs
    lace_start(4, 0);
    if (!start_thread(&c, caller)) {
        fprintf(stderr, "unable to create the threads\n");
        return 1;
    }
    while (!atomic_load(&task_entered)) pause_ms(1);
    lace_stop();
    join_thread(c);
    if (!atomic_load(&task_done)) {
        fprintf(stderr, "the external task did not complete\n");
        return 1;
    }
    printf("lace_stop waited for the external task: ok\n");

    // lace_stop while the releases go on
    lace_start(4, 0);
    if (!start_thread(&r, releaser)) {
        fprintf(stderr, "unable to create the threads\n");
        return 1;
    }
    while (!atomic_load(&released)) pause_ms(1);
    lace_stop();
    atomic_store(&stopped, 1);
    join_thread(r);
    printf("lace_stop while releasing: ok\n");

    atomic_store(&finished, 1);
    join_thread(w);
    return 0;
}
