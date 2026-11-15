/*
 * A NEWFRAME must not wait for workers that fell asleep inside it: when the
 * frame's task completes, Lace wakes them (1.11.0 did not; Sylvan's frequent
 * garbage collections then waited for the futex timeouts of sleeping workers,
 * up to 100 ms per collection).
 *
 * Each frame runs a 20 ms sequential task, long enough for the other workers
 * to fall asleep and back off; the test fails if the median overhead of a frame
 * (its time minus 20 ms) exceeds 2 ms. Measured on quiet machines with 4
 * workers: 1.11.0 5.6-6.8 ms (Linux and macOS), fixed 0.03-0.2 ms. At most as
 * many workers as CPUs (3 on a macOS CI runner): an oversubscribed run waits
 * for time slices instead. Skipped without LACE_BACKOFF (no worker sleeps).
 */
#include <stdio.h>
#include <stdlib.h>

#include <lace.h>

#include "test_crash_handler.h"

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include <time.h>
    #include <unistd.h>
#endif

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

static volatile unsigned long sink;

TASK(void, busy, int, ms)
{
    const double t0 = now();
    while (now() - t0 < (double)ms * 1e-3) sink++;
}

#define FRAMES 15
static double overhead_ms[FRAMES];

TASK(void, frames)
{
    for (int i = 0; i < FRAMES; i++) {
        const double t0 = now();
        NEWFRAME(busy, 20);
        overhead_ms[i] = (now() - t0) * 1e3 - 20.0;
    }
}

static int
cmp(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

int
main(void)
{
    crash_handler_install();
#if !LACE_BACKOFF
    printf("LACE_BACKOFF is off: no worker sleeps, nothing to test\n");
    return 0;
#endif
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
    lace_start(workers, 0);
    RUN(frames);
    lace_stop();
    qsort(overhead_ms, FRAMES, sizeof(double), cmp);
    printf("NEWFRAME of a 20 ms task, %u workers: overhead median %.2f ms, max %.2f ms\n", workers, overhead_ms[FRAMES / 2], overhead_ms[FRAMES - 1]);
    if (overhead_ms[FRAMES / 2] > 2.0) {
        fprintf(stderr, "a frame waited for sleeping workers (median overhead %.2f ms)\n", overhead_ms[FRAMES / 2]);
        return 1;
    }
    return 0;
}
