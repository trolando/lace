#include "lace.h"
#include <math.h>
#include <stdio.h> // for printf, fprintf
#include <stdlib.h> // for exit, atol
#include <sys/time.h>
#include <getopt.h>

static __thread unsigned int seed = 0;

TASK_2(uint64_t, pi_mc, long, start, long, cnt)
{
    if (cnt == 1) {
        if (seed == 0) seed = LACE_WORKER_ID+1;
        double x = rand_r(&seed)/(double)RAND_MAX;
        double y = rand_r(&seed)/(double)RAND_MAX;
        return sqrt(x*x+y*y) < 1.0 ? 1 : 0;
    }
    SPAWN(pi_mc, start, cnt/2);
    uint64_t res = CALL(pi_mc, start+cnt/2, (cnt+1)/2);
    res += SYNC(pi_mc);
    return res;    
}

double wctime() 
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec + 1E-6 * tv.tv_usec);
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
    double pi = 4.0*(double)RUN(pi_mc, 0, n)/n;
    double t2 = wctime();

    printf("With %u workers:\n", lace_workers());
    printf("pi(%ld) = %.12lf (accuracy: %.12lf)\n", n, pi, fabs(M_PI-pi)/M_PI);
    printf("Time: %f\n", t2-t1);

    lace_stop();

    return 0;
}

