#include "lace.h"
#include <math.h>
#include <stdio.h> // for printf, fprintf
#include <stdlib.h> // for exit, atol
#include <time.h>
#include <getopt.h>

static __thread unsigned int seed = 0;

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

TASK_2(uint64_t, pi_mc, long, start, long, cnt)

uint64_t pi_mc(long start, long cnt)
{
    if (cnt == 1) {
        if (seed == 0) seed = lace_worker_id()+1;
        double x = rng(&seed, RAND_MAX)/(double)RAND_MAX;
        double y = rng(&seed, RAND_MAX)/(double)RAND_MAX;
        return sqrt(x*x+y*y) < 1.0 ? 1 : 0;
    }
    pi_mc_SPAWN(start, cnt/2);
    uint64_t res = pi_mc(start+cnt/2, (cnt+1)/2);
    res += pi_mc_SYNC();
    return res;    
}

double wctime() 
{
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (tv.tv_sec + 1E-9 * tv.tv_nsec);
}

void usage(char *s)
{
    fprintf(stderr, "%s -w <workers> [-q dqsize] <n>\n", s);
}

int main(int argc, char **argv)
{
    int workers = 0;
    int dqsize = 1000000;

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

    long n = atol(argv[optind]);

    double t1 = wctime();
    double pi = 4.0*(double)pi_mc_RUN(0, n)/n;
    double t2 = wctime();

    printf("With %u workers:\n", lace_worker_count());
    printf("pi(%ld) = %.12lf (accuracy: %.12lf)\n", n, pi, fabs(M_PI-pi)/M_PI);
    printf("Time: %f\n", t2-t1);

    lace_stop();

    return 0;
}

