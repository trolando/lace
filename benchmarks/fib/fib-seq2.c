#include <stdio.h>
#include <stdlib.h>
#include <time.h>

double wctime() 
{
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (tv.tv_sec + 1E-9 * tv.tv_nsec);
}

int pfib(int n)
{
    if( n < 2 ) {
        return n;
    } else {
        if (n == 3) return 2+1;
        if (n == 4) return pfib(3) + 2;
        return pfib(n-2)+pfib(n-3)+pfib(n-3)+pfib(n-4);
    }
}

int main( int argc, char **argv )
{
    int n,m;

    if( argc < 2 ) {
        fprintf( stderr, "Usage: fib-seq <arg>\n" ),
            exit( 2 );
    }

    n = atoi( argv[ 1 ] );

    double t1 = wctime();
    m = pfib( n );
    double t2 = wctime();

    printf( "%d\n", m );
    printf("Time: %f\n", t2-t1);
    return 0;
}

