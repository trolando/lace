#include <stdio.h>
#include <stdlib.h>

#include <lace.h>

#include "test_crash_handler.h"

/*
 * Idle workers must not keep the CPU busy: after a task, while the program
 * waits outside Lace for 500 ms, the process may use at most 100 ms of CPU.
 * With a single worker the idle loop never counted a failed round (there is
 * no victim to fail on), so that worker spun on sched_yield forever.
 * Process CPU time is not available on Windows here; there the test only
 * runs the tasks.
 */

#if !defined(_WIN32)
#include <sys/resource.h>
#include <time.h>

static double cpu_seconds(void)
{
    struct rusage u;
    getrusage(RUSAGE_SELF, &u);
    return (double)u.ru_utime.tv_sec + (double)u.ru_stime.tv_sec
         + 1e-6 * (double)(u.ru_utime.tv_usec + u.ru_stime.tv_usec);
}

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) != 0) { }
}
#endif

TASK(int, fib, int, n)
{
    if (n < 2) return n;
    SPAWN(fib, n - 1);
    int a = CALL(fib, n - 2);
    int b = SYNC(fib);
    return a + b;
}

static int run(unsigned int n_workers)
{
    lace_start(n_workers, 0);
    int r = RUN(fib, 20);
    if (r != 6765) {
        printf("fib(20) = %d with %u workers, expected 6765\n", r, n_workers);
        return 1;
    }
#if !defined(_WIN32)
    sleep_ms(50); /* let the workers reach their idle loop */
    double before = cpu_seconds();
    sleep_ms(500);
    double used = cpu_seconds() - before;
    printf("%u worker(s): %.3f s CPU while idle for 0.5 s\n", n_workers, used);
    if (used > 0.1) {
        printf("idle workers kept the CPU busy\n");
        return 1;
    }
#endif
    r = RUN(fib, 20); /* the workers wake up for new work */
    lace_stop();
    if (r != 6765) {
        printf("fib(20) = %d after idling, expected 6765\n", r);
        return 1;
    }
    return 0;
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    crash_handler_install();
    lace_set_verbosity(0);

#if !LACE_BACKOFF
    /* Without the backoff, idle workers spin by design */
    printf("test_idle: skipped (LACE_BACKOFF is off)\n");
    return 0;
#endif
    if (run(1) != 0) return 1;
    if (run(2) != 0) return 1;

    printf("test_idle: ok\n");
    return 0;
}
