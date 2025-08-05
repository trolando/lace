#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <getopt.h>

/**
 * N Queens problem
 * Given already placed queens in array a, and we want n queens, place a queen on row d at position i
 */
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
    // and return the sum of the recursive counts
    int sum = 0;
    for (int k=0; k<n; k++) {
        sum += nqueens(aa, n, d, k);
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
    fprintf(stderr, "Usage: %s <n>\n", s);
}

int main(int argc, char *argv[])
{
    int c;
    while ((c=getopt(argc, argv, "w:q:h")) != -1) {
        switch (c) {
            case 'h':
                usage(argv[0]);
                break;
            default:
                abort();
        }
    }

    int n;
    if (optind == argc) {
        n = 14;
    } else if ((optind+1) != argc) {
        usage(argv[0]);
        exit(1);
    } else {
        n = atoi(argv[optind]);
    }

    printf("Running nqueens n=%d sequentially...\n", n);

    double t1 = wctime();
    int res = nqueens(NULL, n, -1, 0);
    double t2 = wctime();

    printf("Result: Q(%d) = %d\n", n, res);
    printf("Time: %f\n", t2-t1);

    return 0;
}
