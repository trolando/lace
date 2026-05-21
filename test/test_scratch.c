/*
 * Copyright 2026 Tom van Dijk, Formal Methods and Tools, University of Twente
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

 /**
  * @file
  * @brief Tests for the per-worker scratch arena.
  *
  * Exercises the public scratch API end-to-end:
  *   - lace_scratch_alloc() basic correctness and alignment
  *   - automatic save/restore inserted by the NAME_CALL wrapper
  *   - independent scratch frames for parent vs. child tasks
  *   - deep recursion that exercises the commit-growth slow path
  *   - manual lace_scratch_mark()/lace_scratch_reset()
  *   - deep-idle trim path (Linux: RSS check; elsewhere: smoke test only)
  *   - work-stealing with scratch usage on stolen tasks
  *
  * Each test returns 0 on success, non-zero on failure. main() aggregates.
  */

#include <lace.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#if defined(__linux__)
#  include <stdio.h>
#endif

  /* --- Test harness ------------------------------------------------------- */

static int test_failures = 0;
static const char* current_test = "(none)";

#define EXPECT(cond)                                                    \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "  FAIL [%s] %s:%d: %s\n",                  \
                    current_test, __FILE__, __LINE__, #cond);           \
            test_failures++;                                            \
        }                                                               \
    } while (0)

#define EXPECT_EQ(a, b)                                                 \
    do {                                                                \
        long long _va = (long long)(a);                                 \
        long long _vb = (long long)(b);                                 \
        if (_va != _vb) {                                               \
            fprintf(stderr,                                             \
                    "  FAIL [%s] %s:%d: %s == %s (got %lld vs %lld)\n", \
                    current_test, __FILE__, __LINE__, #a, #b, _va, _vb);\
            test_failures++;                                            \
        }                                                               \
    } while (0)

#define RUN_TEST(name) do { current_test = #name; name(); } while (0)

/* --- 1. Basic alloc + write + read -------------------------------------- */

VOID_TASK_0(basic_alloc_task)
void basic_alloc_task_IMPL(lace_worker* lw)
{
    char* p = (char*)lace_scratch_alloc(lw, 1024);
    EXPECT(p != NULL);
    memset(p, 0x5A, 1024);
    for (int i = 0; i < 1024; i++) EXPECT_EQ((unsigned char)p[i], 0x5A);
}

static void test_basic_alloc(void)
{
    basic_alloc_task();
}

/* --- 2. Alignment ------------------------------------------------------- */

VOID_TASK_0(alignment_task)
void alignment_task_IMPL(lace_worker* lw)
{
    /* Allocations should be 16-byte aligned regardless of requested size. */
    static const size_t sizes[] = { 1, 7, 8, 15, 16, 17, 31, 33, 100, 4097 };
    const size_t n = sizeof sizes / sizeof sizes[0];
    for (size_t i = 0; i < n; i++) {
        void* p = lace_scratch_alloc(lw, sizes[i]);
        EXPECT(p != NULL);
        EXPECT_EQ((uintptr_t)p & 15u, 0u);
    }
}

static void test_alignment(void)
{
    alignment_task();
}

/* --- 3. Automatic reset at task boundary -------------------------------- */
/*
 * After a task body returns, the framework's _CALL wrapper restores
 * scratch_top to its pre-call value. We verify this by checking the
 * worker's scratch_top before and after calling a task that allocates.
 */

VOID_TASK_0(allocates_a_lot)
void allocates_a_lot_IMPL(lace_worker* lw)
{
    /* Allocate ~256 KB, force pages to actually commit. */
    char* p = (char*)lace_scratch_alloc(lw, 256 * 1024);
    EXPECT(p != NULL);
    memset(p, 0xAA, 256 * 1024);
}

VOID_TASK_0(autoreset_task)
void autoreset_task_IMPL(lace_worker* lw)
{
    char* before = lw->scratch_top;
    allocates_a_lot_CALL(lw);
    char* after = lw->scratch_top;
    /* The inner task allocated 256 KB, but its CALL wrapper restored top. */
    EXPECT_EQ(before, after);
}

static void test_autoreset(void)
{
    autoreset_task();
}

/* --- 4. Parent data survives child invocations -------------------------- */
/*
 * A pointer obtained in a parent task body must remain valid across
 * recursive _CALL and _SPAWN/_SYNC of child tasks. The child gets a
 * fresh scratch frame on top of the parent's; on return, the child's
 * scratch is freed but the parent's is preserved.
 */

VOID_TASK_1(child_consumer, int*, dummy)
void child_consumer_IMPL(lace_worker* lw, int* dummy)
{
    (void)dummy;
    /* Allocate some scratch of our own; this should NOT clobber the
     * parent's previously-allocated buffer. */
    int* mine = (int*)lace_scratch_alloc(lw, 4096 * sizeof(int));
    for (int i = 0; i < 4096; i++) mine[i] = -1;
}

TASK_1(int, parent_with_child, int, n)
int parent_with_child_IMPL(lace_worker* lw, int n)
{
    /* Allocate before spawning/calling children. */
    int* buf = (int*)lace_scratch_alloc(lw, (size_t)n * sizeof(int));
    EXPECT(buf != NULL);
    for (int i = 0; i < n; i++) buf[i] = i * 7 + 3;

    /* Recursive CALL — child has its own scratch frame. */
    child_consumer_CALL(lw, buf);

    /* SPAWN + SYNC: child may run locally OR be stolen by another worker. */
    child_consumer_SPAWN(lw, buf);
    child_consumer_SYNC(lw);

    /* Parent data must be intact. */
    int ok = 1;
    for (int i = 0; i < n; i++) {
        if (buf[i] != i * 7 + 3) { ok = 0; break; }
    }
    return ok;
}

static void test_parent_survives_child(void)
{
    int ok = parent_with_child(1000);
    EXPECT_EQ(ok, 1);
}

/* --- 5. Deep recursion exercises the grow path -------------------------- */
/*
 * Recurse 200 levels deep, allocating 64 KB per frame. Total peak is
 * ~12.5 MB per worker, comfortably above the default 1 MB band, so
 * lace_scratch_grow() gets called repeatedly. At each level we mark
 * our buffer with a sentinel and verify it survives the recursive call.
 */

TASK_1(int, deep_recursion, int, depth)
int deep_recursion_IMPL(lace_worker* lw, int depth)
{
    if (depth == 0) return 0;
    char* buf = (char*)lace_scratch_alloc(lw, 64 * 1024);
    EXPECT(buf != NULL);
    memset(buf, (char)depth, 64 * 1024);

    int sub = deep_recursion_CALL(lw, depth - 1);

    /* After the recursive call returned, our buffer must still be ours. */
    if ((unsigned char)buf[0] != (unsigned char)depth) return -1;
    if ((unsigned char)buf[64 * 1024 - 1] != (unsigned char)depth) return -2;
    return sub + 1;
}

static void test_deep_recursion(void)
{
    int r = deep_recursion(200);
    EXPECT_EQ(r, 200);
}

/* --- 6. Many parallel spawns with scratch usage ------------------------- */
/*
 * Stress: do a wide SPAWN tree. Many tasks will be stolen by other
 * workers, each starting a fresh scratch frame on its worker. This
 * doesn't directly verify steal occurred, but it does verify that
 * scratch usage from many concurrent tasks doesn't corrupt anything.
 */

TASK_1(int, sum_task, int, n)
int sum_task_IMPL(lace_worker* lw, int n)
{
    if (n <= 1) {
        /* Allocate a small scratch to make sure leaves use it. */
        int* p = (int*)lace_scratch_alloc(lw, 16 * sizeof(int));
        p[0] = n;
        return p[0];
    }
    int mid = n / 2;
    sum_task_SPAWN(lw, mid);
    int right = sum_task_CALL(lw, n - mid);
    int left = sum_task_SYNC(lw);
    /* Allocate AFTER the children — must not be clobbered by their frames. */
    int* check = (int*)lace_scratch_alloc(lw, 64);
    check[0] = left + right;
    return check[0];
}

static void test_parallel_spawn(void)
{
    int total = sum_task(1024);
    EXPECT_EQ(total, 1024);
}

/* --- 7. Manual mark/reset (advanced API) -------------------------------- */

VOID_TASK_0(manual_markreset)
void manual_markreset_IMPL(lace_worker* lw)
{
    void* mark = lace_scratch_mark(lw);
    char* a = (char*)lace_scratch_alloc(lw, 1024);
    EXPECT(a != NULL);
    memset(a, 0x11, 1024);

    /* Allocate more, then reset back to the mark — second allocation
     * is freed, first is also freed (mark was taken before it). */
    char* b = (char*)lace_scratch_alloc(lw, 2048);
    EXPECT(b != NULL);
    memset(b, 0x22, 2048);

    lace_scratch_reset(lw, mark);
    EXPECT_EQ(lw->scratch_top, (char*)mark);

    /* New allocation after reset should reuse the same address as `a`. */
    char* c = (char*)lace_scratch_alloc(lw, 512);
    EXPECT_EQ((uintptr_t)c, (uintptr_t)a);
}

static void test_manual_markreset(void)
{
    manual_markreset();
}

/* --- 8. Deep-idle trim (smoke test + RSS check on Linux) ---------------- */

VOID_TASK_1(burn_scratch, int, mb)
void burn_scratch_IMPL(lace_worker* lw, int mb)
{
    size_t bytes = (size_t)mb * 1024 * 1024;
    char* buf = (char*)lace_scratch_alloc(lw, bytes);
    EXPECT(buf != NULL);
    /* Touch every page to force the kernel to commit physical pages. */
    for (size_t i = 0; i < bytes; i += 4096) buf[i] = (char)i;
}

#if defined(__linux__)
static long read_rss_kb(void)
{
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long kb = -1;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%ld", &kb);
            break;
        }
    }
    fclose(f);
    return kb;
}
#endif

static void test_trim_on_idle(void)
{
#if defined(__linux__)
    long rss_baseline = read_rss_kb();
#endif

    /* Burn ~32 MB of scratch per worker. */
    burn_scratch(32);

#if defined(__linux__)
    long rss_peak = read_rss_kb();
#endif

    /* Sleep well past LACE_IDLE_FUTEX_TIMEOUT_MAX (1000us) so all
     * workers reach deep idle and trim their arenas. 200 ms is far
     * more than needed; keeps the test robust across slow CI. */
    lace_sleep_us(200 * 1000);

#if defined(__linux__)
    long rss_after_idle = read_rss_kb();
    if (rss_baseline > 0 && rss_peak > 0 && rss_after_idle > 0) {
        long grew_by = rss_peak - rss_baseline;
        long shrunk_by = rss_peak - rss_after_idle;
        printf("    [trim] RSS: baseline=%ld KB, peak=%ld KB (+%ld), "
            "after idle=%ld KB (-%ld)\n",
            rss_baseline, rss_peak, grew_by, rss_after_idle, shrunk_by);
        /* Expect at least 50% of the peak growth to be released. We
         * grew by ~32 MB per worker; trim should release essentially
         * all of it. Use a loose bound to tolerate CI noise. */
        if (grew_by > 4 * 1024) {  /* only check if growth was meaningful */
            EXPECT(shrunk_by > grew_by / 2);
        }
    }
#else
    printf("    [trim] RSS check skipped on this platform; "
        "smoke test only.\n");
#endif
}

/* --- 9. Reservation can be set to zero (scratch disabled) --------------- */
/*
 * Not strictly a scratch test, but we want to make sure that calling
 * lace_set_scratchsize(0) still produces a working Lace. Tasks that
 * don't call lace_scratch_alloc() should run fine with no arena.
 *
 * This test runs in its own lace_start/lace_stop cycle, so we keep
 * it separate from the rest.
 */

TASK_1(int, no_scratch_fib, int, n)
int no_scratch_fib_IMPL(lace_worker* lw, int n)
{
    if (n < 2) return n;
    no_scratch_fib_SPAWN(lw, n - 1);
    int a = no_scratch_fib_CALL(lw, n - 2);
    int b = no_scratch_fib_SYNC(lw);
    return a + b;
}

static int test_scratch_disabled(void)
{
    current_test = "test_scratch_disabled";
    lace_set_scratchsize(0);
    lace_start(2, 0, 0);
    int r = no_scratch_fib(15);
    lace_stop();
    EXPECT_EQ(r, 610);  /* fib(15) = 610 */
    return r == 610 ? 0 : 1;
}

/* --- main --------------------------------------------------------------- */

int main(void)
{
    printf("test_scratch: per-worker scratch arena tests\n");

    /* Use modest sizes so the test is friendly to CI: 256 MB reservation
     * per worker (cheap on 64-bit, since nothing commits until used) and
     * a small 64 KB band so the grow/trim paths get exercised. */
    lace_set_scratchsize(256ULL * 1024 * 1024);
    lace_set_scratch_band(64 * 1024);

    lace_start(2, 0, 0);

    RUN_TEST(test_basic_alloc);
    RUN_TEST(test_alignment);
    RUN_TEST(test_autoreset);
    RUN_TEST(test_parent_survives_child);
    RUN_TEST(test_deep_recursion);
    RUN_TEST(test_parallel_spawn);
    RUN_TEST(test_manual_markreset);
    RUN_TEST(test_trim_on_idle);

    lace_stop();

    /* test_scratch_disabled runs its own lace_start/lace_stop. */
    test_scratch_disabled();

    if (test_failures == 0) {
        printf("test_scratch: all tests passed\n");
        return 0;
    }
    else {
        printf("test_scratch: %d failure(s)\n", test_failures);
        return 1;
    }
}