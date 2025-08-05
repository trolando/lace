#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int w, n;

int __attribute__((noinline)) loop()
{
    int i, s=0;

    for( i=0; i<n; i++ ) {
        s += i;
    }

    return s;
}

void tree(int d)
{
    if( d>0 ) {
        int i;
        for (i=0;i<w;i++) tree(d-1);
    } else {
        loop();
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
    fprintf(stderr, "Usage: %s <depth> <width> <grain> <reps>\n", s);
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        usage(argv[0]);
        exit(1);
    }

    int d, m;

    d = atoi(argv[1]);
    w = atoi(argv[2]);
    n = atoi(argv[3]);
    m = atoi(argv[4]);

    printf("Running depth first search on %d balanced trees with depth %d, width %d, grain %d.\n", m, d, w, n);
    printf("Running sequentially...\n");

    double t1 = wctime();
    int i;
    for(i=0; i<m; i++) tree(d);
    double t2 = wctime();

    printf("Time: %f\n", t2-t1);

    return 0;
}
