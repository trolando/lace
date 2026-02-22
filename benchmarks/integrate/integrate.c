#include <stdio.h>
#include <stdlib.h>

#include <common.h>

static const double epsilon = 1.0e-9;

static double f(double x)
{
    return (x * x + 1.0) * x;
}

static double
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

    area_x1x0 = integrate(x1, y1, x0, y0, area_x1x0);
    area_x0x2 = integrate(x0, y0, x2, y2, area_x0x2);

    return area_x1x0 + area_x0x2;
}

int main( int argc, char **argv )
{
    int n = 10000;

    if (argc == 1) {
        n = 10000;
    } else if (argc > 2) {
        fprintf(stderr, "Usage: %s <n>\n", argv[0]);
        exit(2);
    } else {
        n = atoi(argv[1]);
    }

    printf("Running integrate n=%d sequentially...\n", n);

    double t1 = wctime();
    double m = integrate(0, f(0), n, f(n), 0);
    double t2 = wctime();

    printf("integrate(%d) = %f\n", n, m);
    printf("Time: %f\n", t2-t1);
    return 0;
}

