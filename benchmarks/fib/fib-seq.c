#include <stdio.h>
#include <stdlib.h>

#include <common.h>

int pfib(int n)
{
    if (n < 2) {
        return n;
    } else {
        return pfib(n-1) + pfib(n-2);
    }
}

int main( int argc, char **argv )
{
    int n,m;

    if (argc < 2) {
        fprintf(stderr, "Usage: fib-seq <arg>\n");
        exit(2);
    }

    n = atoi(argv[1]);

    printf("Running fibonacci n=%d sequentially...\n", n);

    double t1 = wctime();
    m = pfib(n);
    double t2 = wctime();

    printf("fib(%d) = %d\n", n, m);
    printf("Time: %f\n", t2-t1);
    return 0;
}

