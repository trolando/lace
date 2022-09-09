#include <stdio.h> // for printf, fprintf
#include <stdlib.h> // for exit, atoi
#include <time.h>
#include <getopt.h>

#include "lace.h"

TASK_1(int, pfib, int, n)

int pfib(int n)
{
    if( n < 2 ) {
        return n;
    } else {
        int m,k;
        pfib_SPAWN(n-1);
        k = pfib(n-2);
        m = pfib_SYNC();
        return m+k;
    }
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

    double t1 = wctime();
    int m = pfib_RUN(n);
    double t2 = wctime();

    printf("fib(%d) = %d\n", n, m);
    printf("Time: %f\n", t2-t1);

    lace_stop();
    return 0;
}

