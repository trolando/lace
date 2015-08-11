#include <math.h>
#include <stdint.h>
#include <stdio.h> // for printf, fprintf
#include <stdlib.h> // for exit, atol

#include "lace_config.h"
#ifdef LACE_CONFIG_HAVE_SYSTIME_H
#include <sys/time.h> // for gettimeofday
#else
#include "windows/windows_helper.h"
#endif

double wctime() 
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec + 1E-6 * tv.tv_usec);
}

void usage(char *s)
{
    fprintf(stderr, "%s <n>\n", s);
}

int main(int argc, char **argv)
{
    if (argc <= 1) {
        usage(argv[0]);
        exit(1);
    }

    const long n = atol(argv[1]);

    double x, y;
    uint64_t count = 0;
    unsigned int seed = 1234321;
    long m = n;

    double t1 = wctime();
    while (m--) {
        x = rand_r(&seed)/(double)RAND_MAX;
        y = rand_r(&seed)/(double)RAND_MAX;
        if (sqrt(x*x+y*y) < 1.0) ++count;
    }
    double pi = 4.0*(double)count/n;
    double t2 = wctime();

    printf("pi(%ld) = %.12lf (accuracy: %.12lf)\n", n, pi, fabs(M_PI-pi)/M_PI);
    printf("Time: %f\n", t2-t1);

    return 0;
}

