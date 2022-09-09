#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <getopt.h>

#include "lace.h"

static const double epsilon = 1.0e-9;

static double f(double x)
{
    return (x * x + 1.0) * x;
}

TASK_5(double, integrate, double, x1, double, y1, double, x2, double, y2, double, area)

double
integrate(double x1, double y1, double x2, double y2, double area)
{
    double half = (x2 - x1) / 2;
    double x0 = x1 + half;
    double y0 = f(x0);

    double area_x1x0 = (y1 + y0) / 2 * half;
    double area_x0x2 = (y0 + y2) / 2 * half;
    double area_x1x2 = area_x1x0 + area_x0x2;

    if (area_x1x2 - area < epsilon && area - area_x1x2 < epsilon) {
        return area_x1x2;
    }

    integrate_SPAWN(x1, y1, x0, y0, area_x1x0);
    area_x0x2 = integrate(x0, y0, x2, y2, area_x0x2);
    area_x1x0 = integrate_SYNC();

    return area_x1x0 + area_x0x2;
}

static double wctime() 
{
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (tv.tv_sec + 1E-9 * tv.tv_nsec);
}

static void usage(char *s)
{
    fprintf(stderr, "%s -w <workers> [-q dqsize] <n>\n", s);
}

int main( int argc, char **argv )
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
    double m = integrate_RUN(0, f(0), n, f(n), 0);
    double t2 = wctime();

    printf("integrate(%d) = %f\n", n, m);
    printf("Time: %f\n", t2-t1);

    lace_stop();
    return 0;
}
