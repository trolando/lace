#include "lace-3.h"
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <sys/time.h>
#include <getopt.h>

double wctime() 
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec + 1E-6 * tv.tv_usec);
}

/*
 * <a> contains array of <n> queen positions.  Returns 1
 * if none of the queens conflict, and returns 0 otherwise.
 */
int ok(int n, char *a)
{
    int i, j;
    char p, q;

    for (i = 0; i < n; i++) {
        p = a[i];

        for (j = i + 1; j < n; j++) {
            q = a[j];
            if (q == p || q == p - (j - i) || q == p + (j - i))
                return 0;
        }
    }
    return 1;
}

/*
 * <a> is an array of <j> numbers.  The entries of <a> contain
 * queen positions already set.  If there is any extension of <a>
 * to a complete <n> queen setting, returns one of these queen
 * settings (allocated from the heap).  Otherwise, returns NULL.
 * Does not side-effect <a>.
 */

TASK_3(uint64_t, nqueens, int, n, int, j, char *, a)
{
    if (n == j) return 1;

    /* try each possible position for queen <j> */
    int i, k = 0;
    for (i = 0; i < n; i++) {
        /* allocate a temporary array and copy <a> into it */
        char *b = (char *)alloca((j + 1) * sizeof(char));
        memcpy(b, a, j * sizeof(char));
        b[j] = i;
        if (ok(j + 1, b)) {
            k++;
            SPAWN(nqueens, n, j+1, b);
        }
    }

    uint64_t res = 0;
    for (i=0; i<k; i++) res += SYNC(nqueens);
    return res;
}

void usage(char *s)
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

    lace_init(workers, dqsize, 0);

    LACE_ME;

    int n = atoi(argv[optind]);

    char *a = (char*)alloca(n*sizeof(char));

    printf("running queens %d with %d workers...\n", n, workers);

    double t1 = wctime();
    uint64_t res = CALL(nqueens, n, 0, a);
    double t2 = wctime();

    printf("Result: Q(%d) = %lu\n", n, res);

    printf("Time: %f\n", t2-t1);

    lace_exit();

    return 0;
}
