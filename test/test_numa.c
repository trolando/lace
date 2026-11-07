#if defined(__linux__)
#define _GNU_SOURCE /* syscall; before any header */
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lace.h>

#include "test_crash_handler.h"

/*
 * Where the memory of the workers lands on a NUMA machine. Lace pins a worker
 * before it touches its memory, so first touch puts its data, its deque and
 * its stack on the node of its CPU. When Lace pinned after setting up a
 * worker (Lace 1.8.2 and older), half of the workers of a two-socket machine
 * had their data on the other node, which no benchmark showed. Every worker gets one CPU
 * (LACE_PIN_CPU) and checks its pages with move_pages, which only reports
 * their nodes. The top of a stack, where pthread_create puts the thread
 * descriptor and the TLS, is written by the thread that creates the worker,
 * before the worker pins itself (on a two-socket machine half of these pages
 * were on the other node), so the stack page checked is one the worker writes
 * 64 KB further down. Linux with two or more NUMA nodes that the process may
 * use; elsewhere this only checks that Lace runs.
 */

#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>

#define N_PAGES 4
static const char *page_names[N_PAGES] = { "worker data", "public data", "deque", "stack" };
static int *cpu_node;  // per worker: the node of its CPU, -1 if unknown
static int *page_node; // per worker, N_PAGES each: the node of the page, or -errno

/* A one-line file into buf without the newline, or "" */
static void
read_line(const char *path, const char *prefix, char *buf, size_t size)
{
    buf[0] = '\0';
    FILE *f = fopen(path, "r");
    if (f == NULL) return;
    char line[1024];
    while (fgets(line, sizeof(line), f) != NULL) {
        if (prefix == NULL || strncmp(line, prefix, strlen(prefix)) == 0) {
            const char *v = line + (prefix != NULL ? strlen(prefix) : 0);
            while (*v == ' ' || *v == '\t') v++;
            snprintf(buf, size, "%s", v);
            buf[strcspn(buf, "\n")] = '\0';
            break;
        }
    }
    fclose(f);
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

TASK(void, locate)
{
#if defined(__linux__)
    volatile char on_stack[1 << 16]; // its first byte 64 KB below this frame
    on_stack[0] = 1;
    void *slot = __lace_dq_head;
    SPAWN(fib, 2); // writes the deque slot
    const long ps = sysconf(_SC_PAGESIZE);
    void *pages[N_PAGES] = { __lace_worker, __lace_worker->_public, slot, (void *)&on_stack[0] };
    for (int i = 0; i < N_PAGES; i++) pages[i] = (void *)((uintptr_t)pages[i] & ~(uintptr_t)(ps - 1));
    int status[N_PAGES];
    const unsigned id = (unsigned)LACE_WORKER_ID;
    if (syscall(SYS_move_pages, 0, (unsigned long)N_PAGES, pages, NULL, status, 0) != 0) {
        for (int i = 0; i < N_PAGES; i++) status[i] = -1;
    }
    unsigned cpu = 0, node = 0;
    cpu_node[id] = syscall(SYS_getcpu, &cpu, &node, NULL) == 0 ? (int)node : -1;
    for (int i = 0; i < N_PAGES; i++) page_node[id * N_PAGES + (unsigned)i] = status[i];
#endif
    (void)SYNC(fib);
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    crash_handler_install();
    lace_set_verbosity(0);

    int check = 0;
#if defined(__linux__)
    char online[256], allowed[256];
    read_line("/sys/devices/system/node/online", NULL, online, sizeof(online));
    read_line("/proc/self/status", "Mems_allowed_list:", allowed, sizeof(allowed));
    check = strchr(online, '-') != NULL || strchr(online, ',') != NULL; // more than one node
    if (check && strcmp(online, allowed) != 0) {
        printf("NUMA nodes %s, but the process may use %s: not checking\n", online, allowed);
        check = 0;
    }
    if (!check) printf("one NUMA node (%s): not checking\n", online[0] != '\0' ? online : "unknown");
    else printf("NUMA nodes %s\n", online);
#endif

    lace_set_pinning(LACE_PIN_CPU);
    lace_start(0, 0);
    const unsigned n = lace_workers();
#if defined(__linux__)
    cpu_node = calloc(n, sizeof(int));
    page_node = calloc((size_t)n * N_PAGES, sizeof(int));
    if (cpu_node == NULL || page_node == NULL) return 1;
    TOGETHER(locate);
#endif
    int r = RUN(fib, 20);
    lace_stop();
    if (r != 6765) {
        printf("fib(20) = %d, expected 6765\n", r);
        return 1;
    }

    int failures = 0;
#if defined(__linux__)
    if (check) {
        int counted = 0;
        for (unsigned w = 0; w < n; w++) {
            if (cpu_node[w] < 0) continue;
            counted++;
            for (unsigned i = 0; i < N_PAGES; i++) {
                const int p = page_node[w * N_PAGES + i];
                if (p == cpu_node[w]) continue;
                if (p < 0) printf("worker %u: its %s page is not known (%d)\n", w, page_names[i], p);
                else printf("worker %u runs on node %d, its %s on node %d\n", w, cpu_node[w], page_names[i], p);
                failures++;
            }
        }
        printf("%u workers, %d checked\n", n, counted);
    }
    free(cpu_node);
    free(page_node);
#endif
    (void)check;
    if (failures != 0) {
        printf("test_numa: %d pages on another node\n", failures);
        return 1;
    }
    printf("test_numa: ok (%u workers)\n", n);
    return 0;
}
