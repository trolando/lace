#if defined(__linux__)
#define _GNU_SOURCE /* sched_getaffinity, CPU_* macros; before any header */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lace.h>

#include "test_crash_handler.h"

/*
 * Worker pinning (lace_set_pinning, lace_set_pin_cpus, LACE_PIN). Every
 * worker records the CPUs it may run on, and each mode is checked against
 * the affinity mask of the process: one distinct CPU per worker for
 * LACE_PIN_CPU, the whole mask for LACE_PIN_OFF, the same set of distinct
 * cores for all workers with LACE_PIN_ALL, a non-empty part of the mask for
 * the group modes, the listed CPU for a pin list. Linux only; elsewhere this
 * checks that every mode starts and runs.
 */

#if defined(__linux__)
#include <sched.h>

static cpu_set_t process_mask;
static cpu_set_t *worker_masks;
#endif

TASK(void, record_affinity)
{
#if defined(__linux__)
    CPU_ZERO(&worker_masks[LACE_WORKER_ID]);
    (void)sched_getaffinity(0, sizeof(cpu_set_t), &worker_masks[LACE_WORKER_ID]);
#endif
}

TASK(int, fib, int, n)
{
    if (n < 2) return n;
    SPAWN(fib, n - 1);
    int a = CALL(fib, n - 2);
    int b = SYNC(fib);
    return a + b;
}

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s: ", name); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

/* Start n workers in the given mode (or with a pin list), record and check their CPUs */
static void
run_mode(const char *name, int mode, const int *list, unsigned int nlist, unsigned int n)
{
    if (list != NULL) lace_set_pin_cpus(list, nlist);
    else lace_set_pinning((lace_pin_mode)mode);
    lace_start(n, 0);
#if defined(__linux__)
    worker_masks = calloc(n, sizeof(cpu_set_t));
    TOGETHER(record_affinity);
#endif
    int r = RUN(fib, 20);
    lace_stop();
    lace_set_pin_cpus(NULL, 0);
    CHECK(r == 6765, "fib(20) = %d", r);
#if defined(__linux__)
    cpu_set_t seen;
    CPU_ZERO(&seen);
    for (unsigned int w = 0; w < n; w++) {
        cpu_set_t *m = &worker_masks[w], both;
        int count = CPU_COUNT(m);
        CPU_AND(&both, m, &process_mask);
        CHECK(count > 0, "worker %u has no CPUs", w);
        CHECK(CPU_EQUAL(&both, m), "worker %u may run outside the process mask", w);
        if (mode == LACE_PIN_OFF) CHECK(CPU_EQUAL(m, &process_mask), "worker %u is pinned", w);
        if (mode == LACE_PIN_CPU || list != NULL) {
            CHECK(count == 1, "worker %u may run on %d CPUs", w, count);
            if (list != NULL) CHECK(CPU_ISSET((size_t)list[w % nlist], m), "worker %u is not on CPU %d", w, list[w % nlist]);
            if (mode == LACE_PIN_CPU && list == NULL) {
                cpu_set_t dup;
                CPU_AND(&dup, m, &seen);
                CHECK(CPU_COUNT(&dup) == 0, "worker %u shares its CPU", w);
            }
        }
        if (mode == LACE_PIN_ALL) CHECK(CPU_EQUAL(m, &worker_masks[0]), "worker %u has another set than worker 0", w);
        CPU_OR(&seen, &seen, m);
    }
    free(worker_masks);
#endif
    if (failures == 0) printf("%s: ok\n", name);
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    crash_handler_install();
    lace_set_verbosity(0);

    unsigned int n = 2;
    int first_cpu = 0;
#if defined(__linux__)
    if (sched_getaffinity(0, sizeof(process_mask), &process_mask) != 0) {
        perror("sched_getaffinity");
        return 1;
    }
    int ncpus = CPU_COUNT(&process_mask);
    if (ncpus < 2) n = 1;
    for (unsigned c = 0; c < (unsigned)CPU_SETSIZE; c++) if (CPU_ISSET(c, &process_mask)) { first_cpu = (int)c; break; }
    printf("affinity: %d CPUs, %u workers\n", ncpus, n);
#endif

    run_mode("off", LACE_PIN_OFF, NULL, 0, n);
    run_mode("auto", LACE_PIN_AUTO, NULL, 0, n);
    run_mode("cpu", LACE_PIN_CPU, NULL, 0, n);
    run_mode("core", LACE_PIN_CORE, NULL, 0, n);
    run_mode("all", LACE_PIN_ALL, NULL, 0, n);
    run_mode("l2", LACE_PIN_L2, NULL, 0, n);
    run_mode("l3", LACE_PIN_L3, NULL, 0, n);
    run_mode("numa", LACE_PIN_NUMA, NULL, 0, n);
    int list[1] = { first_cpu };
    run_mode("list", -1, list, 1, n);

    if (failures != 0) {
        printf("test_pin: %d failures\n", failures);
        return 1;
    }
    printf("test_pin: ok\n");
    return 0;
}
