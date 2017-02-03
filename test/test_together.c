#include <stdio.h>
#include <stdlib.h>

#include <lace.h>

VOID_TASK_DECL_1(test_together, int);
VOID_TASK_DECL_1(test_newframe, int);

VOID_TASK_IMPL_1(test_together, int, depth)
{
    if (depth != 0) {
        SPAWN(test_together, depth-1);
        SPAWN(test_together, depth-1);
        SPAWN(test_together, depth-1);
        SPAWN(test_together, depth-1);
        NEWFRAME(test_newframe, depth-1);
        SYNC(test_together);
        SYNC(test_together);
        SYNC(test_together);
        SYNC(test_together);
    }
}

VOID_TASK_IMPL_1(test_newframe, int, depth)
{
    if (depth != 0) {
        SPAWN(test_newframe, depth-1);
        SPAWN(test_newframe, depth-1);
        SPAWN(test_newframe, depth-1);
        SPAWN(test_newframe, depth-1);
        TOGETHER(test_together, depth-1);
        SYNC(test_newframe);
        SYNC(test_newframe);
        SYNC(test_newframe);
        SYNC(test_newframe);
    }
}

VOID_TASK_1(_main, void*, arg)
{
    fprintf(stdout, "Testing TOGETHER and NEWFRAME with %zu workers...\n", lace_workers());

    for (int i=0; i<10; i++) {
        NEWFRAME(test_newframe, 5);
        TOGETHER(test_together, 5);
    }

    // We didn't use arg
    (void)arg;
}

int
main (int argc, char *argv[])
{
    int n_workers = 0; // automatically detect number of workers

    if (argc > 1) {
        n_workers = atoi(argv[1]);
    }

    // Initialize the Lace framework for <n_workers> workers.
    lace_init(n_workers, 0);

    // Spawn and start all worker pthreads; suspends current thread until done.
    lace_startup(0, TASK(_main), NULL);

    // The lace_startup command also exits Lace after _main is completed.

    return 0;
}
