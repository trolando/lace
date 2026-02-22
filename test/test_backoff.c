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

VOID_TASK_1(sleeptask, long, us)
void sleeptask_CALL(lace_worker *lace, long us)
{
    lace_sleep_us(us);
}

TASK_1(long, pfib, int, n)
long pfib_CALL(lace_worker* worker, int n)
{
    if (n<2) return n;
    long m,k;
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

    lace_set_verbosity(1);
    lace_start(n_workers, 0, 0);
    double time0 = wctime();
    long res = pfib(35);
    printf("1 bla: %ld\n", res);
    double time1 = wctime();
    res = pfib(35);
    printf("2 bla: %ld\n", res);
    double time2 = wctime();
    res = pfib(35);
    printf("3 bla: %ld\n", res);
    double time3 = wctime();
    sleeptask(1000000);
    double time4 = wctime();
    res = pfib(35);
    printf("4 bla: %ld\n", res);
    double time5 = wctime();
    res = pfib(35);
    printf("5 bla: %ld\n", res);
    double time6 = wctime();
    res = pfib(35);
    printf("6 bla: %ld\n", res);
    double time7 = wctime();
    sleeptask(1000000);
    double time8 = wctime();
    res = pfib(35);
    printf("7 bla: %ld\n", res);
    double time9 = wctime();
    res = pfib(35);
    printf("8 bla: %ld\n", res);
    double time10 = wctime();
    res = pfib(35);
    printf("9 bla: %ld\n", res);
    double time11 = wctime();
    lace_stop();

    printf("Calculating pfib(35) -: %f\n", (time2-time1));
    printf("Calculating pfib(35) -: %f\n", (time3-time2));
    printf("Calculating pfib(35) +: %f\n", (time5-time4));
    printf("Calculating pfib(35) -: %f\n", (time6-time5));
    printf("Calculating pfib(35) -: %f\n", (time7-time6));
    printf("Calculating pfib(35) +: %f\n", (time9-time8));
    printf("Calculating pfib(35) -: %f\n", (time10-time9));
    printf("Calculating pfib(35) -: %f\n", (time11-time10));

    return 0;
}
