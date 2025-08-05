/****************************************************************************\
 * LU decomposition
 * Robert Blumofe
 *
 * Copyright (c) 1996, Robert Blumofe.  All rights reserved.
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 \****************************************************************************/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <lace.h>

/* Define the size of a block. */
#ifndef BLOCK_SIZE
#define BLOCK_SIZE 16
#endif

/* Define the default matrix size. */
#ifndef DEFAULT_SIZE
#define DEFAULT_SIZE 4096
#endif

/* A block is a 2D array of doubles. */
typedef double Block[BLOCK_SIZE][BLOCK_SIZE];
#define BLOCK(B,I,J) (B[I][J])

/* A matrix is a 1D array of blocks. */
typedef Block * Matrix;
#define MATRIX(M,I,J) ((M)[(I)*nBlocks+(J)])

/** Matrix size. */
int n = DEFAULT_SIZE;

/** The global matrix and a copy of the matrix. */
static Matrix M;

/* Matrix size in blocks. */
static int nBlocks;

/****************************************************************************\
 * Utility routines.
 \****************************************************************************/

/*
 * init_matrix - Fill in matrix M with random values.
 */
static void init_matrix(Matrix M, int nb)
{
    int I, J, K, i, j, k;

    /* Initialize random number generator. */
    srand(1);

    /* For each element of each block, fill in random value. */
    for (I = 0; I < nb; I++)
        for (J = 0; J < nb; J++)
            for (i = 0; i < BLOCK_SIZE; i++)
                for (j = 0; j < BLOCK_SIZE; j++)
                    BLOCK(MATRIX(M, I, J), i, j) = ((double)rand()) / (double)RAND_MAX;

    /* Inflate diagonal entries. */
    for (K = 0; K < nb; K++)
        for (k = 0; k < BLOCK_SIZE; k++)
            BLOCK(MATRIX(M, K, K), k, k) *= 10.0;
}

/****************************************************************************\
 * Element operations.
 \****************************************************************************/
/*
 * elem_daxmy - Compute y' = y - ax where a is a double and x and y are
 * vectors of doubles.
 */
static void elem_daxmy(double a, double *x, double *y, int n)
{
    for (n--; n >= 0; n--) y[n] -= a * x[n];
}

/****************************************************************************\
 * Block operations.
 \****************************************************************************/

/*
 * block_lu - Factor block B.
 */
static void block_lu(Block B)
{
    int i, k;

    /* Factor block. */
    for (k = 0; k < BLOCK_SIZE; k++)
        for (i = k + 1; i < BLOCK_SIZE; i++) {
            BLOCK(B, i, k) /= BLOCK(B, k, k);
            elem_daxmy(BLOCK(B, i, k), &BLOCK(B, k, k + 1),
                    &BLOCK(B, i, k + 1), BLOCK_SIZE - k - 1);
        }
}

/*
 * block_lower_solve - Perform forward substitution to solve for B' in
 * LB' = B.
 */
static void block_lower_solve(Block B, Block L)
{
    int i, k;

    /* Perform forward substitution. */
    for (i = 1; i < BLOCK_SIZE; i++)
        for (k = 0; k < i; k++)
            elem_daxmy(BLOCK(L, i, k), &BLOCK(B, k, 0),
                    &BLOCK(B, i, 0), BLOCK_SIZE);
}

/*
 * block_upper_solve - Perform forward substitution to solve for B' in
 * B'U = B.
 */
static void block_upper_solve(Block B, Block U)
{
    int i, k;

    /* Perform forward substitution. */
    for (i = 0; i < BLOCK_SIZE; i++)
        for (k = 0; k < BLOCK_SIZE; k++) {
            BLOCK(B, i, k) /= BLOCK(U, k, k);
            elem_daxmy(BLOCK(B, i, k), &BLOCK(U, k, k + 1),
                    &BLOCK(B, i, k + 1), BLOCK_SIZE - k - 1);
        }
}

/*
 * block_schur - Compute Schur complement B' = B - AC.
 */
static void block_schur(Block B, Block A, Block C)
{
    int i, k;

    /* Compute Schur complement. */
    for (i = 0; i < BLOCK_SIZE; i++)
        for (k = 0; k < BLOCK_SIZE; k++)
            elem_daxmy(BLOCK(A, i, k), &BLOCK(C, k, 0),
                    &BLOCK(B, i, 0), BLOCK_SIZE);
}


/****************************************************************************\
 * Divide-and-conquer matrix LU decomposition.
 \****************************************************************************/

/**
 * schur - Compute M' = M - VW.
 */
VOID_TASK_4(schur, Matrix, M, Matrix, V, Matrix, W, int, nb)
{
    Matrix M00, M01, M10, M11;
    Matrix V00, V01, V10, V11;
    Matrix W00, W01, W10, W11;
    int hnb;

    /* Check base case. */
    if (nb == 1) {
        block_schur(*M, *V, *W);
        return;
    }

    /* Break matrices into 4 pieces. */
    hnb = nb / 2;
    M00 = &MATRIX(M, 0, 0);
    M01 = &MATRIX(M, 0, hnb);
    M10 = &MATRIX(M, hnb, 0);
    M11 = &MATRIX(M, hnb, hnb);
    V00 = &MATRIX(V, 0, 0);
    V01 = &MATRIX(V, 0, hnb);
    V10 = &MATRIX(V, hnb, 0);
    V11 = &MATRIX(V, hnb, hnb);
    W00 = &MATRIX(W, 0, 0);
    W01 = &MATRIX(W, 0, hnb);
    W10 = &MATRIX(W, hnb, 0);
    W11 = &MATRIX(W, hnb, hnb);

    /* Form Schur complement with recursive calls. */
    SPAWN(schur, M00, V00, W00, hnb);
    SPAWN(schur, M01, V00, W01, hnb);
    SPAWN(schur, M10, V10, W00, hnb);
    CALL(schur, M11, V10, W01, hnb);
    SYNC(schur);
    SYNC(schur);
    SYNC(schur);

    SPAWN(schur, M00, V01, W10, hnb);
    SPAWN(schur, M01, V01, W11, hnb);
    SPAWN(schur, M10, V11, W10, hnb);
    CALL(schur, M11, V11, W11, hnb);
    SYNC(schur);
    SYNC(schur);
    SYNC(schur);

    return;
}

/*
 * lower_solve - Compute M' where LM' = M.
 */
VOID_TASK_DECL_3(lower_solve, Matrix, Matrix, int)

VOID_TASK_4(aux_lower_solve, Matrix, Ma, Matrix, Mb, Matrix, L, int, nb)
{
    Matrix L00, L10, L11;

    /* Break L matrix into 4 pieces. */
    L00 = &MATRIX(L, 0, 0);
    L10 = &MATRIX(L, nb, 0);
    L11 = &MATRIX(L, nb, nb);

    /* Solve with recursive calls. */
    CALL(lower_solve, Ma, L00, nb);
    CALL(schur, Mb, L10, Ma, nb);
    CALL(lower_solve, Mb, L11, nb);
}

VOID_TASK_IMPL_3(lower_solve, Matrix, M, Matrix, L, int, nb)
{
    Matrix M00, M01, M10, M11;
    int hnb;

    /* Check base case. */
    if (nb == 1) {
        block_lower_solve(*M, *L);
        return;
    }

    /* Break matrices into 4 pieces. */
    hnb = nb / 2;
    M00 = &MATRIX(M, 0, 0);
    M01 = &MATRIX(M, 0, hnb);
    M10 = &MATRIX(M, hnb, 0);
    M11 = &MATRIX(M, hnb, hnb);

    /* Solve with recursive calls. */
    SPAWN(aux_lower_solve, M00, M10, L, hnb);
    CALL(aux_lower_solve, M01, M11, L, hnb);
    SYNC(aux_lower_solve);

    return;
}

/*
 * upper_solve - Compute M' where M'U = M.
 */
VOID_TASK_DECL_3(upper_solve, Matrix, Matrix, int)

VOID_TASK_4(aux_upper_solve, Matrix, Ma, Matrix, Mb, Matrix, U, int, nb)
{
    Matrix U00, U01, U11;

    /* Break U matrix into 4 pieces. */
    U00 = &MATRIX(U, 0, 0);
    U01 = &MATRIX(U, 0, nb);
    U11 = &MATRIX(U, nb, nb);

    /* Solve with recursive calls. */
    CALL(upper_solve, Ma, U00, nb);
    CALL(schur, Mb, Ma, U01, nb);
    CALL(upper_solve, Mb, U11, nb);

    return;
}

VOID_TASK_IMPL_3(upper_solve, Matrix, M, Matrix, U, int, nb)
{
    Matrix M00, M01, M10, M11;
    int hnb;

    /* Check base case. */
    if (nb == 1) {
        block_upper_solve(*M, *U);
        return;
    }

    /* Break matrices into 4 pieces. */
    hnb = nb / 2;
    M00 = &MATRIX(M, 0, 0);
    M01 = &MATRIX(M, 0, hnb);
    M10 = &MATRIX(M, hnb, 0);
    M11 = &MATRIX(M, hnb, hnb);

    /* Solve with recursive calls. */
    SPAWN(aux_upper_solve, M00, M01, U, hnb);
    CALL(aux_upper_solve, M10, M11, U, hnb);
    SYNC(aux_upper_solve);

    return;
}

/*
 * lu - Perform LU decomposition of matrix M.
 */
VOID_TASK_2(lu, Matrix, M, int, nb)
{
    Matrix M00, M01, M10, M11;
    int hnb;

    /* Check base case. */
    if (nb == 1) {
        block_lu(*M);
        return;
    }

    /* Break matrix into 4 pieces. */
    hnb = nb / 2;
    M00 = &MATRIX(M, 0, 0);
    M01 = &MATRIX(M, 0, hnb);
    M10 = &MATRIX(M, hnb, 0);
    M11 = &MATRIX(M, hnb, hnb);

    /* Decompose upper left. */
    CALL(lu, M00, hnb);

    /* Solve for upper right and lower left. */
    SPAWN(lower_solve, M01, M00, hnb);
    CALL(upper_solve, M10, M00, hnb);
    SYNC(lower_solve);

    /* Compute Schur complement of lower right. */
    CALL(schur, M11, M10, M01, hnb);

    /* Decompose lower right. */
    CALL(lu, M11, hnb);

    return;
}

void init()
{
    nBlocks = n / BLOCK_SIZE;
    M = (Matrix) malloc(n * n * sizeof(double));
    init_matrix(M, nBlocks);
}

static double wctime()
{
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (tv.tv_sec + 1E-9 * tv.tv_nsec);
}

static void usage(char *s)
{
    fprintf(stderr, "Usage: %s [-w <workers>] [-q <dqsize>] <n>\n", s);
}

int main(int argc, char **argv)
{
    int workers = 1;
    int dqsize = 100000;

    int c;
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
        n = DEFAULT_SIZE;
    } else if ((optind+1) != argc) {
        usage(argv[0]);
        exit(2);
    } else {
        n = atoi(argv[optind]);
    }
        
    lace_start(workers, dqsize);

    printf("Running lu n=%d with %u worker(s)...\n", n, lace_workers());

    init();
    double t1 = wctime();
    RUN(lu, M, nBlocks);
    double t2 = wctime();
    printf("Time: %f\n", t2-t1);
            
    lace_stop();

    return 0;
}

