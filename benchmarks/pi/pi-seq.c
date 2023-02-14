#include <math.h>
#include <stdint.h>
#include <stdio.h> // for printf, fprintf
#include <stdlib.h> // for exit, atol
#include <time.h>

double wctime() 
{
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (tv.tv_sec + 1E-9 * tv.tv_nsec);
}

/**
 * Simple random number generated (like rand) using the given seed.
 * (Used for thread-specific (scalable) random number generation.
 */
static inline uint32_t
rng(uint32_t *seed, int max)
{
    uint32_t next = *seed;

    next *= 1103515245;
    next += 12345;

    *seed = next;

    return next % max;
}

void usage(char *s)
{
    fprintf(stderr, "%s <n>\n", s);
}

int main(int argc, char **argv)
{
    if (argc <= 1) {
        usage(argv[0]);
        exit(1);
    }

    const long n = atol(argv[1]);

    double x, y;
    uint64_t count = 0;
    unsigned int seed = 1234321;
    long m = n;

    double t1 = wctime();
    while (m--) {
        x = rng(&seed, RAND_MAX)/(double)RAND_MAX;
        y = rng(&seed, RAND_MAX)/(double)RAND_MAX;
        if (sqrt(x*x+y*y) < 1.0) ++count;
    }
    double pi = 4.0*(double)count/n;
    double t2 = wctime();

    printf("pi(%ld) = %.12lf (accuracy: %.12lf)\n", n, pi, fabs(M_PI-pi)/M_PI);
    printf("Time: %f\n", t2-t1);

    return 0;
}

