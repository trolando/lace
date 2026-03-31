#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <common.h>

int n = 8;
static int * a, * b;
static size_t size;

void quicksort(int * a, size_t n)
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

    quicksort(a, right - a + 1);
    quicksort(left, a + n - left);
}

int verify()
{
    if (size < 2) return 0;

    int prev = a[0];
    for (unsigned int i = 1; i < size; ++i) {
        if (prev > a[i]) return 1;
        prev = a[i];
    }

    return 0;
}

void init()
{
    size = 1;

    for (int i = 0; i < n; ++i) {
        size *= 10;
    }

    a = malloc(size * sizeof *a);
    b = malloc(size * sizeof *b);

    for (unsigned int i = 0; i < size; ++i) {
        b[i] = rand();
    }
}

static void usage(char *s)
{
    fprintf(stderr, "Usage: %s <n>\n", s);
}

int main(int argc, char **argv)
{
    if (argc == 1) {
        n = 8;
    } else if (argc != 2) {
        usage(argv[0]);
    } else {
        n = atoi(argv[1]);
    }

    printf("Running quicksort n=%d sequentially...\n", n);

    init();
    for (unsigned int i = 0; i < size; ++i) {
        a[i] = b[i];
    }
    double t1 = wctime();
    quicksort(a, size);
    double t2 = wctime();

    printf("Time: %f\n" , t2-t1);
    return 0;
}

