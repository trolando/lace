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
 * Tests for the per-worker scratch arena (Lace v1).
 *
 * Exercises:
 *   - lace_scratch_alloc / mark / reset lifecycle
 *   - alignment of allocations
 *   - parent data survival across recursive CALL and SPAWN/SYNC
 *   - commit growth slow path under deep recursion
 *   - deep-backoff trim path
 *   - deep-backoff leak detection and auto-recovery
 *   - disabling scratch via lace_set_scratch_size(0)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <lace.h>

#if LACE_BACKOFF
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    static void sleep_ms(int ms) { Sleep(ms); }
#else
    #include <time.h>
    #include <unistd.h>
    static void sleep_ms(int ms) {
        struct timespec ts;
        ts.tv_sec = ms / 1000;
        ts.tv_nsec = (long)(ms % 1000) * 1000000L;
        nanosleep(&ts, NULL);
    }
#endif
#endif

/* --- Test harness ------------------------------------------------------- */

static int test_failures = 0;
static const char *current_test = "(none)";

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

TASK(void, basic_alloc_task)
{
    void *mark = LACE_SCRATCH_MARK();
    char *p = (char *)LACE_SCRATCH_ALLOC(1024);
    EXPECT(p != NULL);
    memset(p, 0x5A, 1024);
    for (int i = 0; i < 1024; i++) EXPECT_EQ((unsigned char)p[i], 0x5A);
    LACE_SCRATCH_RESET(mark);
}

static void test_basic_alloc(void) { RUN(basic_alloc_task); }

/* --- 2. Alignment ------------------------------------------------------- */

TASK(void, alignment_task)
{
    void *mark = LACE_SCRATCH_MARK();
    static const size_t sizes[] = {1, 7, 8, 15, 16, 17, 31, 33, 100, 4097};
    const size_t n = sizeof sizes / sizeof sizes[0];
    for (size_t i = 0; i < n; i++) {
        void *p = LACE_SCRATCH_ALLOC(sizes[i]);
        EXPECT(p != NULL);
        EXPECT_EQ((uintptr_t)p & 15u, 0u);
    }
    LACE_SCRATCH_RESET(mark);
}

static void test_alignment(void) { RUN(alignment_task); }

/* --- 2b. Interleaved different-typed allocations ------------------------ */
/*
 * Mirrors the typical Sylvan pattern: allocate several arrays of
 * different element types in the same task body. Each result must be
 * 16-byte aligned regardless of the previous allocation's size.
 */

TASK(void, interleaved_alloc_task)
{
    void *mark = LACE_SCRATCH_MARK();

    uint8_t *bytes = LACE_SCRATCH_ARRAY(uint8_t, 3);
    EXPECT(bytes != NULL);
    EXPECT_EQ((uintptr_t)bytes & 15u, 0u);
    bytes[0] = 0xAA; bytes[1] = 0xBB; bytes[2] = 0xCC;

    uint32_t *vars = LACE_SCRATCH_ARRAY(uint32_t, 5);
    EXPECT(vars != NULL);
    EXPECT_EQ((uintptr_t)vars & 15u, 0u);
    for (int i = 0; i < 5; i++) vars[i] = (uint32_t)(0xDEADBEEF + i);

    /* The 3-byte allocation must not overlap the next one. */
    EXPECT((char *)vars - (char *)bytes >= 16);

    uint64_t *bdds = LACE_SCRATCH_ARRAY(uint64_t, 7);
    EXPECT(bdds != NULL);
    EXPECT_EQ((uintptr_t)bdds & 15u, 0u);
    for (int i = 0; i < 7; i++) bdds[i] = 0x1122334455667788ULL + (uint64_t)i;

    /* All three arrays should still have their data intact. */
    EXPECT_EQ(bytes[0], 0xAA);
    EXPECT_EQ(bytes[2], 0xCC);
    EXPECT_EQ(vars[0], 0xDEADBEEFu);
    EXPECT_EQ(vars[4], 0xDEADBEEFu + 4u);
    EXPECT_EQ(bdds[0], 0x1122334455667788ULL);
    EXPECT_EQ(bdds[6], 0x1122334455667788ULL + 6ULL);

    LACE_SCRATCH_RESET(mark);
}

static void test_interleaved_alloc(void) { RUN(interleaved_alloc_task); }

/* --- 3. mark/reset roundtrip ------------------------------------------- */

TASK(void, markreset_task)
{
    char *before = __lace_worker->scratch_top;

    void *mark = LACE_SCRATCH_MARK();
    char *a = (char *)LACE_SCRATCH_ALLOC(1024);
    EXPECT(a != NULL);
    memset(a, 0x11, 1024);

    char *b = (char *)LACE_SCRATCH_ALLOC(2048);
    EXPECT(b != NULL);
    memset(b, 0x22, 2048);

    LACE_SCRATCH_RESET(mark);
    EXPECT_EQ(__lace_worker->scratch_top, (char *)mark);

    /* After reset, the next allocation should reuse the same address. */
    char *c = (char *)LACE_SCRATCH_ALLOC(512);
    EXPECT_EQ((uintptr_t)c, (uintptr_t)a);

    LACE_SCRATCH_RESET(mark);
    EXPECT_EQ(__lace_worker->scratch_top, before);
}

static void test_markreset(void) { RUN(markreset_task); }

/* --- 4. Recursive task body with explicit mark/reset -------------------- */

TASK(int, recursive_task, int, depth)
{
    if (depth == 0) return 0;
    void *mark = LACE_SCRATCH_MARK();

    /* 64 KB per frame; 200 deep = 12.5 MB total at peak. */
    char *buf = (char *)LACE_SCRATCH_ALLOC(64 * 1024);
    EXPECT(buf != NULL);
    memset(buf, (char)depth, 64 * 1024);

    int sub = CALL(recursive_task, depth - 1);

    if ((unsigned char)buf[0] != (unsigned char)depth) {
        LACE_SCRATCH_RESET(mark);
        return -1;
    }
    if ((unsigned char)buf[64 * 1024 - 1] != (unsigned char)depth) {
        LACE_SCRATCH_RESET(mark);
        return -2;
    }

    LACE_SCRATCH_RESET(mark);
    return sub + 1;
}

static void test_recursive(void)
{
    int r = (int)(intptr_t)RUN(recursive_task, 200);
    EXPECT_EQ(r, 200);
}

/* --- 5. SPAWN/SYNC with scratch usage in children ----------------------- */

TASK(int, sum_task, int, n)
{
    if (n <= 1) {
        void *mark = LACE_SCRATCH_MARK();
        int *p = (int *)LACE_SCRATCH_ALLOC(16 * sizeof(int));
        p[0] = n;
        int r = p[0];
        LACE_SCRATCH_RESET(mark);
        return r;
    }
    int mid = n / 2;
    SPAWN(sum_task, mid);
    int right = CALL(sum_task, n - mid);
    int left = SYNC(sum_task);

    void *mark = LACE_SCRATCH_MARK();
    int *check = (int *)LACE_SCRATCH_ALLOC(64);
    check[0] = left + right;
    int r = check[0];
    LACE_SCRATCH_RESET(mark);
    return r;
}

static void test_spawn_sync(void)
{
    int total = (int)(intptr_t)RUN(sum_task, 1024);
    EXPECT_EQ(total, 1024);
}

/* --- 6. Deep-backoff trim + leak detection ----------------------------- */

TASK(void, burn_scratch, int, mb)
{
    void *mark = LACE_SCRATCH_MARK();
    size_t bytes = (size_t)mb * 1024 * 1024;
    char *buf = (char *)LACE_SCRATCH_ALLOC(bytes);
    EXPECT(buf != NULL);
    for (size_t i = 0; i < bytes; i += 4096) buf[i] = (char)i;
    LACE_SCRATCH_RESET(mark);
}

#if LACE_BACKOFF && defined(__linux__)
static long read_rss_kb(void)
{
    FILE *f = fopen("/proc/self/status", "r");
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
#if !LACE_BACKOFF
    printf("    [trim] LACE_BACKOFF disabled; smoke test only.\n");
    RUN(burn_scratch, 8);
    return;
#else

#if defined(__linux__)
    long rss_baseline = read_rss_kb();
#endif

    RUN(burn_scratch, 32);

#if defined(__linux__)
    long rss_peak = read_rss_kb();
#endif

    /* Sleep long enough that workers reach deep backoff (>1000 attempts)
     * and trim their arenas. v1 deep backoff caps at 1 ms per sleep,
     * so 200 ms is plenty. */
    sleep_ms(200);

#if defined(__linux__)
    long rss_after_idle = read_rss_kb();
    if (rss_baseline > 0 && rss_peak > 0 && rss_after_idle > 0) {
        long grew_by = rss_peak - rss_baseline;
        long shrunk_by = rss_peak - rss_after_idle;
        printf("    [trim] RSS: baseline=%ld KB, peak=%ld KB (+%ld), "
               "after idle=%ld KB (-%ld)\n",
               rss_baseline, rss_peak, grew_by, rss_after_idle, shrunk_by);
        if (grew_by > 4 * 1024) {
            EXPECT(shrunk_by > grew_by / 2);
        }
    }
#else
    printf("    [trim] RSS check skipped on this platform; smoke test only.\n");
#endif

#endif /* LACE_BACKOFF */
}

/* --- 7. Leak detection on idle ----------------------------------------- */

TASK(void, leaky_task)
{
    /* Mark, alloc, return WITHOUT reset. */
    (void)LACE_SCRATCH_MARK();
    char *buf = (char *)LACE_SCRATCH_ALLOC(4096);
    EXPECT(buf != NULL);
    memset(buf, 0xCC, 4096);
    /* deliberately no reset */
}

static void test_leak_detection(void)
{
#if !LACE_BACKOFF
    printf("    [leak] LACE_BACKOFF disabled; skipping.\n");
    return;
#else
    printf("    [leak] A 'Lace warning' below is expected:\n");
    RUN(leaky_task);
    sleep_ms(200);
    /* After recovery, a subsequent task should see a fresh arena. */
    RUN(basic_alloc_task);
#endif
}

/* --- 8. Reservation disabled (scratch off) ----------------------------- */

TASK(int, no_scratch_fib, int, n)
{
    if (n < 2) return n;
    SPAWN(no_scratch_fib, n - 1);
    int a = CALL(no_scratch_fib, n - 2);
    int b = SYNC(no_scratch_fib);
    return a + b;
}

static int test_scratch_disabled(void)
{
    current_test = "test_scratch_disabled";
    lace_set_scratch_size(0);
    lace_start(2, 0);
    int r = (int)(intptr_t)RUN(no_scratch_fib, 15);
    lace_stop();
    EXPECT_EQ(r, 610);  /* fib(15) = 610 */
    return r == 610 ? 0 : 1;
}

/* --- main --------------------------------------------------------------- */

int main(void)
{
    printf("test_scratch: per-worker scratch arena tests\n");

    /* Modest sizes friendly to CI and 32-bit systems. */
    lace_set_scratch_size(64ULL * 1024 * 1024);
    lace_set_scratch_band(64 * 1024);

    lace_start(2, 0);

    RUN_TEST(test_basic_alloc);
    RUN_TEST(test_alignment);
    RUN_TEST(test_interleaved_alloc);
    RUN_TEST(test_markreset);
    RUN_TEST(test_recursive);
    RUN_TEST(test_spawn_sync);
    RUN_TEST(test_trim_on_idle);
    RUN_TEST(test_leak_detection);

    lace_stop();

    test_scratch_disabled();

    if (test_failures == 0) {
        printf("test_scratch: all tests passed\n");
        return 0;
    } else {
        printf("test_scratch: %d failure(s)\n", test_failures);
        return 1;
    }
}
