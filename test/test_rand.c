#include <stdio.h>
#include <stdlib.h>

#include <lace.h>

#include "test_crash_handler.h"

/*
 * Lace must not use the program's rand() sequence. Each worker used to seed
 * its victim selection with two rand() calls in its own thread, at no fixed
 * point relative to the program: a program that seeds rand() for its input
 * (as the cholesky, quicksort, fft, lu and strassen benchmarks do) got other
 * input in every run.
 */

TASK(void, touch)
void touch_CALL(lace_worker* lw)
{
    (void)lw;
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    crash_handler_install();

    srand(42);
    int expect[4];
    for (int i = 0; i < 4; i++) expect[i] = rand();

    srand(42);
    lace_start(4, 0, 0);
    touch_TOGETHER(); /* returns after every worker ran it: all are initialized */
    int got[4];
    for (int i = 0; i < 4; i++) got[i] = rand();
    lace_stop();

    for (int i = 0; i < 4; i++) {
        if (got[i] != expect[i]) {
            printf("rand() after lace_start: value %d is %d, expected %d\n", i, got[i], expect[i]);
            return 1;
        }
    }
    printf("test_rand: ok\n");
    return 0;
}
