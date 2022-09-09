#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <getopt.h>
#include <lace.h>

static int w, n;

int __attribute__((noinline)) loop()
{
    int i, s=0;

    for( i=0; i<n; i++ ) {
        s += i;
        s *= i;
        s ^= i;
        s *= i;
        s += i;
    }

    return s;
}

TASK_1(int, tree, int, d)

int tree(int d)
{
    if( d>0 ) {
        int i;
        for (i=0;i<w;i++) tree_SPAWN(d-1);
        for (i=0;i<w;i++) tree_SYNC();
        return 0;
    } else {
        return loop();
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
    fprintf(stderr, "%s -w <workers> [-q dqsize] <depth> <width> <grain> <reps>\n", s);
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

    if (optind + 3 >= argc) {
        usage(argv[0]);
        exit(1);
    }

    lace_start(workers, dqsize);

    int d, m;

    d = atoi(argv[optind]);
    w = atoi(argv[optind+1]);
    n = atoi(argv[optind+2]);
    m = atoi(argv[optind+3]);

    printf("Running depth first search on %d balanced trees with depth %d, width %d, grain %d.\n", m, d, w, n);

    double t1 = wctime();
    int i;
    for(i=0; i<m; i++) tree_RUN(d);
    double t2 = wctime();

    printf("Time: %f\n", t2-t1);

    lace_stop();

    return 0;
}
