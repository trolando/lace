#include "lace-2.h"
#include <stdio.h>
#include <stdlib.h>

int __attribute__((noinline)) loop(int n)
{
    int i, s=0;

    for( i=0; i<n; i++ ) {
        s += i;
    }

    return s;
}

VOID_TASK_2(tree, int, d, int, n)
{
    if( d>0 ) {
        SPAWN(tree, d-1, n);
        CALL(tree, d-1, n);
        SYNC(tree);
    } else {
        loop(n);
    }
}

TASK_2( int, main, int, argc, char **, argv )
{
    int i, d, n, m;

    if( argc < 4 ) {
        fprintf( stderr, "Usage: stress [<wool opts>] <grain> <depth> <reps>\n" );
        return 1;
    }

    n  = atoi( argv[1] );
    d  = atoi( argv[2] );
    m  = atoi( argv[3] );

    for( i=0; i<m; i++) {
        CALL( tree, d, n );
    }
    printf( "DONE\n" );

    return 0;
}
