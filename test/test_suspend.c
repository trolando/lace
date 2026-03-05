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

    printf("Test 2: 1000 tasks, with/without suspension\n");

    {
        lace_start(n_workers, 0, 0);
        for (int i=0; i<100; i++) pfib(10);
        double before_1 = wctime();
        for (int i=0; i<1000; i++) pfib(10);
        double after_1 = wctime();
        lace_suspend();
        double before_2 = wctime();
        for (int i=0; i<1000; i++) pfib(10);
        double after_2 = wctime();
        lace_stop();
        printf("Time without suspend: %f sec\n", (after_1-before_1));
        printf("Time with suspend:    %f sec\n", (after_2-before_2));
    }

    return 0;
}
