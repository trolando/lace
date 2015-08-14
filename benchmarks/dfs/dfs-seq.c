#include <stdio.h>
#include <stdlib.h>

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

#ifdef _MSC_VER
#define __attribute__(arg)
#endif

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
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec + 1E-6 * tv.tv_usec);
}

void usage(char *s)
{
    fprintf(stderr, "%s <depth> <width> <grain> <reps>\n", s);
}

int main(int argc, char **argv)
{
    if (optind + 3 >= argc) {
        usage(argv[0]);
        exit(1);
    }

    int d, m;

    d = atoi(argv[optind]);
    w = atoi(argv[optind+1]);
    n = atoi(argv[optind+2]);
    m = atoi(argv[optind+3]);

    printf("Running depth first search on %d balanced trees with depth %d, width %d, grain %d.\n", m, d, w, n);

    double t1 = wctime();
    int i;
    for(i=0; i<m; i++) tree(d);
    double t2 = wctime();

    printf("Time: %f\n", t2-t1);

    return 0;
}
