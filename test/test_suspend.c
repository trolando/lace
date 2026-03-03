#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <lace.h>

#if LACE_MSVC
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

double wctime(void)
{
#if LACE_MSVC
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return (double)now.QuadPart / (double)freq.QuadPart;
#else
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (double)tv.tv_sec + 1e-9 * (double)tv.tv_nsec;
#endif
}

// simple workload

long sfib(int n)
{
    if (n<2) return n;
    return sfib(n-2) + sfib(n-1);
}

TASK_1(int, pfib, int, n)
int pfib_CALL(lace_worker* worker, int n)
{
    if (n<2) return n;
    int m,k;
    pfib_SPAWN(worker, n-1);
    k = pfib_CALL(worker, n-2);
    m = pfib_SYNC(worker);
    return m+k;
}

void
runtests(int n_workers)
{
    // first run sfib a few times
    for (int i=0; i<10; i++) sfib(35);

    double time = 0;

    for (int i=0; i<10; i++) {
        // Initialize the Lace framework for <n_workers> workers.
        lace_start(n_workers, 0, 0);
        lace_suspend();

        pfib(30);
        sfib(30);
        pfib(30);
        sfib(30);
        pfib(30);
        sfib(30);
        pfib(30);

        lace_resume();
        lace_stop();
    }
}

int
main (int argc, char *argv[])
{
    int n_workers = 0; // automatically detect number of workers

    if (argc > 1) {
        n_workers = atoi(argv[1]);
    }

    double part1 = 0;
    double part2 = 0;

    printf("Test 1: 40 iterations of 40 suspend-resume cycles\n");

    for (int i=0; i<40; i++) {
        lace_start(n_workers, 0, 0);
        double before = wctime();
        for (int j=0; j<40; j++) {
            lace_suspend();
            lace_resume();
        }
        double mid = wctime();
        lace_stop();
        double end = wctime();
        part1 += (mid-before);
        part2 += (end-mid);
    }

    printf("Time suspend + resume: %f sec\n", part1);
    printf("Time to lace_stop():   %f sec\n", part2);
    printf("Time per cycle:        %f sec\n", (part1+part2)/1600.0);

    printf("Test 2: 10 iterations of suspend/resume mixed with a small load\n");

    for (int i=0; i<=n_workers; i++) {
        runtests(i);
    }

    return 0;
}
