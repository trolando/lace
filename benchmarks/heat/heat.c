/*
 * Heat diffusion (Jacobi-type iteration)
 *
 * Volker Strumpen, Boston                                 August 1996
 *
 * Copyright (c) 1996 Massachusetts Institute of Technology
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
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <getopt.h>


#define f(x,y)     (sin(x)*sin(y))
#define randa(x,t) (0.0)
#define randb(x,t) (exp(-2*(t))*sin(x))
#define randc(y,t) (0.0)
#define randd(y,t) (exp(-2*(t))*sin(y))
#define solu(x,y,t) (exp(-2*(t))*sin(x)*sin(y))

int nx, ny, nt;
double xu, xo, yu, yo, tu, to;

double dx, dy, dt;
double dtdxsq, dtdysq;

double **  odd;
double ** even;

static void heat(double ** m, int il, int iu)
{
    if (iu - il > 1) {
        int im = (il + iu) / 2;

        heat(m, il, im);
        heat(m, im, iu);

        return;
    }

    int i = il;
    int j;
    double * row = m[i];

    if (i == 0) {
        for (j = 0; j < ny; ++j) {
            row[j] = randc(yu + j * dy, 0);
        }
    } else if (i == nx - 1) {
        for (j = 0; j < ny; ++j) {
            row[j] = randd(yu + j * dy, 0);
        }
    } else {
        row[0] = randa(xu + i * dx, 0);
        for (j = 1; j < ny - 1; ++j) {
            row[j] = f(xu + i * dx, yu + j * dy);
        }
        row[ny - 1] = randb(xu + i * dx, 0);
    }
}

void diffuse(double ** out, double ** in, int il, int iu, double t)
{
    if (iu - il > 1) {
        int im = (il + iu) / 2;

        diffuse(out, in, il, im, t);
        diffuse(out, in, im, iu, t);

        return;
    }

    int i = il;
    int j;
    double * row = out[i];

    if (i == 0) {
        for (j = 0; j < ny; ++j) {
            row[j] = randc(yu + j * dy, t);
        }
    } else if (i == nx - 1) {
        for (j = 0; j < ny; ++j) {
            row[j] = randd(yu + j * dy, t);
        }
    } else {
        row[0] = randa(xu + i * dx, t);
        for (j = 1; j < ny - 1; ++j) {
            row[j] = in[i][j]
                + dtdysq * (in[i][j + 1] - 2 * in[i][j] + in[i][j - 1])
                + dtdxsq * (in[i + 1][j] - 2 * in[i][j] + in[i - 1][j]);
        }
        row[ny - 1] = randb(xu + i * dx, t);
    }
}

void init(int n)
{
    nx = n;
    ny = 1024;
    nt = 100;
    xu = 0.0;
    xo = 1.570796326794896558;
    yu = 0.0;
    yo = 1.570796326794896558;
    tu = 0.0;
    to = 0.0000001;

    dx = (xo - xu) / (nx - 1);
    dy = (yo - yu) / (ny - 1);
    dt = (to - tu) / nt;

    dtdxsq = dt / (dx * dx);
    dtdysq = dt / (dy * dy);

    even = malloc(sizeof(double * [nx]));
    odd  = malloc(sizeof(double * [nx]));

    int i;
    for (i = 0; i < nx; ++i) {
        even[i] = malloc(sizeof(double [ny]));
        odd [i] = malloc(sizeof(double [ny]));
    }
}

void prep()
{
    heat(even, 0, nx);
}

void test()
{
    double t = tu;
    int i;

    for (i = 1; i <= nt; i += 2) {
        diffuse(odd, even, 0, nx, t += dt);
        diffuse(even, odd, 0, nx, t += dt);
    }

    if (nt % 2) {
        diffuse(odd, even, 0, nx, t += dt);
    }
}

int verify()
{
    double **mat;
    double mae = 0.0;
    double mre = 0.0;
    double me = 0.0;

    mat = nt % 2 ? odd : even;

    int a, b;

    for (a = 0; a < nx; ++a) {
        for (b = 0; b < ny; ++b) {
            double tmp = fabs(mat[a][b] - solu(xu + a * dx, yu + b * dy, to));

            me += tmp;
            if (tmp > mae) mae = tmp;
            if (mat[a][b] != 0.0) tmp = tmp / mat[a][b];
            if (tmp > mre) mre = tmp;
        }
    }

    me = me / (nx * ny);

    if (mae > 1e-12) {
        printf("Local maximal absolute error %10e\n", mae);
        return 1;
    } if (mre > 1e-12) {
        printf("Local maximal relative error %10e\n", mre);
        return 1;
    } if (me > 1e-12) {
        printf("Global Mean absolute error %10e\n", me);
        return 1;
    }

    return 0;
}

static double wctime() 
{
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (tv.tv_sec + 1E-9 * tv.tv_nsec);
}

static void usage(char *s)
{
    fprintf(stderr, "Usage: %s <n>\n", s);
}

int main( int argc, char **argv )
{
    int c;
    while ((c=getopt(argc, argv, "w:q:h")) != -1) {
        switch (c) {
            case 'h':
                usage(argv[0]);
                break;
            default:
                abort();
        }
    }

    int n;
    if (optind == argc) {
        n = 8192;
    } else if ((optind+1) != argc) {
        usage(argv[0]);
        exit(1);
    } else {
        n = atoi(argv[optind]);
    }

    printf("Running heat n=%d sequentially...\n", n);

    init(n);
    prep();
    double t1 = wctime();
    test();
    double t2 = wctime();
    verify();

    printf("Time: %f\n", t2-t1);

    return 0;
}
