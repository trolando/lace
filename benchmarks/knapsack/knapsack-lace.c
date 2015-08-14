#include "lace-4.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include "lace_config.h"
#ifdef LACE_CONFIG_HAVE_SYSTIME_H
#include <sys/time.h> // for gettimeofday
#else
#include "windows/windows_helper.h"
#endif

#ifdef LACE_CONFIG_HAVE_GETOPT_H
#include <getopt.h>
#else
#include "windows/getopt.h"
#endif

double wctime() 
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec + 1E-6 * tv.tv_usec);
}

/* every item in the knapsack has a weight and a value */
#define MAX_ITEMS 256

struct item {
    int value;
    int weight;
};

int best_so_far = INT_MIN;

int compare(struct item *a, struct item *b)
{
    double c = ((double) a->value / a->weight) - ((double) b->value / b->weight);

    if (c > 0)
        return -1;
    if (c < 0)
        return 1;
    return 0;
}

int read_input(const char *filename, struct item *items, int *capacity, int *n)
{
    int i;
    FILE *f;

    if (filename == NULL)
        filename = "\0";
    f = fopen(filename, "r");
    if (f == NULL) {
        fprintf(stderr, "open_input(\"%s\") failed\n", filename);
        return -1;
    }
    /* format of the input: #items capacity value1 weight1 ... */
    fscanf(f, "%d", n);
    fscanf(f, "%d", capacity);

    for (i = 0; i < *n; ++i)
        fscanf(f, "%d %d", &items[i].value, &items[i].weight);

    fclose(f);

    /* sort the items on decreasing order of value/weight */
    /* cilk2c is fascist in dealing with pointers, whence the ugly cast */
    qsort(items, *n, sizeof(struct item),
            (int (*)(const void *, const void *)) compare);

    return 0;
}

/* 
 * return the optimal solution for n items (first is e) and
 * capacity c. Value so far is v.
 */
TASK_4(int, knapsack, struct item *, e, int, c, int, n, int, v)
{
    int with, without, best;
    double ub;

    /* base case: full knapsack or no items */
    if (c < 0)
        return INT_MIN;

    if (n == 0 || c == 0)
        return v;		/* feasible solution, with value v */

    ub = (double) v + c * e->value / e->weight;

    if (ub < best_so_far) {
        /* prune ! */
        //return INT_MIN;
    }
    /* 
     * compute the best solution without the current item in the knapsack 
     */
    SPAWN(knapsack, e + 1, c, n - 1, v);

    /* compute the best solution with the current item in the knapsack */
    with = CALL(knapsack, e + 1, c - e->weight, n - 1, v + e->value);

    without = SYNC(knapsack);

    best = with > without ? with : without;

    /* 
     * notice the race condition here. The program is still
     * correct, in the sense that the best solution so far
     * is at least best_so_far. Moreover best_so_far gets updated
     * when returning, so eventually it should get the right
     * value. The program is highly non-deterministic.
     */
    //if (best > best_so_far)
       // best_so_far = best;

    return best;
}

void usage(char *s)
{
    fprintf(stderr, "%s -w <workers> [-q dqsize] <filename>\n", s);
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

    lace_init(workers, dqsize);
    lace_startup(0, 0, 0);

    LACE_ME;

    struct item items[MAX_ITEMS];	/* array of items */
    int n, capacity, sol;

    if (read_input(argv[optind], items, &capacity, &n))
        return 1;

    double t1 = wctime();
    sol = CALL(knapsack, items, capacity, n, 0);
    double t2 = wctime();

    printf("Best value is %d\n", sol);
    printf("Time: %f\n", t2-t1);

    return 0;
}
