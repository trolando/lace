#if defined(__linux__)
#define _GNU_SOURCE /* sched_setaffinity, CPU_COUNT; before any header */
#endif

#include <stdio.h>
#include <stdlib.h>

#include <lace.h>

#include "test_crash_handler.h"

/*
 * With n_workers 0, Lace starts one worker per CPU it may run on: under an
 * affinity mask of two CPUs (as taskset, cgroups and SLURM set), at most two
 * workers. This needs sched_getaffinity; the CMake check for it failed on
 * glibc (no _GNU_SOURCE), and Lace counted every online CPU. On Windows the
 * mask is that of the thread in its processor group. Linux, FreeBSD and
 * Windows; elsewhere this only checks that Lace starts.
 */

#if defined(__linux__) || defined(__FreeBSD__)
#include <sched.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

TASK(int, fib, int, n)
{
    if (n < 2) return n;
    SPAWN(fib, n - 1);
    int a = CALL(fib, n - 2);
    int b = SYNC(fib);
    return a + b;
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    crash_handler_install();

    unsigned int allowed = 0;
#if defined(__linux__) || defined(__FreeBSD__)
    cpu_set_t cs;
    CPU_ZERO(&cs);
    if (sched_getaffinity(0, sizeof(cs), &cs) != 0) {
        perror("sched_getaffinity");
        return 1;
    }
    /* keep the first two CPUs of the current mask */
    cpu_set_t two;
    CPU_ZERO(&two);
    for (int i = 0, kept = 0; i < CPU_SETSIZE && kept < 2; i++) {
        if (CPU_ISSET(i, &cs)) { CPU_SET(i, &two); kept++; }
    }
    if (sched_setaffinity(0, sizeof(two), &two) != 0) {
        perror("sched_setaffinity");
        return 1;
    }
    allowed = (unsigned int)CPU_COUNT(&two);
    printf("affinity: %u of %ld online CPUs\n", allowed, sysconf(_SC_NPROCESSORS_ONLN));
#elif defined(_WIN32)
    GROUP_AFFINITY ga;
    if (!GetThreadGroupAffinity(GetCurrentThread(), &ga)) {
        fprintf(stderr, "GetThreadGroupAffinity failed\n");
        return 1;
    }
    /* keep the first two CPUs of the thread's processor group */
    KAFFINITY two = 0;
    for (int i = 0; i < (int)(8 * sizeof(KAFFINITY)) && allowed < 2; i++) {
        if ((ga.Mask >> i) & 1) { two |= (KAFFINITY)1 << i; allowed++; }
    }
    ga.Mask = two;
    if (!SetThreadGroupAffinity(GetCurrentThread(), &ga, NULL)) {
        fprintf(stderr, "SetThreadGroupAffinity failed\n");
        return 1;
    }
    printf("affinity: %u CPUs of processor group %u\n", allowed, (unsigned int)ga.Group);
#endif

    lace_set_verbosity(0);
    lace_start(0, 0);
    unsigned int n = lace_workers();
    int r = RUN(fib, 20);
    lace_stop();

    if (r != 6765) {
        printf("fib(20) = %d, expected 6765\n", r);
        return 1;
    }
    if (allowed != 0 && n > allowed) {
        printf("%u workers for %u allowed CPUs\n", n, allowed);
        return 1;
    }
    printf("test_affinity: ok (%u workers)\n", n);
    return 0;
}
