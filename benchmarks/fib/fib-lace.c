#include "lace-1.h"
#include <stdio.h> // for printf, fprintf
#include <stdlib.h> // for exit, atoi
#include <sys/time.h>
#include <getopt.h>

TASK_1(int, pfib, int, n)
{
    if( n < 2 ) {
        return n;
    } else {
        int m,k;
        SPAWN( pfib, n-1 );
        k = CALL( pfib, n-2 );
        m = SYNC( pfib );
        return m+k;
    }
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

    int n = atoi(argv[optind]);

    double t1 = wctime();
    int m = CALL(pfib, n);
    double t2 = wctime();

    printf("fib(%d) = %d\n", n, m);
    printf("Time: %f\n", t2-t1);

    lace_exit();
    return 0;
}

