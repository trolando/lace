#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE // dsyrk.h pins its initialization threads
#endif
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <lace.h>
#include "common.h"
#include "dsyrk.h"

/*
 * Tiled DSYRK (dsyrk.h) on Lace. By default one task per tile of the lower
 * triangle of C, which applies all k/b updates in order: independent tasks,
 * found by halving the range of tiles. With -s, one parallel step per tile
 * column of A, each applying that column's update to every tile, and a join
 * after each step, as a fork-join port of a dataflow code without its
 * dependencies would do. With -p, a task makes its spawned tasks stealable
 * (LACE_MAKE_ALL_SHARED) before it runs a tile: they stay private until the
 * owner's next SPAWN or SYNC otherwise, which with few long tiles leaves
 * workers without work.
 */

static int share = 0;

TASK(void, tiles, int, lo, int, hi, int, step)
{
    if (hi - lo == 1) {
        if (share) LACE_MAKE_ALL_SHARED();
        if (step < 0) dsyrk_tile_all(lo);
        else dsyrk_tile(tile_i[lo], tile_j[lo], step);
        return;
    }
    int mid = lo + (hi - lo) / 2;
    SPAWN(tiles, mid, hi, step);
    CALL(tiles, lo, mid, step);
    SYNC(tiles);
}

static void
usage(char *s)
{
    fprintf(stderr, "Usage: %s [-w <workers>] [-q <dqsize>] [-b <tile>] [-k <k>] [-s] [-p] [<n>]\n", s);
}

int
main(int argc, char **argv)
{
    int workers = 0, dqsize = 100000, steps = 0;
    int c;
    while ((c = getopt(argc, argv, "w:q:b:k:sph")) != -1) {
        switch (c) {
        case 'w': workers = dsyrk_arg("the number of workers", optarg, 0, 65535); break;
        case 'q': dqsize = dsyrk_arg("the deque size", optarg, 0, INT_MAX); break;
        case 'b': b = dsyrk_arg("the tile size", optarg, 1, DSYRK_DIM_MAX); break;
        case 'k': k = dsyrk_arg("k", optarg, 1, DSYRK_DIM_MAX); break;
        case 's': steps = 1; break;
        case 'p': share = 1; break;
        case 'h': usage(argv[0]); return 0;
        default: usage(argv[0]); return 1;
        }
    }
    if (optind + 1 == argc) n = dsyrk_arg("n", argv[optind], 1, DSYRK_DIM_MAX);
    else if (optind != argc) { usage(argv[0]); return 1; }
    if (dsyrk_setup() != 0) return 1;

    lace_start(workers, dqsize);
    printf("Running dsyrk n=%d k=%d b=%d (%d tiles%s) with %u worker(s)...\n", n, k, b, ntiles,
           steps ? ", in steps" : "", lace_workers());

    double t1 = wctime();
    if (steps) for (int l = 0; l < kt; l++) RUN(tiles, 0, ntiles, l);
    else RUN(tiles, 0, ntiles, -1);
    double t2 = wctime();

    PRINT_RESULT_HASH(result_hash(RESULT_HASH_INIT, C, sizeof(double) * (size_t)n * (size_t)n));
    printf("Time: %f\n", t2 - t1);
    printf("GFLOPS: %.1f\n", dsyrk_flops() / (t2 - t1) * 1e-9);

    lace_stop();
    dsyrk_free();
    return 0;
}
