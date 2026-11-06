#include <stdio.h>
#include <stdlib.h>

#include <lace.h>

#include "test_crash_handler.h"

/*
 * Mixes SYNC and DROP in LIFO order on a tree with stealing, so DROP takes
 * both its inline fast path (task still private) and lace_drop_slow (task in
 * the shared region, or stolen). Each node spawns four children, syncs two
 * and drops two; the synced results give 3^depth.
 *
 * A dropped child is either cancelled or run in full by a thief before DROP
 * returns. Dropped children add 1 to their parent's counter when they start
 * and 1 when they finish, so after the DROPs the counter must be even and
 * must not change any more.
 */

TASK(long, tree, int, depth, atomic_int*, sink)
{
    if (sink != NULL) atomic_fetch_add_explicit(sink, 1, memory_order_relaxed);

    long r = 1;
    if (depth > 0) {
        atomic_int dropped = 0;
        SPAWN(tree, depth-1, NULL);       // synced
        SPAWN(tree, depth-1, &dropped);   // dropped
        SPAWN(tree, depth-1, NULL);       // synced
        SPAWN(tree, depth-1, &dropped);   // dropped
        r = CALL(tree, depth-1, NULL);
        DROP();
        r += SYNC(tree);
        DROP();
        r += SYNC(tree);

        int seen = atomic_load_explicit(&dropped, memory_order_relaxed);
        if (seen % 2 != 0 || seen > 4) {
            printf("dropped tasks still running after DROP (counter %d)\n", seen);
            exit(1);
        }
        for (int i = 0; i < 100; i++) {
            if (atomic_load_explicit(&dropped, memory_order_relaxed) != seen) {
                printf("dropped task ran after DROP returned\n");
                exit(1);
            }
        }
    }

    if (sink != NULL) atomic_fetch_add_explicit(sink, 1, memory_order_relaxed);
    return r;
}

int
main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);
    crash_handler_install();

    int n_workers = 0; // automatically detect number of workers
    if (argc > 1) n_workers = atoi(argv[1]);

    lace_set_verbosity(0);
    lace_start(n_workers, 0);

    const int depth = 12;
    long expected = 1;
    for (int i = 0; i < depth; i++) expected *= 3;

    for (int i = 0; i < 20; i++) {
        long r = RUN(tree, depth, NULL);
        if (r != expected) {
            printf("tree(%d) = %ld, expected %ld\n", depth, r, expected);
            exit(1);
        }
    }

    lace_stop();
    printf("test_drop: ok\n");
    return 0;
}
