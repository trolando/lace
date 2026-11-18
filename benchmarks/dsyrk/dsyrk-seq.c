#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE // dsyrk.h pins its initialization threads
#endif
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "dsyrk.h"

/* Tiled DSYRK (dsyrk.h), serially: the tiles in order, as dsyrk-lace without tasks */

static void
usage(char *s)
{
    fprintf(stderr, "Usage: %s [-b <tile>] [-k <k>] [-s] [<n>]\n", s);
}

int
main(int argc, char **argv)
{
    int steps = 0;
    int c;
    while ((c = getopt(argc, argv, "b:k:sh")) != -1) {
        switch (c) {
        case 'b': b = dsyrk_arg("the tile size", optarg, 1, DSYRK_DIM_MAX); break;
        case 'k': k = dsyrk_arg("k", optarg, 1, DSYRK_DIM_MAX); break;
        case 's': steps = 1; break;
        case 'h': usage(argv[0]); return 0;
        default: usage(argv[0]); return 1;
        }
    }
    if (optind + 1 == argc) n = dsyrk_arg("n", argv[optind], 1, DSYRK_DIM_MAX);
    else if (optind != argc) { usage(argv[0]); return 1; }
    if (dsyrk_setup() != 0) return 1;

    printf("Running dsyrk n=%d k=%d b=%d (%d tiles%s), serially...\n", n, k, b, ntiles, steps ? ", in steps" : "");

    double t1 = wctime();
    if (steps) {
        for (int l = 0; l < kt; l++) for (int t = 0; t < ntiles; t++) dsyrk_tile(tile_i[t], tile_j[t], l);
    } else {
        for (int t = 0; t < ntiles; t++) dsyrk_tile_all(t);
    }
    double t2 = wctime();

    PRINT_RESULT_HASH(result_hash(RESULT_HASH_INIT, C, sizeof(double) * (size_t)n * (size_t)n));
    printf("Time: %f\n", t2 - t1);
    printf("GFLOPS: %.1f\n", dsyrk_flops() / (t2 - t1) * 1e-9);

    dsyrk_free();
    return 0;
}
