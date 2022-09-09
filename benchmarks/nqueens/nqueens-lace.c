#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <getopt.h>
#include <lace.h>

/**
 * N Queens problem
 * Given already placed queens in array a, and we want n queens, place a queen on row d at position i
 */
TASK_4(int, nqueens, const int*, a, int, n, int, d, int, i)

int nqueens(const int* a, int n, int d, int i)
{
    // copy queens from a to new array aa and check if ok
    int aa[d + 1];

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
    for (int k = 0; k<n; k++) {
        nqueens_SPAWN(aa, n, d, k);
    }

    // and return the sum of the recursive counts
    int sum = 0;
    for (int k=0; k<n; k++) {
        sum += nqueens_SYNC();
    }
    return sum;
}

static double wctime() 
{
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (tv.tv_sec + 1E-9 * tv.tv_nsec);
}

static void usage(char *s)
{
    fprintf(stderr, "%s -w <workers> [-q dqsize] <n>\n", s);
}

int main(int argc, char *argv[])
{
    int workers = 1;
    int dqsize = 100000;

    char c;
    while ((c=getopt(argc, argv, "w:q:h")) != -1) {
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
        usage(argv[0]);
        exit(1);
    }

    lace_start(workers, dqsize);

    int n = atoi(argv[optind]);

    printf("Running nqueens(%d) with %d workers...\n", n, workers);

    double t1 = wctime();
    int res = nqueens_RUN(NULL, n, -1, 0);
    double t2 = wctime();

    printf("Result: Q(%d) = %d\n", n, res);
    printf("Time: %f\n", t2-t1);

    lace_stop();

    return 0;
}
