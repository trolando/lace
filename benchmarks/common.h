#ifndef COMMON_H
#define COMMON_H

#if defined(_MSC_VER) && !defined(__clang__)
#include "getopt_win.h"
#else
#include <getopt.h>
#endif

#ifdef _WIN32
#include <windows.h>

static inline double wctime(void)
{
    static LARGE_INTEGER frequency;
    static int initialized = 0;
    LARGE_INTEGER counter;

    if (!initialized) {
        QueryPerformanceFrequency(&frequency);
        initialized = 1;
    }

    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
}

#else
#include <time.h>

static inline double wctime(void)
{
    struct timespec tv;
#if defined(CLOCK_MONOTONIC_RAW)
    clock_gettime(CLOCK_MONOTONIC_RAW, &tv);
#else
    clock_gettime(CLOCK_MONOTONIC, &tv);
#endif
    return (tv.tv_sec + 1e-9 * tv.tv_nsec);
}
#endif

#endif // COMMON_H
