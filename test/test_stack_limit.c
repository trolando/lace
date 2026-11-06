#include <stdio.h>
#include <stdlib.h>

#include <lace.h>

#include "test_crash_handler.h"

/*
 * Lace must start under a limit on address space (ulimit -v), which some
 * clusters set: the default 1 GB stack reservations then fail, and Lace
 * falls back to the conservative stack size. Linux enforces RLIMIT_AS;
 * elsewhere the limit has no effect and this only checks a normal start.
 * Sanitizers reserve huge shadow mappings, so the limit is skipped there.
 */

#if defined(__linux__)
#include <sys/resource.h>
#define LIMIT_AS 1
#else
#define LIMIT_AS 0
#endif

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#undef LIMIT_AS
#define LIMIT_AS 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
#undef LIMIT_AS
#define LIMIT_AS 0
#endif
#endif

TASK(int, fib, int, n)
{
    if (n < 2) return n;
    SPAWN(fib, n - 1);
    int a = CALL(fib, n - 2);
    int b = SYNC(fib);
    return a + b;
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    crash_handler_install();

    const unsigned int n_workers = 4;

#if LIMIT_AS
    /* 2 GB: room for the program, but not for four 1 GB stacks */
    struct rlimit lim;
    lim.rlim_cur = lim.rlim_max = (rlim_t)2 << 30;
    if (setrlimit(RLIMIT_AS, &lim) != 0) {
        perror("setrlimit");
        return 1;
    }
#endif

    lace_set_scratch_size((size_t)16 << 20); /* the default 1 GB arena would not fit either */
    lace_set_verbosity(1);
    lace_start(n_workers, 0);

    int r = RUN(fib, 25);
    lace_stop();

    if (r != 75025) {
        printf("fib(25) = %d, expected 75025\n", r);
        return 1;
    }
    printf("test_stack_limit: ok\n");
    return 0;
}
