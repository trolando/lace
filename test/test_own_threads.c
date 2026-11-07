#if defined(__linux__)
#define _GNU_SOURCE /* sched_getaffinity, CPU_* macros; before any header */
#endif

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lace.h>

#include "test_crash_handler.h"

/*
 * Lace on the program's own threads (lace_start_external): the test creates
 * the threads, and each enters as a worker.
 *
 * - A root worker: thread 0 enters and generates the tasks itself (it only
 *   steals at its SYNCs); it also runs a TOGETHER task, which every worker,
 *   the root included, must run. The other threads only steal.
 * - A plain caller: all threads only steal, and the main thread calls in
 *   from outside, including lace_release_memory.
 * - Restarts: several runs in which the threads swap worker ids, with
 *   lace_stop called by the main thread or by the former root after leaving.
 * - Pinning (Linux): without a pinning choice, the threads keep their
 *   affinity although LACE_DEFAULT_PIN is AUTO; with LACE_PIN_CPU they are
 *   pinned while they are workers and get their mask back when they leave.
 */

#if defined(_WIN32)
#include <windows.h>
typedef HANDLE host_thread_t;
static DWORD WINAPI trampoline(LPVOID p);
static void thread_start(host_thread_t *t, void *arg)
{
    *t = CreateThread(NULL, 0, trampoline, arg, 0, NULL);
    if (*t == NULL) { printf("CreateThread failed\n"); exit(1); }
}
static void thread_join(host_thread_t t) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
#else
#include <pthread.h>
typedef pthread_t host_thread_t;
static void *trampoline(void *p);
static void thread_start(host_thread_t *t, void *arg)
{
    if (pthread_create(t, NULL, trampoline, arg) != 0) { printf("pthread_create failed\n"); exit(1); }
}
static void thread_join(host_thread_t t) { pthread_join(t, NULL); }
#endif

#if defined(__linux__)
#include <sched.h>
#endif

#define N 4
#define FIB_N 24
#define FIB_R 46368

TASK(int, fib, int, n)

int fib_CALL(lace_worker* lw, int n)
{
    if (n < 2) return n;
    fib_SPAWN(lw, n - 1);
    int a = fib_CALL(lw, n - 2);
    int b = fib_SYNC(lw);
    return a + b;
}

static atomic_int together_count;

TASK(void, count_together)

void count_together_CALL(lace_worker* lw)
{
    (void)lw;
    atomic_fetch_add(&together_count, 1);
}

static atomic_int failures;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); atomic_fetch_add(&failures, 1); } } while (0)

/* One run: how the threads take part */
static int run_no;          // shifts the worker ids between runs
static int has_root;        // worker 0 generates the tasks (else all threads steal)
static int root_stops;      // the root calls lace_stop after leaving
static int expect_pinned;   // lace_set_pinning was called before the run

static void
host_thread(int k)
{
    int id = (k + run_no) % N;
#if defined(__linux__)
    cpu_set_t before, during, after;
    CPU_ZERO(&before); CPU_ZERO(&during); CPU_ZERO(&after);
    (void)sched_getaffinity(0, sizeof(before), &before);
#endif
    lace_worker_enter((unsigned int)id);
#if defined(__linux__)
    (void)sched_getaffinity(0, sizeof(during), &during);
#endif
    CHECK(lace_worker_id() == id, "thread %d entered as worker %d, but is worker %d", k, id, lace_worker_id());
    if (has_root && id == 0) {
        int r = fib(FIB_N); // runs directly on worker 0
        CHECK(r == FIB_R, "fib(%d) = %d on the root worker, expected %d", FIB_N, r, FIB_R);
        atomic_store(&together_count, 0);
        count_together_TOGETHER();
        CHECK(atomic_load(&together_count) == N, "TOGETHER ran on %d workers, expected %d",
            atomic_load(&together_count), N);
        lace_worker_leave();
        if (root_stops) lace_stop();
    } else {
        lace_worker_loop();
        lace_worker_leave();
    }
    CHECK(lace_worker_id() == -1, "thread %d is still a worker after leaving", k);
#if defined(__linux__)
    (void)sched_getaffinity(0, sizeof(after), &after);
    CHECK(CPU_EQUAL(&before, &after), "thread %d did not get its affinity back", k);
    if (!expect_pinned) {
        CHECK(CPU_EQUAL(&before, &during), "worker %d was pinned without a pinning choice", id);
    } else {
        cpu_set_t both;
        CPU_AND(&both, &during, &before);
        CHECK(CPU_EQUAL(&both, &during), "worker %d was pinned outside its mask", id);
#if !LACE_USE_HWLOC
        if (CPU_COUNT(&before) >= N) {
            CHECK(CPU_COUNT(&during) == 1, "worker %d may run on %d CPUs, expected 1", id, CPU_COUNT(&during));
        }
#endif
    }
#endif
}

static int thread_ids[N];

#if defined(_WIN32)
static DWORD WINAPI trampoline(LPVOID p) { host_thread(*(int*)p); return 0; }
#else
static void *trampoline(void *p) { host_thread(*(int*)p); return NULL; }
#endif

static void
run(int run_number, int root, int root_calls_stop, int pinned)
{
    run_no = run_number;
    has_root = root;
    root_stops = root_calls_stop;
    expect_pinned = pinned;

    lace_start_external(N, 0);
    CHECK(lace_worker_count() == N, "lace_worker_count() = %u, expected %d", lace_worker_count(), N);

    host_thread_t threads[N];
    for (int k = 0; k < N; k++) {
        thread_ids[k] = k;
        thread_start(&threads[k], &thread_ids[k]);
    }

    if (root) {
        if (!root_calls_stop) {
            // wait for the root to finish, then stop from this thread
            int root_thread = (N - run_number % N) % N;
            thread_join(threads[root_thread]);
            lace_stop();
            for (int k = 0; k < N; k++) if (k != root_thread) thread_join(threads[k]);
        } else {
            for (int k = 0; k < N; k++) thread_join(threads[k]);
        }
    } else {
        // this thread is not a worker: it calls in from outside
        int r = fib(FIB_N);
        CHECK(r == FIB_R, "fib(%d) = %d from outside, expected %d", FIB_N, r, FIB_R);
        lace_release_memory();
        lace_stop();
        for (int k = 0; k < N; k++) thread_join(threads[k]);
    }
    CHECK(!lace_is_running(), "Lace still runs after lace_stop");
    printf("run %d (%s, stop by %s%s): %s\n", run_number, root ? "root worker" : "plain caller",
        root && root_calls_stop ? "the root" : "the main thread", pinned ? ", pinned" : "",
        atomic_load(&failures) == 0 ? "ok" : "FAILED");
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    crash_handler_install();
    lace_set_verbosity(0);

    run(0, 1, 0, 0); // root worker, the main thread stops
    run(1, 1, 1, 0); // other worker ids, the former root stops
    run(2, 0, 0, 0); // plain caller
    run(3, 1, 0, 0); // and once more with other ids
    lace_set_pinning(LACE_PIN_CPU);
    run(1, 1, 0, 1); // pinned while they are workers

    if (atomic_load(&failures) != 0) {
        printf("test_own_threads: %d failures\n", atomic_load(&failures));
        return 1;
    }
    printf("test_own_threads: ok\n");
    return 0;
}
