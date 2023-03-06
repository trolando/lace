/*
 * Sparse Cholesky code with little blocks at the leaves of the Quad tree
 * Keith Randall -- Aske Plaat
 *
 * This code should run with any square sparse real symmetric matrix
 * from MatrixMarket (http://math.nist.gov/MatrixMarket)
 *
 * run with `cholesky -f george-liu.mtx' for a given matrix, or
 * `cholesky -n 1000 -z 10000' for a 1000x1000 random matrix with 10000
 * nonzeros (caution: random matrices produce lots of fill).
 */
/*
 * Copyright (c) 2000 Massachusetts Institute of Technology
 * Copyright (c) 2000 Matteo Frigo
 *
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
 */

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <getopt.h>
#include <lace.h>

/*************************************************************\
 * Basic types
 \*************************************************************/

typedef double Real;

#define BLOCK_DEPTH 2		/* logarithm base 2 of BLOCK_SIZE */
#define BLOCK_SIZE  (1<<BLOCK_DEPTH)	/* 4 seems to be the optimum */

typedef Real Block[BLOCK_SIZE][BLOCK_SIZE];

#define BLOCK(B,I,J) (B[I][J])

#define _00 0
#define _01 1
#define _10 2
#define _11 3

#define TR_00 _00
#define TR_01 _10
#define TR_10 _01
#define TR_11 _11

typedef struct InternalNode {
    struct InternalNode *child[4];
} InternalNode;

typedef struct {
    Block block;
} LeafNode;

typedef InternalNode *Matrix;

static Matrix A, R;
static int depth;

int n = 4000;
static int nonzeros = 40000;

/*************************************************************\
 * Linear algebra on blocks
 \*************************************************************/

/*
 * block_schur - Compute Schur complement B' = B - AC.
 */
static void block_schur_full(Block B, Block A, Block C)
{
    int i, j, k;
    for (i = 0; i < BLOCK_SIZE; i++) {
        for (j = 0; j < BLOCK_SIZE; j++) {
            for (k = 0; k < BLOCK_SIZE; k++) {
                BLOCK(B, i, j) -= BLOCK(A, i, k) * BLOCK(C, j, k);
            }
        }
    }
}

/*
 * block_schur - Compute Schur complement B' = B - AC.
 */
static void block_schur_half(Block B, Block A, Block C)
{
    int i, j, k;

    /*
     * printf("schur half\n");
     */
    /* Compute Schur complement. */
    for (i = 0; i < BLOCK_SIZE; i++) {
        for (j = 0; j <= i /* BLOCK_SIZE */ ; j++) {
            for (k = 0; k < BLOCK_SIZE; k++) {
                BLOCK(B, i, j) -= BLOCK(A, i, k) * BLOCK(C, j, k);
            }
        }
    }
}

/*
 * block_upper_solve - Perform substitution to solve for B' in
 * B'U = B.
 */
static void block_backsub(Block B, Block U)
{
    int i, j, k;

    /* Perform backward substitution. */
    for (i = 0; i < BLOCK_SIZE; i++) {
        for (j = 0; j < BLOCK_SIZE; j++) {
            for (k = 0; k < i; k++) {
                BLOCK(B, j, i) -= BLOCK(U, i, k) * BLOCK(B, j, k);	/* transpose? */
            }
            BLOCK(B, j, i) /= BLOCK(U, i, i);
        }
    }
}

/*
 * block_cholesky - Factor block B.
 */
static void block_cholesky(Block B)
{
    int i, j, k;

    for (k = 0; k < BLOCK_SIZE; k++) {
        Real x;
        if (BLOCK(B, k, k) < 0.0) {
            printf("sqrt error: %f\n", BLOCK(B, k, k));
            printf("matrix is probably not numerically stable\n");
            exit(9);
        }
        x = sqrt(BLOCK(B, k, k));
        for (i = k; i < BLOCK_SIZE; i++) {
            BLOCK(B, i, k) /= x;
        }
        for (j = k + 1; j < BLOCK_SIZE; j++) {
            for (i = j; i < BLOCK_SIZE; i++) {
                BLOCK(B, i, j) -= BLOCK(B, i, k) * BLOCK(B, j, k);
                if (j > i && BLOCK(B, i, j) != 0.0) {
                    printf("Upper not empty\n");
                }
            }
        }
    }
}

/*
 * block_zero - zero block B.
 */
static void block_zero(Block B)
{
    int i, k;

    for (i = 0; i < BLOCK_SIZE; i++) {
        for (k = 0; k < BLOCK_SIZE; k++) {
            BLOCK(B, i, k) = 0.0;
        }
    }
}

/*************************************************************\
 * Allocation and initialization
 \*************************************************************/

/*
 * Create new leaf nodes (BLOCK_SIZE x BLOCK_SIZE submatrices)
 */
static inline InternalNode *new_block_leaf(void)
{
    LeafNode *leaf = malloc(sizeof(LeafNode));
    if (leaf == NULL) {
        printf("out of memory!\n");
        exit(1);
    }
    return (InternalNode *) leaf;
}

/*
 * Create internal node in quadtree representation
 */
static inline InternalNode *new_internal(InternalNode * a00, InternalNode * a01,
        InternalNode * a10, InternalNode * a11)
{
    InternalNode *node = malloc(sizeof(InternalNode));
    if (node == NULL) {
        printf("out of memory!\n");
        exit(1);
    }
    node->child[_00] = a00;
    node->child[_01] = a01;
    node->child[_10] = a10;
    node->child[_11] = a11;
    return node;
}

/*
 * Duplicate matrix.  Resulting matrix may be laid out in memory
 * better than source matrix.
 */
static Matrix copy_matrix(int depth, Matrix a)
{
    Matrix r;

    if (!a)
        return a;

    if (depth == BLOCK_DEPTH) {
        LeafNode *A = (LeafNode *) a;
        LeafNode *R;
        r = new_block_leaf();
        R = (LeafNode *) r;
        memcpy(R->block, A->block, sizeof(Block));
    } else {
        Matrix r00, r01, r10, r11;

        depth--;

        r00 = copy_matrix(depth, a->child[_00]);
        r01 = copy_matrix(depth, a->child[_01]);
        r10 = copy_matrix(depth, a->child[_10]);
        r11 = copy_matrix(depth, a->child[_11]);

        r = new_internal(r00, r01, r10, r11);
    }
    return r;
}

/*
 * Deallocate matrix.
 */
void free_matrix(int depth, Matrix a)
{
    if (a == NULL)
        return;
    if (depth == BLOCK_DEPTH) {
        free(a);
    } else {
        depth--;
        free_matrix(depth, a->child[_00]);
        free_matrix(depth, a->child[_01]);
        free_matrix(depth, a->child[_10]);
        free_matrix(depth, a->child[_11]);
        free(a);
    }
}

/*************************************************************\
 * Simple matrix operations
 \*************************************************************/

/*
 * Get matrix element at row r, column c.
 */
static Real get_matrix(int depth, Matrix a, int r, int c)
{
    if (a == NULL)
        return 0.0;

    if (depth == BLOCK_DEPTH) {
        LeafNode *A = (LeafNode *) a;
        return BLOCK(A->block, r, c);
    } else {
        int mid;

        depth--;
        mid = 1 << depth;

        if (r < mid) {
            if (c < mid)
                return get_matrix(depth, a->child[_00], r, c);
            else
                return get_matrix(depth, a->child[_01], r, c - mid);
        } else {
            if (c < mid)
                return get_matrix(depth, a->child[_10], r - mid, c);
            else
                return get_matrix(depth, a->child[_11], r - mid, c - mid);
        }
    }
}

/*
 * Set matrix element at row r, column c to value.
 */
static Matrix set_matrix(int depth, Matrix a, int r, int c, Real value)
{
    if (depth == BLOCK_DEPTH) {
        LeafNode *A;
        if (a == NULL) {
            a = new_block_leaf();
            A = (LeafNode *) a;
            block_zero(A->block);
        } else {
            A = (LeafNode *) a;
        }
        BLOCK(A->block, r, c) = value;
    } else {
        int mid;

        if (a == NULL)
            a = new_internal(NULL, NULL, NULL, NULL);

        depth--;
        mid = 1 << depth;

        if (r < mid) {
            if (c < mid)
                a->child[_00] = set_matrix(depth, a->child[_00],
                        r, c, value);
            else
                a->child[_01] = set_matrix(depth, a->child[_01],
                        r, c - mid, value);
        } else {
            if (c < mid)
                a->child[_10] = set_matrix(depth, a->child[_10],
                        r - mid, c, value);
            else
                a->child[_11] = set_matrix(depth, a->child[_11],
                        r - mid, c - mid, value);
        }
    }
    return a;
}

/*************************************************************\
 * Cholesky algorithm
 \*************************************************************/

/*
 * Perform R -= A * Transpose(B)
 * if lower==1, update only lower-triangular part of R
 */
TASK_5(Matrix, mul_and_subT, int, depth, int, lower, Matrix, a, Matrix, b, Matrix, r)

Matrix mul_and_subT(LaceWorker* worker, int depth, int lower, Matrix a, Matrix b, Matrix r)
{
    if (depth == BLOCK_DEPTH) {
        LeafNode *A = (LeafNode *) a;
        LeafNode *B = (LeafNode *) b;
        LeafNode *R;

        if (r == NULL) {
            r = new_block_leaf();
            R = (LeafNode *) r;
            block_zero(R->block);
        } else
            R = (LeafNode *) r;

        if (lower)
            block_schur_half(R->block, A->block, B->block);
        else
            block_schur_full(R->block, A->block, B->block);
    } else {
        Matrix r00, r01, r10, r11;

        depth--;

        if (r != NULL) {
            r00 = r->child[_00];
            r01 = r->child[_01];
            r10 = r->child[_10];
            r11 = r->child[_11];
        } else {
            r00 = NULL;
            r01 = NULL;
            r10 = NULL;
            r11 = NULL;
        }

        // first spawn

        if (a->child[_00] && b->child[TR_00])
            mul_and_subT_SPAWN(worker, depth, lower, a->child[_00], b->child[TR_00], r00);
        if (!lower && a->child[_00] && b->child[TR_01])
            mul_and_subT_SPAWN(worker, depth, 0, a->child[_00], b->child[TR_01], r01);
        if (a->child[_10] && b->child[TR_00])
            mul_and_subT_SPAWN(worker, depth, 0, a->child[_10], b->child[TR_00], r10);
        if (a->child[_10] && b->child[TR_01])
            mul_and_subT_SPAWN(worker, depth, lower, a->child[_10], b->child[TR_01], r11);

        // then sync

        if (a->child[_10] && b->child[TR_01])
            r11 = mul_and_subT_SYNC(worker);
        if (a->child[_10] && b->child[TR_00])
            r10 = mul_and_subT_SYNC(worker);
        if (!lower && a->child[_00] && b->child[TR_01])
            r01 = mul_and_subT_SYNC(worker);
        if (a->child[_00] && b->child[TR_00])
            r00 = mul_and_subT_SYNC(worker);

        // first spawn

        if (a->child[_01] && b->child[TR_10])
            mul_and_subT_SPAWN(worker, depth, lower, a->child[_01], b->child[TR_10], r00);
        if (!lower && a->child[_01] && b->child[TR_11])
            mul_and_subT_SPAWN(worker, depth, 0, a->child[_01], b->child[TR_11], r01);
        if (a->child[_11] && b->child[TR_10])
            mul_and_subT_SPAWN(worker, depth, 0, a->child[_11], b->child[TR_10], r10);
        if (a->child[_11] && b->child[TR_11])
            mul_and_subT_SPAWN(worker, depth, lower, a->child[_11], b->child[TR_11], r11);

        // then sync

        if (a->child[_11] && b->child[TR_11])
            r11 = mul_and_subT_SYNC(worker);
        if (a->child[_11] && b->child[TR_10])
            r10 = mul_and_subT_SYNC(worker);
        if (!lower && a->child[_01] && b->child[TR_11])
            r01 = mul_and_subT_SYNC(worker);
        if (a->child[_01] && b->child[TR_10])
            r00 = mul_and_subT_SYNC(worker);

        if (r == NULL) {
            if (r00 || r01 || r10 || r11)
                r = new_internal(r00, r01, r10, r11);
        } else {
            r->child[_00] = r00;
            r->child[_01] = r01;
            r->child[_10] = r10;
            r->child[_11] = r11;
        }
    }
    return r;
}

/*
 * Perform substitution to solve for B in BL = A
 * Returns B in place of A.
 */
TASK_3(Matrix, backsub, int, depth, Matrix, a, Matrix, l)

Matrix backsub(LaceWorker* worker, int depth, Matrix a, Matrix l)
{
    if (depth == BLOCK_DEPTH) {
        LeafNode *A = (LeafNode *) a;
        LeafNode *L = (LeafNode *) l;
        block_backsub(A->block, L->block);
    } else {
        Matrix a00, a01, a10, a11;
        Matrix l00, l10, l11;

        depth--;

        a00 = a->child[_00];
        a01 = a->child[_01];
        a10 = a->child[_10];
        a11 = a->child[_11];

        l00 = l->child[_00];
        l10 = l->child[_10];
        l11 = l->child[_11];

        if (a00) backsub_SPAWN(worker, depth, a00, l00);
        if (a10) backsub_SPAWN(worker, depth, a10, l00);
        if (a10) a10 = backsub_SYNC(worker);
        if (a00) a00 = backsub_SYNC(worker);

        if (a00 && l10) mul_and_subT_SPAWN(worker, depth, 0, a00, l10, a01);
        if (a10 && l10) mul_and_subT_SPAWN(worker, depth, 0, a10, l10, a11);
        if (a10 && l10) a11 = mul_and_subT_SYNC(worker);
        if (a00 && l10) a01 = mul_and_subT_SYNC(worker);

        if (a01) backsub_SPAWN(worker, depth, a01, l11);
        if (a11) backsub_SPAWN(worker, depth, a11, l11);
        if (a11) a11 = backsub_SYNC(worker);
        if (a01) a01 = backsub_SYNC(worker);

        a->child[_00] = a00;
        a->child[_01] = a01;
        a->child[_10] = a10;
        a->child[_11] = a11;
    }

    return a;
}

/*
 * Compute Cholesky factorization of A.
 */
TASK_2(Matrix, cholesky, int, depth, Matrix, a)

Matrix cholesky(LaceWorker* worker, int depth, Matrix a)
{
    if (depth == BLOCK_DEPTH) {
        LeafNode *A = (LeafNode *) a;
        block_cholesky(A->block);
    } else {
        Matrix a00, a10, a11;

        depth--;

        a00 = a->child[_00];
        a10 = a->child[_10];
        a11 = a->child[_11];

        if (!a10) {
            cholesky_SPAWN(worker, depth, a00);
            a11 = cholesky(worker, depth, a11);
            a00 = cholesky_SYNC(worker);
        } else {
            a00 = cholesky(worker, depth, a00);
            a10 = backsub(worker, depth, a10, a00);
            a11 = mul_and_subT(worker, depth, 1, a10, a10, a11);
            a11 = cholesky(worker, depth, a11);
        }
        a->child[_00] = a00;
        a->child[_10] = a10;
        a->child[_11] = a11;
    }
    return a;
}

static int logarithm(int size)
{
    int k = 0;

    while ((1 << k) < size)
        k++;
    return k;
}

void init()
{
    /* generate random matrix */
    depth = logarithm(n);

    /* diagonal elements */
    int i;
    for (i = 0; i < n; i++)
        A = set_matrix(depth, A, i, i, 1.0);

    /* off-diagonal elements */
    for (i = 0; i < nonzeros - n; i++) {
        int r, c;

        do {
            r = rand() % n;
            c = rand() % n;
        } while (r <= c || get_matrix(depth, A, r, c) != 0.0);

        A = set_matrix(depth, A, r, c, 0.1);
    }

    /* extend to power of two n with identity matrix */
    for (i = n; i < (1 << depth); i++) {
        A = set_matrix(depth, A, i, i, 1.0);
    }

    R = copy_matrix(depth, A);
}

static double wctime() 
{
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (tv.tv_sec + 1E-9 * tv.tv_nsec);
}

static void usage(char *s)
{
    fprintf(stderr, "Usage: %s <n> <nonzeros>\n", s);
}

int main(int argc, char **argv)
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
        n = 4000;
        nonzeros = 40000;
    } else if ((optind+2) != argc) {
        exit(2);
    } else {
        n = atoi(argv[optind]);
        nonzeros = atoi(argv[optind+1]);
    }

    lace_start(workers, dqsize);

    init();
    double t1 = wctime();
    R = cholesky_RUN(depth, R);
    double t2 = wctime();
    printf("Time: %f\n", t2-t1);

    lace_stop();

    return 0;
}

