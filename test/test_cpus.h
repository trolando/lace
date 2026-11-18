#ifndef TEST_CPUS_H
#define TEST_CPUS_H

/*
 * The number of CPUs the calling thread may run on, as Lace counts them for
 * its default number of workers. On Linux, define _GNU_SOURCE before any
 * header.
 */

#if defined(_WIN32)
#include <windows.h>
static unsigned int
test_allowed_cpus(void)
{
    GROUP_AFFINITY ga;
    if (!GetThreadGroupAffinity(GetCurrentThread(), &ga)) return 1;
    unsigned int n = 0;
    for (KAFFINITY m = ga.Mask; m != 0; m &= m - 1) n++;
    return n > 0 ? n : 1;
}
#elif defined(__linux__) || defined(__FreeBSD__)
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#if defined(__FreeBSD__)
#include <pthread_np.h>
#endif
static unsigned int
test_allowed_cpus(void)
{
    cpu_set_t cs;
    CPU_ZERO(&cs);
    if (pthread_getaffinity_np(pthread_self(), sizeof(cs), &cs) == 0) return (unsigned int)CPU_COUNT(&cs);
    const long np = sysconf(_SC_NPROCESSORS_ONLN);
    return np > 0 ? (unsigned int)np : 1;
}
#else
#include <unistd.h>
static unsigned int
test_allowed_cpus(void)
{
    const long np = sysconf(_SC_NPROCESSORS_ONLN);
    return np > 0 ? (unsigned int)np : 1;
}
#endif

#endif
