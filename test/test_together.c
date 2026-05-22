#include <stdio.h>
#include <stdlib.h>

#include <lace.h>

#include "test_crash_handler.h"

int counter;

TASK(void, test_count)
{
    counter++;
}

int* worker_counter;

TASK(void, test_count_perworker)
{
    worker_counter[LACE_WORKER_ID]++;
}

TASK(void, test_only_newframe, int, depth)
{
    if (depth > 0) {
        SPAWN(test_only_newframe, depth - 1);
        SPAWN(test_only_newframe, depth - 1);
        SPAWN(test_only_newframe, depth - 1);
        SYNC(test_only_newframe);
        SYNC(test_only_newframe);
        SYNC(test_only_newframe);
    }
    else {
        NEWFRAME(test_count);
    }
}

TASK(void, test_only_together, int, depth)
{
    if (depth > 0) {
        SPAWN(test_only_together, depth - 1);
        SPAWN(test_only_together, depth - 1);
        SPAWN(test_only_together, depth - 1);
        SYNC(test_only_together);
        SYNC(test_only_together);
        SYNC(test_only_together);
    }
    else {
        TOGETHER(test_count_perworker);
    }
}

TASK(void, test_together, int, depth)
{
    if (depth > 0) {
        SPAWN(test_together, depth - 1);
        SPAWN(test_together, depth - 1);
        SPAWN(test_together, depth - 1);
        SYNC(test_together);
        SYNC(test_together);
        SYNC(test_together);
    }
    else {
        CALL(test_only_newframe, 3);
    }
}

TASK(void, test_newframe, int, depth)
{
    if (depth > 0) {
        SPAWN(test_newframe, depth - 1);
        SPAWN(test_newframe, depth - 1);
        SPAWN(test_newframe, depth - 1);
        SYNC(test_newframe);
        SYNC(test_newframe);
        SYNC(test_newframe);
    }
    else {
        CALL(test_only_together, 3);
    }
}

TASK(void, test_report_id)
{
    printf("running from worker %d\n", LACE_WORKER_ID);
}

void
runtests(int n_workers)
{
    // Initialize the Lace framework for <n_workers> workers.
    lace_start(n_workers, 0);

    worker_counter = (int*)malloc(lace_workers() * sizeof(int));

    printf("Testing only newframe...\n");
    counter = 0;
    RUN(test_only_newframe, 6);
    printf("Counter reads %d (expecting 729)\n", counter);
    if (counter != 729) exit(1);

    printf("Testing only together...\n");
    for (unsigned int i = 0; i < lace_workers(); i++) worker_counter[i] = 0;
    RUN(test_only_together, 6);
    for (unsigned int i = 0; i < lace_workers(); i++) {
        printf("Counter %d reads %d (expecting 729)\n", i, worker_counter[i]);
        if (worker_counter[i] != 729) exit(1);
    }

    // Spawn and start all worker pthreads; suspends current thread until done.
    printf("Testing mixed newframe/together...\n");
    worker_counter = (int*)calloc(lace_workers(), sizeof(int));
    RUN(test_newframe, 5);
    RUN(test_together, 5);
    for (unsigned int i = 0; i < lace_workers(); i++) {
        printf("Counter %d reads %d (expecting 6561)\n", i, worker_counter[i]);
        if (worker_counter[i] != 6561) exit(1);
    }

    free(worker_counter);

    lace_stop();
}

int
main(int argc, char* argv[])
{
    crash_handler_install();

    int n_workers = 0; // automatically detect number of workers

    if (argc > 1) {
        n_workers = atoi(argv[1]);
    }

    lace_set_verbosity(0);

#define EXECUTIONS 3

    for (unsigned int i = 0; i < EXECUTIONS; i++) {
        printf("### RUNNING TEST %d OF %d ###\n", i + 1, EXECUTIONS);
        runtests(n_workers);
        printf("\n");
    }

    return 0;
}
