#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <time.h>
#include <getopt.h>

double wctime() 
{
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (tv.tv_sec + 1E-9 * tv.tv_nsec);
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

long nqueens(int n, int j, char *a)
{
    if (n == j) return 1;

    /* try each possible position for queen <j> */
    int i;
    long res = 0L;
    for (i = 0; i < n; i++) {
        /* allocate a temporary array and copy <a> into it */
        char *b = (char *)alloca((j + 1) * sizeof(char));
        memcpy(b, a, j * sizeof(char));
        b[j] = i;
        if (ok(j + 1, b)) {
            res += nqueens(n, j+1, b);
        }
    }

    return res;
}

void usage(char *s)
{
    fprintf(stderr, "%s <n>\n", s);
}

int main(int argc, char *argv[])
{
    if (1 == argc) {
        usage(argv[0]);
        exit(1);
    }

    int n = atoi(argv[1]);

    char *a = (char*)alloca(n*sizeof(char));

    printf("running queens %d sequentially...\n", n);

    double t1 = wctime();
    long res = nqueens(n, 0, a);
    double t2 = wctime();

    printf("Result: Q(%d) = %ld\n", n, res);

    printf("Time: %f\n", t2-t1);

    return 0;
}
