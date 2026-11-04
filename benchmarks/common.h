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


/* A hash of a benchmark's output, printed after the timed region as "Result
 * checksum = ...". The programs are deterministic, so it must be the same for
 * every worker count and every scheduler (FNV-1a over the bytes). */
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#define RESULT_HASH_INIT 1469598103934665603ULL
static inline uint64_t result_hash(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}
#define PRINT_RESULT_HASH(h) printf("Result checksum = %016" PRIx64 "\n", (uint64_t)(h))

#endif // COMMON_H
