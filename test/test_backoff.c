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
    #include <sys/resource.h>
#endif

double wctime(void)
{
#if defined(_WIN32)
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

#if defined(_WIN32)
static double cpu_time_seconds(void)
{
    FILETIME create, exit, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user);
    /* FILETIME is in 100-nanosecond intervals */
    uint64_t k = ((uint64_t)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime;
    uint64_t u = ((uint64_t)user.dwHighDateTime << 32) | user.dwLowDateTime;
    return (k + u) / 1e7;
}
#else
static double cpu_time_seconds(void)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6
         + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
}
#endif

TASK(void, sleeptask, long, us)
{
    lace_sleep_us(us);
}

TASK(long, pfib, int, n)
{
    if (n<2) return n;
    long m,k;
    SPAWN(pfib, n-1);
    k = CALL(pfib, n-2);
    m = SYNC(pfib);
    return m+k;
}

int
main (int argc, char *argv[])
{
    crash_handler_install();

    int n_workers = 0; // automatically detect number of workers

    if (argc > 1) {
        n_workers = atoi(argv[1]);
    }

#if LACE_BACKOFF
    printf("Running test_backoff LACE_BACKOFF: ON\n\n");
#else
    printf("Running test_backoff LACE_BACKOFF: OFF\n\n");
#endif

    lace_set_verbosity(1);
    lace_start(n_workers, 0);

    for (int i=0; i<10; i++) RUN(pfib, 20); // some startup workload

    for (int zzz=0; zzz<=2; zzz++) {
        if (zzz==1) lace_sleep_us(1000000);
        double cpu_before_1 = cpu_time_seconds();
        double before_1 = wctime();
        double sleep_1 = 0;
        for (int i=0; i<200; i++) {
            RUN(pfib, 10);
            if (zzz==2) {
                double bef = wctime();
                lace_sleep_us(10000);
                double aft = wctime();
                sleep_1 += (aft-bef);
            }
        }
        double after_1 = wctime();
        double cpu_after_1 = cpu_time_seconds();

        if (zzz==0) printf("\nWITHOUT sleep\n");
        else if (zzz==1) printf("\nAFTER sleep\n");
        else if (zzz==2) printf("\nWITH sleep\n");
        printf("WC time:     %f sec\n", (after_1-before_1));
        printf("Sleep time:  %f sec\n", sleep_1);
        printf("Wake time:   %f sec\n", (after_1-before_1-sleep_1));
        printf("CPU time:    %f sec\n", (cpu_after_1-cpu_before_1));
    }

    lace_stop();

    return 0;
}
