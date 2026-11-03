#include <stdio.h>
#include <stdlib.h>

#include <lace.h>

#include "test_crash_handler.h"

/*
 * lace_release_memory gives back what a deep computation left in the worker
 * stacks: a task touches about 64 MB of stack, the workers release it, and
 * the workers keep working (stacks fault in again). On Linux and macOS the
 * resident set must also shrink by at least 32 MB.
 */

#define FRAME 4096
#define DEPTH (16 * 1024)   /* frames of a little over FRAME bytes: ~64 MB */

static void touch_impl(char *buf, long n)
{
    buf[0] = (char)(n & 0x7f);
    buf[FRAME - 1] = (char)(n & 0x7f);
}

/* Called through a volatile pointer, so every frame keeps the whole buffer */
static void (*volatile touch)(char *, long) = touch_impl;

/* AddressSanitizer may put the frames of the functions it instruments on a
 * fake stack of its own (detect_stack_use_after_return), which the workers
 * cannot release: deep() must dirty the worker stack itself. */
#if defined(__SANITIZE_ADDRESS__) && (defined(__GNUC__) || defined(__clang__))
#define NO_ASAN __attribute__((no_sanitize_address))
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define NO_ASAN __attribute__((no_sanitize_address))
#endif
#endif
#ifndef NO_ASAN
#define NO_ASAN
#endif

static LACE_NOINLINE NO_ASAN long deep(long n)
{
    char buf[FRAME];
    touch(buf, n);
    long r = n == 0 ? 0 : deep(n - 1);
    return r + (buf[0] == buf[FRAME - 1] ? 1 : 0);
}

TASK(long, deep_task, long, n)

long deep_task_CALL(lace_worker* lw, long n)
{
    (void)lw;
    return deep(n);
}

TASK(int, fib, int, n)

int fib_CALL(lace_worker* lw, int n)
{
    if (n < 2) return n;
    fib_SPAWN(lw, n - 1);
    int a = fib_CALL(lw, n - 2);
    int b = fib_SYNC(lw);
    return a + b;
}

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

/* Resident set size in bytes, or -1 if unknown */
static long resident(void)
{
#if defined(__APPLE__)
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) return -1;
    return (long)info.resident_size;
#elif defined(__linux__)
    FILE *f = fopen("/proc/self/statm", "r");
    if (f == NULL) return -1;
    long size, pages;
    int ok = fscanf(f, "%ld %ld", &size, &pages) == 2;
    fclose(f);
    return ok ? pages * 4096L : -1;
#else
    return -1;
#endif
}

int
main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);
    crash_handler_install();

    int n_workers = 2;
    if (argc > 1) n_workers = atoi(argv[1]);

    lace_set_verbosity(0);
    lace_start(n_workers, 0, (size_t)128 << 20);

    for (int i = 0; i < 3; i++) {
        long r = deep_task(DEPTH);
        if (r != DEPTH + 1) {
            printf("deep(%d) = %ld, expected %d\n", DEPTH, r, DEPTH + 1);
            return 1;
        }
        long before = resident();
        lace_release_memory();
        long after = resident();
        if (before > 0 && after > 0) {
            printf("round %d: resident %ld MB before release, %ld MB after\n", i, before >> 20, after >> 20);
            if (before - after < (32L << 20)) {
                printf("lace_release_memory released less than 32 MB\n");
                return 1;
            }
        }
        int f = fib(25);
        if (f != 75025) {
            printf("fib(25) = %d after lace_release_memory, expected 75025\n", f);
            return 1;
        }
    }

    lace_stop();
    printf("test_release: ok\n");
    return 0;
}
