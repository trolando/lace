#include <stdlib.h>
#include <stdio.h>

#include <lace.h>
#include <common.h>

/**
 * N Queens problem
 * Given already placed queens in array a, and we want n queens, place a queen on row d at position i
 */
TASK(long, nqueens, const int*, a, int, n, int, d, int, i)
{
    // copy queens from a to new array aa and check if ok
#if defined(_MSC_VER) && !defined(__clang__)
    int* aa = (int*)_alloca((d + 2) * sizeof(*aa));
#else
    int aa[d + 2]; // allocate one more to avoid UB
#endif

    for (int j = 0; j < d; ++j) {
        aa[j] = a[j];

        int diff = a[j] - i;
        int dist = d - j;

        if (diff == 0 || dist == diff || dist + diff == 0) return 0;
    }

    // it is ok, place the queen
    if (d >= 0) aa[d] = i;

    // check if we reached the target
    if (++d == n) return 1;

    // if not reached, place the next queen recursively
    for (int k = 0; k < n; k++) {
        SPAWN(nqueens, aa, n, d, k);
    }

    // and return the sum of the recursive counts
    long sum = 0;
    for (int k = 0; k < n; k++) {
        sum += SYNC(nqueens);
    }
    return sum;
}

static void usage(char* s)
{
    fprintf(stderr, "%s -w <workers> [-q dqsize] <n>\n", s);
}

int main(int argc, char* argv[])
{
    int n = 14;
    int workers = 1;
    int dqsize = 100000;

    int c;
    while ((c = getopt(argc, argv, "w:q:h")) != -1) {
        switch (c) {
        case 'w':
            workers = atoi(optarg);
            break;
        case 'q':
            dqsize = atoi(optarg);
            break;
        case 'h':
            usage(argv[0]);
            break;
        default:
            abort();
        }
    }

    if (optind == argc) {
        n = 14;
    }
    else if ((optind + 1) != argc) {
        usage(argv[0]);
        exit(1);
    }
    else {
        n = atoi(argv[optind]);
    }

    lace_start(workers, dqsize);

    printf("Running nqueens n=%d with %u workers...\n", n, lace_workers());

    double t1 = wctime();
    long res = RUN(nqueens, NULL, n, -1, 0);
    double t2 = wctime();

    printf("Result: nqueens(%d) = %ld\n", n, res);
    printf("Time: %f\n", t2 - t1);

    lace_stop();

    return 0;
}