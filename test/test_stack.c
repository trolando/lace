#include <stdio.h>
#include <stdlib.h>

#include <lace.h>

#include "test_crash_handler.h"

/*
 * An explicit stack size above the old 64 MB cap must be honoured: workers
 * started with 160 MB stacks run a task that recurses through about 100 MB
 * of stack. Before, lace_start clamped every size to 64 MB and this crashed.
 */

#define FRAME 4096
#define DEPTH (24 * 1024)   /* frames of a little over FRAME bytes: ~100 MB */

static void touch_impl(char *buf, long n)
{
    buf[0] = (char)(n & 0x7f);
    buf[FRAME - 1] = (char)(n & 0x7f);
}

/* Called through a volatile pointer, so the compiler must keep the whole
 * buffer in every frame (it cannot see which bytes the callee uses). */
static void (*volatile touch)(char *, long) = touch_impl;

static LACE_NOINLINE long deep(long n)
{
    char buf[FRAME];
    touch(buf, n);
    long r = n == 0 ? 0 : deep(n - 1);
    /* use the buffer after the call, so the frame stays live (no tail call) */
    return r + (buf[0] == buf[FRAME - 1] ? 1 : 0);
}

TASK(long, deep_task, long, n)

long deep_task_CALL(lace_worker* lw, long n)
{
    (void)lw;
    return deep(n);
}

int
main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);
    crash_handler_install();

    int n_workers = 2;
    if (argc > 1) n_workers = atoi(argv[1]);

    lace_set_verbosity(0);
    lace_start(n_workers, 0, (size_t)160 << 20);

    long r = deep_task(DEPTH);
    lace_stop();

    if (r != DEPTH + 1) {
        printf("deep(%d) = %ld, expected %d\n", DEPTH, r, DEPTH + 1);
        return 1;
    }
    printf("test_stack: ok\n");
    return 0;
}
