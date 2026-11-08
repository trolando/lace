#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lace.h>

#include "test_crash_handler.h"

#if defined(_WIN32)
#include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <time.h>
static void sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#endif

/*
 * Scratch stays with the task that allocated it while its worker runs a
 * NEWFRAME. Every worker allocates and fills scratch in a TOGETHER task and
 * polls for a new frame; worker 0 then starts a NEWFRAME whose task sleeps, so
 * that the other workers go into deep idle in that frame's steal loop. The
 * steal loop used to reset the scratch arena to its base there, as if no task
 * owned scratch: under the interrupted tasks, whose scratch it decommitted and
 * whose next allocation overlapped what they still used (seen in mCRL2, with
 * Sylvan's garbage collection as the frame). With 8 workers, more than the
 * sentinels that stay awake reach deep idle.
 */

#define WORKERS 8
#define SIZE ((size_t)4 << 20) /* more than the arena keeps committed in idle */

static atomic_int done = 0;
static atomic_int failures = 0;

TASK(void, nap)
{
    sleep_ms(300);
}

TASK(void, holder)
{
    const unsigned int id = (unsigned int)LACE_WORKER_ID;
    void *mark = lace_scratch_mark(__lace_worker);
    unsigned char *p = (unsigned char *)lace_scratch_alloc(__lace_worker, SIZE);
    if (p == NULL) {
        printf("worker %u: no scratch\n", id);
        atomic_fetch_add_explicit(&failures, 1, memory_order_relaxed);
        return;
    }
    const unsigned char v = (unsigned char)(id + 1);
    memset(p, v, SIZE);

    if (id == 0) {
        sleep_ms(20); // the others are polling by now
        NEWFRAME(nap);
        atomic_store_explicit(&done, 1, memory_order_release);
    } else {
        while (!atomic_load_explicit(&done, memory_order_acquire)) {
            YIELD_NEWFRAME();
            sleep_ms(1);
        }
    }

    for (size_t i = 0; i < SIZE; i++) {
        if (p[i] != v) {
            printf("worker %u: scratch byte %zu is %d, not %d\n", id, i, p[i], v);
            atomic_fetch_add_explicit(&failures, 1, memory_order_relaxed);
            break;
        }
    }
    unsigned char *q = (unsigned char *)lace_scratch_alloc(__lace_worker, 64);
    if (q < p + SIZE) {
        printf("worker %u: a new allocation overlaps scratch still in use\n", id);
        atomic_fetch_add_explicit(&failures, 1, memory_order_relaxed);
    }
    lace_scratch_reset(__lace_worker, mark);
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    crash_handler_install();
    lace_set_verbosity(0);

    lace_start(WORKERS, 0);
    TOGETHER(holder);
    lace_stop();

    int f = atomic_load_explicit(&failures, memory_order_relaxed);
    if (f != 0) {
        printf("test_scratch_frame: %d failures\n", f);
        return 1;
    }
    printf("test_scratch_frame: ok\n");
    return 0;
}
