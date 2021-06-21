#include "wool-8.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <getopt.h>

#ifndef ROOT_CALL
#define ROOT_CALL CALL
#endif

#define REAL float

double wctime() 
{
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (tv.tv_sec + 1E-9 * tv.tv_nsec);
}

void zero(REAL *A, int n)
{
    int i, j;

    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            A[i * n + j] = 0.0;
        }
    }
}

void init(REAL *A, int n)
{
    int i, j;

    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            A[i * n + j] = (double)rand();
        }
    }
}

double maxerror(REAL *A, REAL *B, int n)
{
    int i, j;
    double error = 0.0;

    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            double diff = (A[i * n + j] - B[i * n + j]) / A[i * n + j];
            if (diff < 0)
                diff = -diff;
            if (diff > error)
                error = diff;
        }
    }
    return error;
}

void iter_matmul(REAL *A, REAL *B, REAL *C, int n)
{
    int i, j, k;

    for (i = 0; i < n; i++)
        for (k = 0; k < n; k++) {
            REAL c = 0.0;
            for (j = 0; j < n; j++)
                c += A[i * n + j] * B[j * n + k];
            C[i * n + k] = c;
        }
}

/*
 * A \in M(m, n)
 * B \in M(n, p)
 * C \in M(m, p)
 */
VOID_TASK_8(rec_matmul, REAL*, A, REAL*, B, REAL*, C, int, m, int, n, int, _p, int, ld, int, add)
{
    if ((m + n + _p) <= 64) {
        int i, j, k;
        /* base case */
        if (add) {
            for (i = 0; i < m; i++)
                for (k = 0; k < _p; k++) {
                    REAL c = 0.0;
                    for (j = 0; j < n; j++)
                        c += A[i * ld + j] * B[j * ld + k];
                    C[i * ld + k] += c;
                }
        } else {
            for (i = 0; i < m; i++)
                for (k = 0; k < _p; k++) {
                    REAL c = 0.0;
                    for (j = 0; j < n; j++)
                        c += A[i * ld + j] * B[j * ld + k];
                    C[i * ld + k] = c;
                }
        }
    } else if (m >= n && n >= _p) {
        int m1 = m >> 1;
        SPAWN(rec_matmul, A, B, C, m1, n, _p, ld, add);
        CALL(rec_matmul, A + m1 * ld, B, C + m1 * ld, m - m1, n, _p, ld, add);
        SYNC(rec_matmul);
    } else if (n >= m && n >= _p) {
        int n1 = n >> 1;
        CALL(rec_matmul, A, B, C, m, n1, _p, ld, add);
        CALL(rec_matmul, A + n1, B + n1 * ld, C, m, n - n1, _p, ld, 1);
    } else {
        int p1 = _p >> 1;
        SPAWN(rec_matmul, A, B, C, m, n, p1, ld, add);
        CALL(rec_matmul, A, B + p1, C + p1, m, n, _p - p1, ld, add);
        SYNC(rec_matmul);
    }
}

void usage(char *s)
{
    fprintf(stderr, "%s -w <workers> -n <stealable> [-q dqsize] <n>\n", s);
}

int main(int argc, char *argv[])
{
    int workers = 1;
    int dqsize = 100000;
    int n_stealable = 0;

    char c;
    while ((c=getopt(argc, argv, "w:q:hn:")) != -1) {
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
            case 'n':
                n_stealable = atoi(optarg);
                break;
            default:
                abort();
        }
    }

    if (optind == argc) {
        usage(argv[0]);
        exit(1);
    }

    int n = atoi(argv[optind]);

    REAL *A  = malloc(n * n * sizeof(REAL));
    REAL *B  = malloc(n * n * sizeof(REAL));
    REAL *C1 = malloc(n * n * sizeof(REAL));
    REAL *C2 = malloc(n * n * sizeof(REAL));

    init(A, n);
    init(B, n);
    zero(C1, n);
    zero(C2, n);

    wool_init2(workers, dqsize, n_stealable, 0);

    double t1 = wctime();
    ROOT_CALL(rec_matmul, A, B, C2, n, n, n, n, 0); 
    double t2 = wctime();

    printf("Time: %f\n", t2-t1);
/*
    iter_matmul(A, B, C1, n);
    double err = maxerror(C1, C2, n);

    printf("Max error matmul(%d x %d) = %g\n", n, n, err);
*/
    wool_fini();

    free(C2);
    free(C1);
    free(B);
    free(A);
    return 0;
}
