#include <stdio.h>
#include <stdlib.h>

#include <lace.h>

#if LACE_MSVC
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
static inline void lace_test_yield(void) { SwitchToThread(); }
static inline void lace_test_sleep_ns(long ns)
{
    /* Windows Sleep is ms-granularity; for 2000ns this becomes ~0ms.
       That’s fine for a “give others a chance” hint. */
    (void)ns;
    Sleep(0);
}
#else
#include <sched.h>
#include <time.h>
static inline void lace_test_yield(void) { sched_yield(); }
static inline void lace_test_sleep_ns(long ns)
{
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = ns;
    nanosleep(&ts, NULL);
}
#endif

int *worker_counter;

TASK(void, test_barrier)
{
    int id = LACE_WORKER_ID;
    int count = lace_workers();

    for (int i=0; i<100; i++) {
        if (i % count == 0) {
            lace_test_yield();
            lace_sleep_us(1);
        }

        worker_counter[id]++;

        lace_barrier();

        for (int j=0; j<count; j++) {
            if (worker_counter[j] != i+1) {
                printf("Mismatch at iteration %d: worker_counter[%d] = %d (expected %d)\n", i, j, worker_counter[j], i+1);
                exit(1);
            }
        }

        lace_barrier();
    }
}

int
main (int argc, char *argv[])
{
    int n_workers = 0; // automatically detect number of workers

    if (argc > 1) {
        n_workers = atoi(argv[1]);
    }

    lace_set_verbosity(0);

#define EXECUTIONS 5

    for (int i=0; i<EXECUTIONS; i++) {
        printf("### RUNNING TEST %d OF %d ###\n", i+1, EXECUTIONS);

        lace_start(n_workers, 0);

        worker_counter = (int*)calloc(lace_workers(), sizeof(int));
        test_barrier_TOGETHER();
        free(worker_counter);

        lace_stop();

        printf("\n");
    }

    return 0;
}
