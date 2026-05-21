#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <lace.h>
#include <common.h>

int n = 8;
static int * a, * b;
static size_t size;

TASK(void, quicksort, int*, a, size_t, n)
void quicksort_IMPL(lace_worker* worker, int* a, size_t n)
{
    if (n < 2) return;

    int pivot = a[n / 2];

    int *left  = a;
    int *right = a + n - 1;

    while (left <= right) {
        if (*left < pivot) {
            left++;
        } else if (*right > pivot) {
            right--;
        } else {
            int tmp = *left;
            *left = *right;
            *right = tmp;
            left++;
            right--;
        }
    }

    quicksort_SPAWN(worker, a, right - a + 1);
    quicksort_CALL(worker, left, a + n - left);
    quicksort_SYNC(worker);
}

int verify(void)
{
    if (size < 2) return 0;

    int prev = a[0];
    for (unsigned int i = 1; i < size; ++i) {
        if (prev > a[i]) return 1;
        prev = a[i];
    }

    return 0;
}

void init(void)
{
    size = 1;

    for (int i = 0; i < n; ++i) {
        size *= 10;
    }

    a = malloc((size_t)size * sizeof *a);
    b = malloc((size_t)size * sizeof *b);

    for (unsigned int i = 0; i < size; ++i) {
        b[i] = rand();
    }
}

void prep(void)
{
    for (unsigned int i = 0; i < size; ++i) {
        a[i] = b[i];
    }
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
        n = 8;
    } else if ((optind+1) != argc) {
        usage(argv[0]);
        exit(2);
    } else {
        n = atoi(argv[optind]);
    }

    lace_start(workers, dqsize, 0);

    printf("Running quicksort n=10^%d with %u worker(s)...\n", n, lace_worker_count());

    init();
    prep();

    double t1 = wctime();
    quicksort(a, size);
    double t2 = wctime();
    printf("Time: %f\n", t2-t1);

    lace_stop();

    return 0;
}

