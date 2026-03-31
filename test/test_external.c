/**
 * test_external.c
 *
 * Stress test for lace_run_task called from multiple non-Lace threads
 * concurrently. Exercises the fixed-size external task array under
 * contention.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

#include <lace.h>

#if LACE_MSVC
#include <process.h>
#else
#include <pthread.h>
#endif

#define N_EXTERNAL_THREADS 8
#define TASKS_PER_THREAD   10000

 /* Shared atomic counter incremented by each task */
static atomic_int global_counter = 0;

/* A simple task that atomically increments the counter */
TASK(int, increment_task, int, value)
{
    atomic_fetch_add_explicit(&global_counter, value, memory_order_relaxed);
    return value;
}

/* A task that does some actual work (fibonacci) to exercise stealing */
TASK(int, fib_task, int, n)
{
    if (n < 2) return n;
    SPAWN(fib_task, n - 1);
    int a = CALL(fib_task, n - 2);
    int b = SYNC(fib_task);
    return a + b;
}

/* Thread function: submits TASKS_PER_THREAD tasks via lace_run_task */
static void* external_thread_simple(void* arg)
{
    int thread_id = (int)(size_t)arg;
    (void)thread_id;

    for (int i = 0; i < TASKS_PER_THREAD; i++) {
        int result = RUN(increment_task, 1);
        if (result != 1) {
            fprintf(stderr, "Thread %d: unexpected result %d\n", thread_id, result);
            exit(1);
        }
    }
    return NULL;
}

/* Thread function: submits fibonacci tasks that generate internal parallelism */
static void* external_thread_fib(void* arg)
{
    int thread_id = (int)(size_t)arg;
    (void)thread_id;

    for (int i = 0; i < 100; i++) {
        int result = RUN(fib_task, 20);
        if (result != 6765) {
            fprintf(stderr, "Thread %d: fib(20) = %d, expected 6765\n", thread_id, result);
            exit(1);
        }
    }
    return NULL;
}

static void test_concurrent_simple(void)
{
    printf("Test: %d threads x %d simple tasks...\n", N_EXTERNAL_THREADS, TASKS_PER_THREAD);

    atomic_store_explicit(&global_counter, 0, memory_order_relaxed);

#if LACE_MSVC
    HANDLE threads[N_EXTERNAL_THREADS];
    for (int i = 0; i < N_EXTERNAL_THREADS; i++) {
        unsigned tid;
        threads[i] = (HANDLE)_beginthreadex(NULL, 0,
            (unsigned(__stdcall*)(void*))external_thread_simple,
            (void*)(size_t)i, 0, &tid);
        if (threads[i] == 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            exit(1);
        }
    }
    for (int i = 0; i < N_EXTERNAL_THREADS; i++) {
        WaitForSingleObject(threads[i], INFINITE);
        CloseHandle(threads[i]);
    }
#else
    pthread_t threads[N_EXTERNAL_THREADS];
    for (int i = 0; i < N_EXTERNAL_THREADS; i++) {
        int rc = pthread_create(&threads[i], NULL, external_thread_simple, (void*)(size_t)i);
        if (rc != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            exit(1);
        }
    }
    for (int i = 0; i < N_EXTERNAL_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
#endif

    int expected = N_EXTERNAL_THREADS * TASKS_PER_THREAD;
    int actual = atomic_load_explicit(&global_counter, memory_order_relaxed);
    printf("Counter: %d (expected %d)\n", actual, expected);
    if (actual != expected) {
        fprintf(stderr, "FAIL: counter mismatch!\n");
        exit(1);
    }
    printf("PASS\n\n");
}

static void test_concurrent_fib(void)
{
    printf("Test: %d threads x 100 fib(20) tasks...\n", N_EXTERNAL_THREADS);

#if LACE_MSVC
    HANDLE threads[N_EXTERNAL_THREADS];
    for (int i = 0; i < N_EXTERNAL_THREADS; i++) {
        unsigned tid;
        threads[i] = (HANDLE)_beginthreadex(NULL, 0,
            (unsigned(__stdcall*)(void*))external_thread_fib,
            (void*)(size_t)i, 0, &tid);
        if (threads[i] == 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            exit(1);
        }
    }
    for (int i = 0; i < N_EXTERNAL_THREADS; i++) {
        WaitForSingleObject(threads[i], INFINITE);
        CloseHandle(threads[i]);
    }
#else
    pthread_t threads[N_EXTERNAL_THREADS];
    for (int i = 0; i < N_EXTERNAL_THREADS; i++) {
        int rc = pthread_create(&threads[i], NULL, external_thread_fib, (void*)(size_t)i);
        if (rc != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            exit(1);
        }
    }
    for (int i = 0; i < N_EXTERNAL_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
#endif

    printf("PASS\n\n");
}

/* Test that mixing external and internal tasks works */
TASK(void, internal_work, int, depth)
{
    if (depth > 0) {
        SPAWN(internal_work, depth - 1);
        SPAWN(internal_work, depth - 1);
        SYNC(internal_work);
        SYNC(internal_work);
    }
    else {
        atomic_fetch_add_explicit(&global_counter, 1, memory_order_relaxed);
    }
}

static void* external_thread_mixed(void* arg)
{
    (void)arg;
    for (int i = 0; i < 100; i++) {
        RUN(internal_work, 8);
    }
    return NULL;
}

static void test_mixed(void)
{
    printf("Test: %d threads submitting tree-recursive tasks...\n", N_EXTERNAL_THREADS);

    atomic_store_explicit(&global_counter, 0, memory_order_relaxed);

#if LACE_MSVC
    HANDLE threads[N_EXTERNAL_THREADS];
    for (int i = 0; i < N_EXTERNAL_THREADS; i++) {
        unsigned tid;
        threads[i] = (HANDLE)_beginthreadex(NULL, 0,
            (unsigned(__stdcall*)(void*))external_thread_mixed,
            (void*)(size_t)i, 0, &tid);
        if (threads[i] == 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            exit(1);
        }
    }
    for (int i = 0; i < N_EXTERNAL_THREADS; i++) {
        WaitForSingleObject(threads[i], INFINITE);
        CloseHandle(threads[i]);
    }
#else
    pthread_t threads[N_EXTERNAL_THREADS];
    for (int i = 0; i < N_EXTERNAL_THREADS; i++) {
        int rc = pthread_create(&threads[i], NULL, external_thread_mixed, (void*)(size_t)i);
        if (rc != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            exit(1);
        }
    }
    for (int i = 0; i < N_EXTERNAL_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
#endif

    /* Each internal_work(8) produces 2^8 = 256 leaf increments */
    /* 8 threads x 100 calls x 256 leaves = 204800 */
    int expected = N_EXTERNAL_THREADS * 100 * 256;
    int actual = atomic_load_explicit(&global_counter, memory_order_relaxed);
    printf("Counter: %d (expected %d)\n", actual, expected);
    if (actual != expected) {
        fprintf(stderr, "FAIL: counter mismatch!\n");
        exit(1);
    }
    printf("PASS\n\n");
}

int main(int argc, char* argv[])
{
    int n_workers = 0;
    if (argc > 1) n_workers = atoi(argv[1]);

    lace_set_verbosity(1);
    lace_start(n_workers, 0);

    test_concurrent_simple();
    test_concurrent_fib();
    test_mixed();

    lace_stop();

    printf("All external task tests passed.\n");
    return 0;
}
