#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

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

#ifdef LACE_CONFIG_HAVE_ALLOCA_H
#include <alloca.h>
#elif defined(_MSC_VER)
#include <malloc.h>
#ifndef alloca
#define alloca(size) _alloca((size))
#endif
#endif

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

uint64_t nqueens(int n, int j, char *a)
{
    if (n == j) return 1;

    /* try each possible position for queen <j> */
    int i, k = 0;
    uint64_t res = 0L;
    for (i = 0; i < n; i++) {
        /* allocate a temporary array and copy <a> into it */
        char *b = (char *)alloca((j + 1) * sizeof(char));
        memcpy(b, a, j * sizeof(char));
        b[j] = i;
        if (ok(j + 1, b)) {
            k++;
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
    uint64_t res = nqueens(n, 0, a);
    double t2 = wctime();

    printf("Result: Q(%d) = %zu\n", n, res);

    printf("Time: %f\n", t2-t1);

    return 0;
}
