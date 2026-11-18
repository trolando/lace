/*
 * Tiled DSYRK, shared by dsyrk-seq and dsyrk-lace (and ports to other
 * frameworks), so that they run the same kernel on the same data and only the
 * scheduling differs.
 *
 * C = C - A * A^T on the lower triangle of C (n x n), with A n x k, in b x b
 * tiles, as tiled linear algebra does it (PLASMA, and the X-OpenMP paper).
 * Every tile of the lower triangle of C is updated with the k/b tiles of its
 * row of A, in order, so the result does not depend on the schedule. The
 * kernel is plain C that compilers vectorize (no BLAS): the benchmark is about
 * scheduling coarse, regular tasks, not about the speed of the kernel. A^T is
 * stored next to A (set up before the timing), so that the inner loop runs
 * along rows of C and of A^T.
 */
#ifndef DSYRK_H
#define DSYRK_H

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#if !defined(_WIN32)
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

static int n = 4096, k = 4096, b = 256; // sizes; n and k multiples of b

/* An argument of at least lo and at most hi (no overflow in the int loops),
 * else exit with an error */
static int
dsyrk_arg(const char *name, const char *s, long lo, long hi)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno != 0 || v < lo || v > hi) {
        fprintf(stderr, "%s must be a number from %ld to %ld, not %s\n", name, lo, hi, s);
        exit(1);
    }
    return (int)v;
}
#define DSYRK_DIM_MAX (1L << 20)
static int nt, kt, ntiles;              // tiles per side of C, per row of A, in the lower triangle
static double *A, *AT, *C;              // A: n x k, AT: k x n, C: n x n, row-major
static int *tile_i, *tile_j;            // the lower-triangle tiles of C, row by row

/* C tile (ti, tj) -= A tile (ti, l) * (A tile (tj, l))^T; on the diagonal only j <= i */
static void
dsyrk_tile(int ti, int tj, int l)
{
    double *c = C + (size_t)ti * (size_t)b * (size_t)n + (size_t)tj * (size_t)b;
    const double *a = A + (size_t)ti * (size_t)b * (size_t)k + (size_t)l * (size_t)b;
    const double *at = AT + (size_t)l * (size_t)b * (size_t)n + (size_t)tj * (size_t)b;
    for (int i = 0; i < b; i++) {
        double *restrict ci = c + (size_t)i * (size_t)n;
        const int jend = ti == tj ? i + 1 : b;
        for (int x = 0; x < b; x++) {
            const double v = a[(size_t)i * (size_t)k + (size_t)x];
            const double *restrict atx = at + (size_t)x * (size_t)n;
            for (int j = 0; j < jend; j++) ci[j] -= v * atx[j];
        }
    }
}

/* All k/b updates of one tile of C, in order */
static void
dsyrk_tile_all(int t)
{
    for (int l = 0; l < kt; l++) dsyrk_tile(tile_i[t], tile_j[t], l);
}

/* The matrices are initialized in parallel by plain threads, one per CPU of
 * the affinity mask and pinned to it (Linux), rows in turn: the pages of A, AT
 * and C are spread over the NUMA nodes like an interleaved allocation, the same
 * for every framework under test (Lace, Cilk, sequential), and independent of
 * which worker the scheduler picks. A[x] is a hash of x, so any thread can
 * compute any element. Without pthreads (Windows), one thread does it all. */
static inline uint64_t
dsyrk_hash(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static void
dsyrk_init_rows(int t, int T, int phase)
{
    if (phase == 0) {
        for (int r = t; r < n; r += T) {
            for (int x = 0; x < k; x++) {
                const size_t i = (size_t)r * (size_t)k + (size_t)x;
                A[i] = (double)(dsyrk_hash(i) >> 11) * (1.0 / 9007199254740992.0) - 0.5;
            }
            for (int j = 0; j < n; j++) C[(size_t)r * (size_t)n + (size_t)j] = 0.0;
        }
    } else {
        for (int x = t; x < k; x += T)
            for (int i = 0; i < n; i++) AT[(size_t)x * (size_t)n + (size_t)i] = A[(size_t)i * (size_t)k + (size_t)x];
    }
}

#if !defined(_WIN32)
typedef struct { int t, T, phase, cpu; } dsyrk_init_arg;

static void *
dsyrk_init_thread(void *p)
{
    dsyrk_init_arg *a = (dsyrk_init_arg *)p;
#if defined(__linux__)
    if (a->cpu >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(a->cpu, &set);
        pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    }
#endif
    dsyrk_init_rows(a->t, a->T, a->phase);
    return NULL;
}
#endif

static void
dsyrk_init_parallel(void)
{
#if defined(_WIN32)
    dsyrk_init_rows(0, 1, 0);
    dsyrk_init_rows(0, 1, 1);
#else
    int cpus[1024], T = 0;
#if defined(__linux__)
    cpu_set_t mask;
    if (sched_getaffinity(0, sizeof(mask), &mask) == 0)
        for (int c = 0; c < CPU_SETSIZE && T < 1024; c++) if (CPU_ISSET(c, &mask)) cpus[T++] = c;
#endif
    if (T == 0) {
        const long np = sysconf(_SC_NPROCESSORS_ONLN);
        T = np < 1 ? 1 : np > 1024 ? 1024 : (int)np;
        for (int c = 0; c < T; c++) cpus[c] = -1;
    }
    pthread_t *th = malloc(sizeof(pthread_t) * (size_t)T);
    dsyrk_init_arg *args = malloc(sizeof(dsyrk_init_arg) * (size_t)T);
    char *started = malloc((size_t)T);
    if (th == NULL || args == NULL || started == NULL) {
        free(th); free(args); free(started);
        dsyrk_init_rows(0, 1, 0); dsyrk_init_rows(0, 1, 1);
        return;
    }
    for (int phase = 0; phase < 2; phase++) {
        for (int t = 0; t < T; t++) {
            args[t] = (dsyrk_init_arg){ t, T, phase, cpus[t] };
            started[t] = pthread_create(&th[t], NULL, dsyrk_init_thread, &args[t]) == 0;
            if (!started[t]) dsyrk_init_rows(t, T, phase); // no thread: do its rows here, unpinned
        }
        for (int t = 0; t < T; t++) if (started[t]) pthread_join(th[t], NULL);
    }
    free(th);
    free(args);
    free(started);
#endif
}

/* x * y elements of size s in bytes, or 0 if that does not fit in a size_t */
static size_t
dsyrk_bytes(size_t x, size_t y, size_t s)
{
    if (x != 0 && y > SIZE_MAX / s / x) return 0;
    return x * y * s;
}

/* Allocate and fill A, A^T and C, and list the tiles, for n, k and b as set; 0 or -1 */
static int
dsyrk_setup(void)
{
    if (b <= 0 || n <= 0 || k <= 0 || n % b != 0 || k % b != 0) {
        fprintf(stderr, "n (%d) and k (%d) must be positive multiples of the tile size b (%d)\n", n, k, b);
        return -1;
    }
    nt = n / b;
    kt = k / b;
    const uint64_t tiles = (uint64_t)nt * ((uint64_t)nt + 1) / 2; // nt <= INT_MAX: fits
    const size_t a_bytes = dsyrk_bytes((size_t)n, (size_t)k, sizeof(double));
    const size_t c_bytes = dsyrk_bytes((size_t)n, (size_t)n, sizeof(double));
    if (tiles > INT_MAX || tiles > SIZE_MAX / sizeof(int) || a_bytes == 0 || c_bytes == 0) {
        fprintf(stderr, "n (%d), k (%d) and b (%d) are too large for this machine\n", n, k, b);
        return -1;
    }
    ntiles = (int)tiles;
    tile_i = malloc(sizeof(int) * (size_t)ntiles);
    tile_j = malloc(sizeof(int) * (size_t)ntiles);
    A = malloc(a_bytes);
    AT = malloc(a_bytes);
    C = malloc(c_bytes);
    if (!tile_i || !tile_j || !A || !AT || !C) {
        fprintf(stderr, "out of memory\n");
        return -1;
    }
    for (int i = 0, t = 0; i < nt; i++) for (int j = 0; j <= i; j++, t++) { tile_i[t] = i; tile_j[t] = j; }
    dsyrk_init_parallel();
    return 0;
}

/* Floating-point operations of the whole update: two per element of the lower triangle, per column of A */
static double
dsyrk_flops(void)
{
    return 2.0 * (double)k * (double)n * (double)(n + 1) / 2.0;
}

static void
dsyrk_free(void)
{
    free(tile_i);
    free(tile_j);
    free(A);
    free(AT);
    free(C);
}

#endif
