#define _USE_MATH_DEFINES
#include <math.h>
#include <stdio.h> // for printf, fprintf
#include <stdlib.h> // for exit, atol

#include <lace.h>
#include <common.h>


static LACE_TLS unsigned int seed = 0;

/**
 * Simple random number generated (like rand) using the given seed.
 * (Used for thread-specific (scalable) random number generation.
 */
static inline uint32_t
rng(uint32_t* seed, int max)
{
    uint32_t next = *seed;

    next *= 1103515245;
    next += 12345;

    *seed = next;

    return next % max;
}

TASK(uint64_t, pi_mc, long, start, long, cnt)
{
    if (cnt == 1) {
        if (seed == 0) seed = LACE_WORKER_ID + 1;
        double x = rng(&seed, RAND_MAX) / (double)RAND_MAX;
        double y = rng(&seed, RAND_MAX) / (double)RAND_MAX;
        return sqrt(x * x + y * y) < 1.0 ? 1 : 0;
    }
    SPAWN(pi_mc, start, cnt / 2);
    uint64_t res = CALL(pi_mc, start + cnt / 2, (cnt + 1) / 2);
    res += SYNC(pi_mc);
    return res;
}

void usage(char* s)
{
    fprintf(stderr, "Usage: %s [-w <workers>] [-q <dqsize>] <n>\n", s);
}

int main(int argc, char** argv)
{
    long n = 500000000L;
    int workers = 0;
    int dqsize = 1000000;

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
        n = 500000000L;
    }
    else if ((optind + 1) != argc) {
        usage(argv[0]);
        exit(1);
    }
    else {
        n = atol(argv[optind]);
    }

    lace_start(workers, dqsize);

    printf("Running pi n=%ld with %u worker(s)...\n", n, lace_workers());

    double t1 = wctime();
    double pi = 4.0 * (double)RUN(pi_mc, 0, n) / n;
    double t2 = wctime();

    printf("pi(%ld) = %.12lf (accuracy: %.12lf)\n", n, pi, fabs(M_PI - pi) / M_PI);
    printf("Time: %f\n", t2 - t1);

    lace_stop();

    return 0;
}
