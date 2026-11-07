/*
 * Copyright 2013-2016 Formal Methods and Tools, University of Twente
 * Copyright 2016-2018 Tom van Dijk, Johannes Kepler University Linz
 * Copyright 2019-2026 Tom van Dijk, Formal Methods and Tools, University of Twente
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* For sched_getaffinity and CPU_COUNT. __GLIBC__ alone does not work here:
 * it comes from <features.h>, which no header has included yet. */
#if defined(__linux__) || defined(__GLIBC__)
    #ifndef _GNU_SOURCE
       #define _GNU_SOURCE
    #endif
#endif

#include <lace.h>

#include <errno.h> // for errno
#include <inttypes.h> // for PRIu64
#include <limits.h> // for INT_MAX
#include <stddef.h> // for size_t
#include <stdio.h>  // for fprintf
#include <stdlib.h> // for memalign, malloc
#include <string.h> // for memset
#include <time.h> // for clock_gettime

/*
 * Constants of the leapfrogging and idle policies; docs/scheduling-experiments.rst
 * reports the measurements behind each value.
 */
#ifndef LACE_LEAP_HOPS /* How far a leapfrogging worker follows the chain of workers that wait for each other */
#define LACE_LEAP_HOPS 4
#endif

#ifndef LACE_IDLE_SENTINELS /* Idle workers kept awake (napping) while some worker runs a task */
#define LACE_IDLE_SENTINELS 2
#endif

#ifndef LACE_SENTINEL_NAP_US /* A sentinel naps this long between its rounds */
#define LACE_SENTINEL_NAP_US 20
#endif

#ifndef LACE_SENTINEL_SPIN_US /* Right after it became a sentinel, a worker spins this long instead of napping */
#define LACE_SENTINEL_SPIN_US 200
#endif

#ifndef LACE_SENTINEL_GRACE_US /* A sentinel retires after no worker ran a task this long */
#define LACE_SENTINEL_GRACE_US 10000
#endif

#ifndef LACE_IDLE_SHALLOW_US /* For this long after it ran a task, a sleeping worker keeps the shortest timeout */
#define LACE_IDLE_SHALLOW_US 20000
#endif

#ifndef LACE_IDLE_STEAL_MIN_X /* A stolen task shorter than this many times the average steal cost counts as a failed steal */
#define LACE_IDLE_STEAL_MIN_X 16
#endif

#ifndef LACE_IDLE_STEAL_MIN_NS /* That threshold before the average of a thread is known */
#define LACE_IDLE_STEAL_MIN_NS 500
#endif

#ifndef LACE_EXT_YIELD_US /* A caller of lace_run_task spins this long before it yields now and then */
#define LACE_EXT_YIELD_US 5
#endif

#ifndef LACE_DEFAULT_PIN /* The pinning mode when neither lace_set_pinning() nor LACE_PIN chooses one */
#define LACE_DEFAULT_PIN LACE_PIN_AUTO
#endif


#if LACE_MSVC
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <processthreadsapi.h>
    #include <process.h>
    #include <synchapi.h>
    #include <profileapi.h>
    #include <malloc.h>
#else
    #include <sched.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <pthread.h>

    #if defined(_WIN32)
        #define WIN32_LEAN_AND_MEAN
        #define NOMINMAX
        #include <windows.h> // still get windows...
    #else
        #include <sys/resource.h> // for getrlimit
    #endif
    #if defined(__linux__)
        #include <sys/prctl.h> // for PR_SET_TIMERSLACK
    #endif
#endif

#ifdef __STDC_NO_ATOMICS__
    #if LACE_MSVC
        #error "C atomic support is not enabled; try /std:c11 and /experimental:c11atomics"
    #else
        #error "C atomic support is not enabled"
    #endif
#endif

#include <stdatomic.h>

#if LACE_USE_HWLOC
    #include <hwloc.h>

    /**
     * HWLOC information
     */
    static hwloc_topology_t topo;
    static hwloc_cpuset_t* cpusets;
    static unsigned int n_nodes, n_cores, n_pus;
#else
    static unsigned int n_pus;
#endif

#if !defined(_WIN32)
    #include <sys/mman.h>
    #ifndef MAP_NORESERVE
        #define MAP_NORESERVE 0
    #endif
#endif

/**
 * Little helper to get cache line size
 */
#if LACE_MSVC
size_t get_cache_line_size(void)
{
    DWORD bytes = 0;
    GetLogicalProcessorInformation(NULL, &bytes);
    if (bytes == 0) return 64;

    SYSTEM_LOGICAL_PROCESSOR_INFORMATION* buf =
        (SYSTEM_LOGICAL_PROCESSOR_INFORMATION*)malloc(bytes);
    if (!buf) return 64;

    if (!GetLogicalProcessorInformation(buf, &bytes)) {
        free(buf);
        return 64;
    }

    size_t line = 0;
    DWORD count = bytes / (DWORD)sizeof(*buf);
    for (DWORD i = 0; i < count; i++) {
        if (buf[i].Relationship == RelationCache &&
            buf[i].Cache.Level == 1 &&
            buf[i].Cache.LineSize != 0) {
            line = (size_t)buf[i].Cache.LineSize;
            break;
        }
    }

    free(buf);
    return line ? line : 64;
}

#elif defined(__APPLE__)
#include <sys/sysctl.h>

static size_t get_cache_line_size(void)
{
    size_t line = 0;
    size_t sz = sizeof(line);
    if (sysctlbyname("hw.cachelinesize", &line, &sz, NULL, 0) == 0 && line != 0)
        return line;
    return 64;
}

#elif defined(__linux__)
static size_t get_cache_line_size(void)
{
    long line = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    return (line > 0) ? (size_t)line : 64;
}

#else
static size_t get_cache_line_size(void)
{
    return 64;
}
#endif

static size_t cache_line_size;

/**
 * Portable futex support
 */

#if defined(__linux__)
#include <linux/futex.h>
#include <sys/syscall.h>

LACE_UNUSED
static int lace_futex_wait(atomic_int *addr, int expected, int64_t timeout_us)
{
    if (timeout_us < 0) {
        return (int)syscall(SYS_futex, addr, FUTEX_WAIT_PRIVATE, expected, NULL, NULL, 0);
    }
    struct timespec ts;
    ts.tv_sec = (time_t)(timeout_us / 1000000);
    ts.tv_nsec = (long)((timeout_us % 1000000) * 1000);
    return (int)syscall(SYS_futex, addr, FUTEX_WAIT_PRIVATE, expected, &ts, NULL, 0);
}

LACE_UNUSED
static int lace_futex_wake_one(atomic_int *addr)
{
    return (int)syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
}

LACE_UNUSED
static int lace_futex_wake_all(atomic_int *addr)
{
    return (int)syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
}

#elif defined(__FreeBSD__)
#include <sys/umtx.h>

LACE_UNUSED
static int lace_futex_wait(atomic_int *addr, int expected, int64_t timeout_us)
{
    struct _umtx_time ut;
    if (timeout_us < 0) {
        return _umtx_op(addr, UMTX_OP_WAIT_UINT_PRIVATE, (unsigned long)expected, NULL, NULL);
    }
    ut._flags = UMTX_ABSTIME;  /* we want relative, but FreeBSD wants this struct */
    ut._clockid = CLOCK_MONOTONIC;
    ut._timeout.tv_sec = (time_t)(timeout_us / 1000000);
    ut._timeout.tv_nsec = (timeout_us % 1000000) * 1000;
    /* Actually for relative timeout, don't set UMTX_ABSTIME */
    ut._flags = 0;
    return _umtx_op(addr, UMTX_OP_WAIT_UINT_PRIVATE, (unsigned long)expected,
                    (void *)(uintptr_t)sizeof(ut), &ut);
}

LACE_UNUSED
static int lace_futex_wake_one(atomic_int *addr)
{
    return _umtx_op(addr, UMTX_OP_WAKE_PRIVATE, 1, NULL, NULL);
}

LACE_UNUSED
static int lace_futex_wake_all(atomic_int *addr)
{
    return _umtx_op(addr, UMTX_OP_WAKE_PRIVATE, INT_MAX, NULL, NULL);
}

#elif defined(__APPLE__)
#include <Availability.h>

#if defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && __MAC_OS_X_VERSION_MIN_REQUIRED >= 140400
/* macOS 14.4+: public os_sync API */
#include <os/os_sync_wait_on_address.h>

LACE_UNUSED
static int lace_futex_wait(atomic_int *addr, int expected, int64_t timeout_us)
{
    if (timeout_us < 0) {
        return os_sync_wait_on_address(addr, (uint64_t)(unsigned int)expected,
            sizeof(int), OS_SYNC_WAIT_ON_ADDRESS_NONE);
    }
    uint64_t timeout_ns = (uint64_t)timeout_us * 1000;
    return os_sync_wait_on_address_with_timeout(addr, (uint64_t)(unsigned int)expected,
        sizeof(int), OS_SYNC_WAIT_ON_ADDRESS_NONE,
        OS_CLOCK_MACH_ABSOLUTE_TIME, timeout_ns);
}

LACE_UNUSED
static int lace_futex_wake_one(atomic_int *addr)
{
    return os_sync_wake_by_address_any(addr, sizeof(int), OS_SYNC_WAKE_BY_ADDRESS_NONE);
}

LACE_UNUSED
static int lace_futex_wake_all(atomic_int *addr)
{
    return os_sync_wake_by_address_all(addr, sizeof(int), OS_SYNC_WAKE_BY_ADDRESS_NONE);
}

#else
/* macOS < 14.4: private __ulock API (used by Rust stdlib, Go runtime, libc++) */
extern int __ulock_wait(uint32_t op, void *addr, uint64_t val, uint32_t timeout_us);
extern int __ulock_wake(uint32_t op, void *addr, uint64_t val);
#define UL_COMPARE_AND_WAIT 1
#define ULF_WAKE_ALL        0x00000100

LACE_UNUSED
static int lace_futex_wait(atomic_int *addr, int expected, int64_t timeout_us)
{
    uint32_t tus = (timeout_us < 0) ? 0 : (uint32_t)timeout_us;
    return __ulock_wait(UL_COMPARE_AND_WAIT, addr, (uint64_t)(unsigned int)expected, tus);
}

LACE_UNUSED
static int lace_futex_wake_one(atomic_int *addr)
{
    return __ulock_wake(UL_COMPARE_AND_WAIT, addr, 0);
}

LACE_UNUSED
static int lace_futex_wake_all(atomic_int *addr)
{
    return __ulock_wake(UL_COMPARE_AND_WAIT | ULF_WAKE_ALL, addr, 0);
}
#endif /* macOS version check */

#elif defined(_WIN32)
/* WaitOnAddress / WakeByAddress* — available since Windows 8 */
/* synchapi.h is already included above for LACE_MSVC; link with Synchronization.lib */

LACE_UNUSED
static int lace_futex_wait(atomic_int *addr, int expected, int64_t timeout_us)
{
    DWORD ms;
    if (timeout_us < 0) {
        ms = INFINITE;
    } else {
        ms = (DWORD)((timeout_us + 999) / 1000);
        if (ms == 0) ms = 1;
    }
    return WaitOnAddress((volatile void*)addr, &expected, sizeof(int), ms) ? 0 : -1;
}

LACE_UNUSED
static int lace_futex_wake_one(atomic_int *addr)
{
    WakeByAddressSingle((void*)addr);
    return 0;
}

LACE_UNUSED
static int lace_futex_wake_all(atomic_int *addr)
{
    WakeByAddressAll((void*)addr);
    return 0;
}

#else
#error "No futex implementation available for this platform"
#endif

/**
 * CPU pause/yield helpers
 */

#if defined(_MSC_VER)
# include <intrin.h>
#endif

#if defined(__i386__) || defined(__x86_64__)
# if !defined(_MSC_VER)
#  include <emmintrin.h>   /* _mm_pause on Clang/GCC */
# endif
#endif

LACE_UNUSED
static inline void lace_cpu_pause(void)
{
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    /* x86/x64: use intrinsics/builtins, no inline asm needed */
# if defined(_MSC_VER)
    _mm_pause();
# elif defined(__clang__) || defined(__GNUC__)
    _mm_pause();
# else
    __asm__ __volatile__("pause");
# endif
#elif defined(__arm__) || defined(__aarch64__) || defined(_M_ARM) || defined(_M_ARM64)
    /* ARM/AArch64 */
# if defined(_MSC_VER)
    __yield();
# else
    __asm__ __volatile__("yield");
# endif
#else
    /* no-op on other architectures */
#endif
}

LACE_UNUSED
static inline void lace_cpu_yield(void)
{
#if defined(_WIN32)
    SwitchToThread();
#else
    sched_yield();
#endif
}

#define LACE_STOLEN      ((lace_worker_public*)0)
#define LACE_BUSY        ((lace_worker_public*)1)
#define LACE_NOWORK      ((lace_worker_public*)2)
#define LACE_STOLEN_LAST ((lace_worker_public*)3)

/**
 * Thread handles
 */
#if !LACE_MSVC
    static pthread_t *handles = NULL;
#else
    static HANDLE *handles = NULL;
#endif

/**
 * Worker thread program stacks
 */
#if LACE_USE_HWLOC || (!defined(_WIN32) && !defined(__FreeBSD__))
    static void** worker_stacks = NULL;
    static size_t worker_stack_size = 0;
    static size_t worker_stack_page_size = 0;
#endif

/**
 * (public) Worker data
 */
static lace_worker_public **workers = NULL;

/**
 * Size of the task deque
 */
static size_t dqsize = 1048576; // 1M entries, virtual only, should be sufficient... (famous last words)

/**
 * Verbosity flag, set with lace_set_verbosity
 */
static int verbosity = 0;

/**
 * Number of workers
 */
static unsigned int n_workers = 0;

/**
 * Datastructure of the task deque etc for each worker.
 * - first public cachelines (accessible via global "workers" variable)
 * - then private cachelines
 * - then the deque array
 */
#if LACE_MSVC
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

typedef struct {
    lace_worker_public worker_public;
    alignas(LACE_PADDING_TARGET) lace_worker worker_private;
    alignas(LACE_PADDING_TARGET) lace_task deque[];
} worker_data;

static_assert(offsetof(worker_data, worker_private) == sizeof(lace_worker_public),
              "lace_worker_public_of() assumes the public struct right before the private one");

#if LACE_MSVC
#pragma warning(pop)
#endif

/**
 * (Secret) holds pointers to the memory block allocated for each worker
 */
static worker_data **workers_memory = NULL;

/* lace_start_external: the program brings the worker threads (1), or Lace creates them (0) */
static int external_mode = 0;

/* lace_set_pinning (-1: LACE_PIN or the default) and lace_set_pin_cpus */
static int pin_api_mode = -1;
static int *pin_api_cpus = NULL;
static unsigned int LACE_UNUSED pin_api_ncpus = 0; // read only by the Linux pinning

void
lace_set_pinning(lace_pin_mode mode)
{
    pin_api_mode = (int)mode;
}

void
lace_set_pin_cpus(const int *cpus, unsigned int n)
{
    free(pin_api_cpus);
    pin_api_cpus = NULL;
    pin_api_ncpus = 0;
    if (cpus == NULL || n == 0) {
        if (pin_api_mode == 8) pin_api_mode = -1;
        return;
    }
    pin_api_cpus = malloc(sizeof(int) * n);
    if (pin_api_cpus == NULL) return;
    memcpy(pin_api_cpus, cpus, sizeof(int) * n);
    pin_api_ncpus = n;
    pin_api_mode = 8; // LACE_PIN_LIST_
}

#if LACE_USE_HWLOC
/* With hwloc, the mode (lace_set_pinning, else LACE_PIN, else LACE_DEFAULT_PIN)
 * only chooses whether to pin: off does not, any other mode pins to cores */
static int hwloc_pin = 1;

static int
lace_hwloc_pins(void)
{
    if (pin_api_mode >= 0) return pin_api_mode != LACE_PIN_OFF;
    const char *pe = getenv("LACE_PIN");
    if (pe != NULL && *pe != '\0') return strcmp(pe, "off") != 0;
    return !external_mode && LACE_DEFAULT_PIN != LACE_PIN_OFF;
}
#endif

#if defined(__linux__) && !LACE_USE_HWLOC
/**
 * Pinning without hwloc (Linux), within the affinity mask of the thread that
 * calls lace_start (taskset, cgroups, SLURM). The modes are lace_pin_mode
 * (lace.h): what each worker may run on, from one hardware thread to a NUMA
 * node; lace_set_pinning() or else LACE_PIN in the environment chooses, else
 * the default is LACE_DEFAULT_PIN (CMake, LACE_PIN_AUTO unless set), or
 * LACE_PIN_OFF with lace_start_external.
 * The CPUs come one of every physical core first (sysfs thread_siblings_list),
 * package by package, in mask order. In the group modes (L2, L3, NUMA) the
 * workers are spread over the groups in proportion to their cores, so that
 * two programs pinning this way share the groups evenly. Auto pins one CPU
 * per worker if the workers fill the cores of the mask, else every worker may
 * run on all cores: a program that leaves cores free may share the machine
 * with another (two programs that both pin worker i to the i-th core took
 * 1.95 times as long as one, against 1.14 times with all cores), and one
 * that fills its mask owns it (taskset, or the default number of workers),
 * where one CPU per worker was fastest on every machine tested.
 * There is no pinning with more workers than places: hardware threads for
 * LACE_PIN_CPU, cores otherwise. LACE_PIN_PRINT=1 prints the CPUs of every
 * worker.
 */
static int pin_mode = 0;
static cpu_set_t *pin_sets = NULL; // per worker: the CPUs it may run on

/* The first number in a sysfs file, or dflt if unknown */
/* A small sysfs file into buf, NUL-terminated, without trailing whitespace:
 * its length, or -1 (also if it does not fit). A plain open and read: fopen
 * also allocates a buffer and calls fstat. */
static int
lace_sysfs_read(const char *path, char *buf, int size)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t r = read(fd, buf, (size_t)size - 1);
    close(fd);
    if (r < 0 || r >= size - 1) return -1;
    while (r > 0 && (buf[r - 1] == '\n' || buf[r - 1] == ' ')) r--;
    buf[r] = '\0';
    return (int)r;
}

/* The first number in a sysfs file, or dflt if unknown */
static int
lace_sysfs_int(const char *path, int dflt)
{
    char buf[64], *end;
    if (lace_sysfs_read(path, buf, (int)sizeof(buf)) <= 0) return dflt;
    long v = strtol(buf, &end, 10);
    return end == buf ? dflt : (int)v;
}


/* The lowest CPU that shares the unified cache of this level with cpu, or dflt if unknown */
static int
lace_cache_of(int cpu, int level, int dflt)
{
    char path[128];
    for (int i = 0; i < 10; i++) {
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cache/index%d/level", cpu, i);
        if (lace_sysfs_int(path, -1) == level) {
            /* not the instruction cache of that level */
            snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cache/index%d/type", cpu, i);
            char type[16];
            if (lace_sysfs_read(path, type, (int)sizeof(type)) < 0) type[0] = '\0';
            if (strcmp(type, "Instruction") == 0) continue;
            snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cache/index%d/shared_cpu_list", cpu, i);
            return lace_sysfs_int(path, dflt);
        }
    }
    return dflt;
}

/* The NUMA node of cpu, or dflt if unknown */
static int
lace_node_of(int cpu, int dflt)
{
    char path[128];
    for (int k = 0; k < 1024; k++) {
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/node%d", cpu, k);
        if (access(path, F_OK) == 0) return k;
    }
    return dflt;
}

/* A CPU list such as 0,2,4-7 into cpus (at most max); the count, or -1 if malformed */
static int
lace_parse_cpulist(const char *s, int *cpus, int max)
{
    int n = 0;
    while (*s != '\0') {
        char *end;
        long a = strtol(s, &end, 10), b;
        if (end == s || a < 0) return -1;
        s = end;
        b = a;
        if (*s == '-') {
            b = strtol(s + 1, &end, 10);
            if (end == s + 1 || b < a) return -1;
            s = end;
        }
        for (long c = a; c <= b && n < max; c++) cpus[n++] = (int)c;
        if (*s == ',') s++;
        else if (*s != '\0') return -1;
    }
    return n;
}

/*
 * The core and the package of every CPU, as keys: the lowest CPU of its
 * hardware threads, and of its package. Read from sysfs at most once per
 * process, as the topology does not change while a program runs; reading two
 * files per CPU at every lace_start took most of its time. One sibling list
 * covers all CPUs in it. 0 means not read yet, else the key + 1.
 */
static int lace_cpu_core[CPU_SETSIZE];
static int lace_cpu_pkg[CPU_SETSIZE];

/* Read the CPU list file of cpu (the first of files that exists) and set
 * key[c] for each CPU c in it; dflt for cpu if no file is readable */
static void
lace_read_cpu_group(int cpu, const char *file, const char *file2, int dflt, int *key)
{
    char path[128], buf[4096];
    int cpus[CPU_SETSIZE], n = -1;
    for (int k = 0; k < 2 && n <= 0; k++) {
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/%s", cpu, k == 0 ? file : file2);
        if (lace_sysfs_read(path, buf, (int)sizeof(buf)) > 0) n = lace_parse_cpulist(buf, cpus, CPU_SETSIZE);
    }
    int low = dflt;
    if (n > 0) {
        low = cpus[0];
        for (int i = 1; i < n; i++) if (cpus[i] < low) low = cpus[i];
        if (low < 0 || low > cpu) low = cpu;
        for (int i = 0; i < n; i++) if (cpus[i] >= 0 && cpus[i] < CPU_SETSIZE) key[cpus[i]] = low + 1;
    }
    if (key[cpu] == 0) key[cpu] = low + 1;
}

#define LACE_PIN_LIST_ 8 /* internal mode: worker i on the i-th CPU of a list */

/* The mode that LACE_PIN names: a number, a name, or list:CPUS (then *list, *nlist) */
static int
lace_pin_mode_env(const char *pe, int **list, int *nlist)
{
    static const char *names[] = { "off", "auto", "all", "cpu", "l3", "numa", "core", "l2" };
    for (int i = 0; i < 8; i++) if (strcmp(pe, names[i]) == 0) return i;
    if (strncmp(pe, "list:", 5) == 0) {
        int *l = malloc(sizeof(int) * CPU_SETSIZE);
        int n = l != NULL ? lace_parse_cpulist(pe + 5, l, CPU_SETSIZE) : -1;
        if (n > 0) {
            *list = l;
            *nlist = n;
            return LACE_PIN_LIST_;
        }
        free(l);
    } else {
        char *end;
        long m = strtol(pe, &end, 10);
        if (end != pe && *end == '\0' && m >= 0 && m <= 7) return (int)m;
    }
    fprintf(stderr, "Lace warning: LACE_PIN=%s is not a mode (off, auto, all, cpu, core, l2, l3, numa) "
                    "or list:CPUS; not pinning\n", pe);
    return LACE_PIN_OFF;
}

/* The CPU set of every worker (pin_sets) for the mode that applies */
static void
lace_pin_setup(void)
{
    int *list = NULL, nlist = 0;
    const char *pe = getenv("LACE_PIN");
    if (pin_api_mode >= 0) {
        pin_mode = pin_api_mode;
        if (pin_mode == LACE_PIN_LIST_) {
            list = pin_api_cpus;
            nlist = (int)pin_api_ncpus;
        }
    } else if (pe != NULL && *pe != '\0') {
        pin_mode = lace_pin_mode_env(pe, &list, &nlist);
    } else {
        pin_mode = external_mode ? LACE_PIN_OFF : LACE_DEFAULT_PIN; // own threads: only when asked
    }
    cpu_set_t mask;
    if (pin_mode <= 0 || sched_getaffinity(0, sizeof(mask), &mask) != 0 || CPU_COUNT(&mask) == 0) goto done;
    const int n = CPU_COUNT(&mask);
    int *cpus = malloc(sizeof(int) * 4 * (size_t)n); // result, mask CPUs, their cores and packages
    if (cpus == NULL) goto done;
    int *in = cpus + n, *core = cpus + 2 * n, *pkg = cpus + 3 * n, m = 0, maxpkg = 0, ncores = 0;
    for (unsigned c = 0; c < (unsigned)CPU_SETSIZE && m < n; c++) {
        if (CPU_ISSET(c, &mask)) {
            in[m] = (int)c;
            if (lace_cpu_core[c] == 0) lace_read_cpu_group((int)c, "thread_siblings_list", "core_cpus_list", (int)c, lace_cpu_core);
            if (lace_cpu_pkg[c] == 0) lace_read_cpu_group((int)c, "package_cpus_list", "core_siblings_list", 0, lace_cpu_pkg);
            core[m] = lace_cpu_core[c] - 1;
            pkg[m] = lace_cpu_pkg[c] - 1;
            m++;
        }
    }
    // Number the packages 0, 1, ... in the order of their keys
    {
        int *rank = calloc(CPU_SETSIZE, sizeof(int));
        if (rank == NULL) { free(cpus); goto done; }
        for (int i = 0; i < m; i++) rank[pkg[i]] = 1;
        int np = 0;
        for (int k = 0; k < CPU_SETSIZE; k++) if (rank[k]) rank[k] = ++np;
        for (int i = 0; i < m; i++) pkg[i] = rank[pkg[i]] - 1;
        maxpkg = np - 1;
        free(rank);
    }
    // First the first CPU of every core, then the others; both by package, then in mask order
    int ncpus = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int p = 0; p <= maxpkg; p++) {
            for (int i = 0; i < m; i++) {
                if (pkg[i] != p) continue;
                int first = 1;
                for (int j = 0; j < i && first; j++) if (core[j] == core[i]) first = 0;
                if (first == (pass == 0)) cpus[ncpus++] = in[i];
            }
        }
        if (pass == 0) ncores = ncpus;
    }
    if (pin_mode == LACE_PIN_AUTO) pin_mode = n_workers >= (unsigned)ncores ? LACE_PIN_CPU : LACE_PIN_ALL;
    if (pin_mode == LACE_PIN_LIST_) {
        // The listed CPUs that are in the mask, in list order
        int k = 0;
        for (int i = 0; i < nlist; i++) {
            if (list[i] >= 0 && list[i] < CPU_SETSIZE && CPU_ISSET((size_t)list[i], &mask)) cpus[k++] = list[i];
        }
        if (k == 0) {
            fprintf(stderr, "Lace warning: no CPU of the pin list is in the affinity mask; not pinning\n");
        } else if ((pin_sets = calloc(n_workers, sizeof(cpu_set_t))) != NULL) {
            for (unsigned w = 0; w < n_workers; w++) CPU_SET((size_t)cpus[w % (unsigned)k], &pin_sets[w]);
        }
    } else if ((unsigned)(pin_mode == LACE_PIN_CPU ? ncpus : ncores) >= n_workers && pin_mode <= 7 &&
               (pin_sets = calloc(n_workers, sizeof(cpu_set_t))) != NULL) {
        if (pin_mode == LACE_PIN_CPU) {
            for (unsigned w = 0; w < n_workers; w++) CPU_SET((size_t)cpus[w], &pin_sets[w]);
        } else if (pin_mode == LACE_PIN_ALL) {
            for (unsigned w = 0; w < n_workers; w++)
                for (int c = 0; c < ncores; c++) CPU_SET((size_t)cpus[c], &pin_sets[w]);
        } else if (pin_mode == LACE_PIN_CORE) {
            // Worker w: the hardware threads (in the mask) of the w-th core
            for (unsigned w = 0; w < n_workers; w++) {
                const int key = lace_cpu_core[cpus[w]] - 1;
                for (int i = 0; i < m; i++) if (core[i] == key) CPU_SET((size_t)in[i], &pin_sets[w]);
            }
        } else {
            /* Groups of cores (in pin order): the L2, the L3 or the NUMA node of each */
            int *grp = in; // reuse: the group key of cpus[c]
            for (int c = 0; c < ncores; c++) {
                const int pk = lace_cpu_pkg[cpus[c]] - 1;
                grp[c] = pin_mode == LACE_PIN_L3 ? lace_cache_of(cpus[c], 3, pk)
                       : pin_mode == LACE_PIN_L2 ? lace_cache_of(cpus[c], 2, cpus[c])
                       : lace_node_of(cpus[c], 0);
            }
            /* Workers per group in proportion to its cores (largest
             * remainders), the groups in order of their first core */
            int *gsize = core, ng = 0; // reuse: cores per group, by first appearance
            int *gkey = pkg;
            for (int c = 0; c < ncores; c++) {
                int g = 0;
                while (g < ng && gkey[g] != grp[c]) g++;
                if (g == ng) { gkey[ng] = grp[c]; gsize[ng] = 0; ng++; }
                gsize[g]++;
            }
            unsigned *gw = calloc(2 * (size_t)ng, sizeof(unsigned)); // workers per group, extra given
            if (gw != NULL) {
                unsigned assigned = 0;
                for (int g = 0; g < ng; g++) {
                    gw[g] = (unsigned)((uint64_t)n_workers * (unsigned)gsize[g] / (unsigned)ncores);
                    assigned += gw[g];
                }
                while (assigned < n_workers) { // one more each for the largest remainders
                    int best = -1;
                    uint64_t bestr = 0;
                    for (int g = 0; g < ng; g++) {
                        if (gw[ng + g] || gw[g] >= (unsigned)gsize[g]) continue;
                        uint64_t r = (uint64_t)n_workers * (unsigned)gsize[g] % (unsigned)ncores;
                        if (best < 0 || r > bestr) { best = g; bestr = r; }
                    }
                    if (best < 0) break;
                    gw[best]++;
                    gw[ng + best] = 1;
                    assigned++;
                }
                unsigned w = 0;
                for (int g = 0; g < ng; g++) {
                    for (unsigned x = 0; x < gw[g] && w < n_workers; x++, w++)
                        for (int c = 0; c < ncores; c++) if (grp[c] == gkey[g]) CPU_SET((size_t)cpus[c], &pin_sets[w]);
                }
                free(gw);
            } else {
                free(pin_sets);
                pin_sets = NULL;
            }
        }
    }
    const char *pp = getenv("LACE_PIN_PRINT");
    if (pin_sets != NULL && pp != NULL && atoi(pp)) {
        for (unsigned w = 0; w < n_workers; w++) {
            fprintf(stderr, "lace pin mode %d worker %u:", pin_mode, w);
            for (unsigned c = 0; c < (unsigned)CPU_SETSIZE; c++) if (CPU_ISSET(c, &pin_sets[w])) fprintf(stderr, " %u", c);
            fprintf(stderr, "\n");
        }
    }
    free(cpus);
done:
    if (list != NULL && list != pin_api_cpus) free(list);
}
#endif

/**
 * One flag per worker, each in its own cache line: 1 while the worker fails
 * to steal in its steal loop (or sleeps there), 0 while it runs a task. A
 * worker writes its flag only when the value changes, so the line stays in
 * its cache; a sentinel reads them to see whether some worker is busy.
 */
typedef struct {
    LACE_ALIGN(LACE_PADDING_TARGET) atomic_uchar idle;
} lace_idle_flag;

static lace_idle_flag *idle_flags = NULL;

static inline void
lace_set_idle(lace_worker *w, unsigned char v)
{
    atomic_uchar *f = &idle_flags[w->worker].idle;
    if (atomic_load_explicit(f, memory_order_relaxed) != v) atomic_store_explicit(f, v, memory_order_relaxed);
}

/**
 * Does a worker other than w run a task (it is not in its steal loop)?
 */
LACE_UNUSED static int
lace_some_active(lace_worker *w)
{
    for (unsigned int i = 0; i < n_workers; i++) {
        if (i != w->worker && !atomic_load_explicit(&idle_flags[i].idle, memory_order_relaxed)) return 1;
    }
    return 0;
}



/**
 * Number of bytes allocated for each worker's worker data.
 */
static size_t workers_memory_size = 0;

/**
 * Per-worker scratch arena reservation size in bytes.
 *
 * Each worker reserves this many bytes of virtual address space at
 * startup. No physical memory is committed until the first scratch
 * allocation; pages are committed in page-sized chunks as needed and
 * released back to the OS during deep backoff. Set to 0 to disable.
 *
 * On 64-bit systems the default is 1 GiB per worker, which costs only
 * address space until used. On 32-bit systems the default is 16 MiB
 * per worker, because user-mode address space is limited (~2-3 GiB
 * total) and a multi-worker process would otherwise exhaust it. If the
 * default does not fit (an address-space limit such as ulimit -v), Lace
 * falls back to LACE_SCRATCH_SMALL, as it does for the stacks; a size
 * set with lace_set_scratch_size() is used as given.
 */
#define LACE_SCRATCH_SMALL ((size_t)16 << 20)
#if UINTPTR_MAX > 0xFFFFFFFFu
static size_t scratch_size = (size_t)1 << 30;   /* 1 GiB on 64-bit */
#else
static size_t scratch_size = LACE_SCRATCH_SMALL; /* 16 MiB on 32-bit */
#endif
static int scratch_size_explicit = 0;

/**
 * The arenas lace_start() reserved, one per worker, of scratch_arena_size
 * bytes each (NULL when the arena is disabled).
 */
static char **scratch_arenas = NULL;
static size_t scratch_arena_size = 0;

/**
 * Per-worker scratch committed band in bytes (default 1 MiB).
 *
 * The band is the number of bytes kept committed above the current
 * scratch top, providing hysteresis so that small push/pop activity
 * near the top does not trigger commit/decommit syscalls.
 */
static size_t scratch_band = (size_t)1 << 20;

/**
 * Cached system page size, populated by lace_scratch_init_page_size()
 * during lace_start(). All scratch commit/decommit operates on
 * page-aligned ranges.
 */
static size_t lace_scratch_page_size = 0;

/**
 * (Secret) holds pointer to private worker data, just for stats collection at end
 */
static lace_worker **workers_p;

/**
 * Flag to signal all workers to quit.
 */
static atomic_int lace_quits = 0;
static int is_running = 0;

/**
 * lace_release_memory: each worker compares release_gen in its idle loop and
 * counts itself in release_done once it has released its memory.
 */
static atomic_uint release_gen = 0;
static atomic_uint release_done = 0;
static atomic_flag release_lock = ATOMIC_FLAG_INIT;

/**
 * Per run: the worker ids that a thread has entered (lace_worker_enter), the
 * workers that have left (lace_stop waits for all of them), and the workers
 * in the steal loop (lace_release_memory waits for those).
 */
static atomic_int *worker_entered = NULL;
static atomic_uint n_left = 0;
static atomic_uint n_looping = 0;

/**
 * Futex-based idle system for progressive worker sleeping
 */
/* Wake up to n waiters: one call where the kernel takes a count. */
LACE_UNUSED static void lace_futex_wake_n(atomic_int *addr, int n)
{
#if defined(__linux__)
    (void)syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, n, NULL, NULL, 0);
#elif defined(__FreeBSD__)
    (void)_umtx_op(addr, UMTX_OP_WAKE_PRIVATE, (unsigned long)n, NULL, NULL);
#else
    for (int i = 0; i < n; i++) (void)lace_futex_wake_one(addr);
#endif
}

static atomic_int wake_futex = 0;   /* futex word: incremented to wake sleeping workers */
static atomic_int n_sleeping = 0;   /* count of workers currently in futex_wait */
static atomic_uint n_sentinels = 0; /* idle workers that stay awake (LACE_IDLE_SENTINELS) */
static atomic_int sentinel_futex = 0; /* napping sentinels wait on it; lace_run_task bumps it */
static atomic_int n_napping = 0;      /* sentinels in a futex nap: lace_run_task wakes one only then */

/* Progressive idle thresholds (failed steal iterations before transitioning) */
#define LACE_IDLE_STAGE1_LIMIT  256   /* yield */
#define LACE_IDLE_FUTEX_TIMEOUT_MIN 100
#ifndef LACE_IDLE_FUTEX_TIMEOUT_MAX
/* With sentinels, a sleeping worker is woken when work appears, so the
 * timeout is only a safety net and may be long: fewer wake-ups while idle. */
#define LACE_IDLE_FUTEX_TIMEOUT_MAX 100000
#endif

/**
 * Thread-specific mechanism to access current worker data
 */
LACE_TLS lace_worker* lace_thread_worker = NULL;

/* How long the last task this thread stole ran (ns), without the steal: the
 * cache misses of a steal can take longer than a tiny task itself. */
static LACE_TLS uint64_t lace_last_stolen_ns = 0;
/* When the thief held that task (lace_now_ns), before any wake-up it sent: the
 * steal loop takes the time it started the attempt from it (the cost of the
 * steal), for the adaptive threshold (LACE_IDLE_STEAL_MIN_X). */
static LACE_TLS uint64_t lace_last_stolen_at = 0;
/* This thread's running average of that cost (ns; 0: none yet) */
static LACE_TLS uint64_t lace_steal_cost_avg = 0;


/**
 * Worker stack sizes. A stack is reserved address space; its pages are
 * committed when first touched. Where that holds (64-bit, not Windows except
 * MSVC, which reserves with STACK_SIZE_PARAM_IS_A_RESERVATION), the default is
 * a large reservation, so deep recursion does not overflow. LACE_DONT_OVERCOMMIT
 * restores the conservative default: RLIMIT_STACK clamped to [16 MB, 64 MB].
 * An explicit stack size is used as given (at least LACE_STACK_MIN).
 */
#define LACE_STACK_MIN        ((size_t)16 << 20)
#define LACE_STACK_SMALL_MAX  ((size_t)64 << 20)
#ifndef LACE_STACK_LARGE
#define LACE_STACK_LARGE      ((size_t)1 << 30)
#endif
#if !LACE_DONT_OVERCOMMIT && SIZE_MAX > 0xffffffffu && (!defined(_WIN32) || LACE_MSVC)
#define LACE_LARGE_STACKS 1
#else
#define LACE_LARGE_STACKS 0
#endif

lace_worker_public* lace_steal(lace_worker *self, lace_worker_public *victim);
int lace_shrink_shared(lace_worker *w);
void lace_leapfrog(lace_worker *__lace_worker);

/**
 * Global newframe variable used for the implementation of NEWFRAME and TOGETHER
 */
lace_newframe_t lace_newframe;

/**
 * Get the number of workers
 */
unsigned int
lace_worker_count(void)
{
    return n_workers;
}

/**
 * If we are collecting PIE times, then we need some helper functions.
 */
static uint64_t LACE_UNUSED count_at_start; // read only with LACE_PIE_TIMES
static uint64_t us_elapsed_timer;

static inline uint64_t lace_now_us(void)
{
#if defined(_WIN32)
    /* Windows: QueryPerformanceCounter -> microseconds */
    LARGE_INTEGER t, f;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    /* avoid overflow: (t * 1e6) / f  */
    return (uint64_t)(((uint64_t)t.QuadPart * 1000000ULL) / (uint64_t)f.QuadPart);

#elif defined(__APPLE__)
    return lace_macos_now_ns() / 1000ULL;

#else
    /* POSIX: clock_gettime -> microseconds */
    struct timespec ts_now;
#if defined(CLOCK_MONOTONIC_RAW)
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts_now);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts_now);
#endif
    return (uint64_t)ts_now.tv_sec * 1000000ULL + (uint64_t)ts_now.tv_nsec / 1000ULL;
#endif
}

static inline uint64_t lace_now_ns(void)
{
#if defined(__APPLE__)
    return lace_macos_now_ns();
#elif defined(_WIN32)
    return lace_now_us() * 1000ULL;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
#endif
}

static void us_elapsed_start(void)
{
    us_elapsed_timer = lace_now_us();
}

LACE_UNUSED
static unsigned long long us_elapsed(void)
{
    return (unsigned long long)(lace_now_us() - us_elapsed_timer);
}

/**
 * Lace barrier implementation, that synchronizes on all workers.
 */
#if LACE_MSVC
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

typedef struct {
    LACE_ALIGN(LACE_PADDING_TARGET) atomic_int count;
    LACE_ALIGN(LACE_PADDING_TARGET) atomic_int leaving;
    LACE_ALIGN(LACE_PADDING_TARGET) atomic_int wait;
    atomic_int sleeping; // waiters asleep on wait (its cache line: the leader reads it after the flip)
} barrier_t;

#if LACE_MSVC
#pragma warning(pop)
#endif

static barrier_t lace_bar;

#ifndef LACE_BARRIER_SPIN_US /* How long a worker spins at a barrier before it sleeps */
#define LACE_BARRIER_SPIN_US 20
#endif

/**
 * The slow path of lace_barrier: sleep on the wait flag until the leader
 * flips it, like an idle worker (lace_steal_loop): in naps of
 * LACE_IDLE_FUTEX_TIMEOUT_MIN for LACE_IDLE_SHALLOW_US after the wait began,
 * then until woken. The leader wakes the sleepers, which ends a nap at once;
 * the naps only keep the core in a shallow idle state. A sleep without a
 * timeout let the core go into a deep idle state that took 30-140 us to leave.
 */
static LACE_NOINLINE void
lace_barrier_sleep(int wait, uint64_t start)
{
    /* seq_cst, as the flip and the load of sleeping by the leader: either
     * the leader sees us sleeping, or we see the flip before we sleep */
    atomic_fetch_add_explicit(&lace_bar.sleeping, 1, memory_order_seq_cst);
    while (atomic_load_explicit(&lace_bar.wait, memory_order_seq_cst) == wait) {
        const int64_t timeout = lace_now_us() - start < (uint64_t)LACE_IDLE_SHALLOW_US ?
            LACE_IDLE_FUTEX_TIMEOUT_MIN : -1;
        lace_futex_wait(&lace_bar.wait, wait, timeout);
    }
    atomic_fetch_sub_explicit(&lace_bar.sleeping, 1, memory_order_relaxed);
}

/**
 * Enter the Lace barrier and wait until all workers have entered the Lace barrier.
 */
void
lace_barrier(void)
{
    int wait = atomic_load_explicit(&lace_bar.wait, memory_order_relaxed);
    if ((int)n_workers == 1 + atomic_fetch_add_explicit(&lace_bar.count, 1, memory_order_acq_rel)) {
        // This thread is the last to arrive (the leader)
        atomic_store_explicit(&lace_bar.count, 0, memory_order_relaxed);
        atomic_store_explicit(&lace_bar.leaving, (int)n_workers, memory_order_relaxed);
        /* seq_cst, as the sleeper count: either we see a sleeper, or the
         * sleeper sees the flip before it sleeps */
        atomic_fetch_xor_explicit(&lace_bar.wait, 1, memory_order_seq_cst);
        if (atomic_load_explicit(&lace_bar.sleeping, memory_order_seq_cst) > 0) {
            lace_futex_wake_all(&lace_bar.wait);
        }
    } else {
        /* Wait until the leader flips the wait value. Spin briefly, since
         * the others usually arrive within microseconds, then sleep: with
         * more threads than CPUs, spinning waiters keep the workers they
         * wait for off the CPU, and a TOGETHER took minutes. The clock is
         * read only after 64 rounds, so short waits do not read it. */
        uint64_t start = 0;
        for (unsigned int spin = 1; atomic_load_explicit(&lace_bar.wait, memory_order_acquire) == wait; spin++) {
            lace_cpu_pause();
            if ((spin & 63) != 0) continue;
            const uint64_t now = lace_now_us();
            if (spin == 64) {
                start = now;
            } else if (now - start >= LACE_BARRIER_SPIN_US) {
                lace_barrier_sleep(wait, start);
                break;
            }
        }
    }
    // Needed for lace_barrier_destroy to observe all threads have exited
    atomic_fetch_sub_explicit(&lace_bar.leaving, 1, memory_order_release);
}

/**
 * Initialize the Lace barrier
 */
static void
lace_barrier_init(void)
{
    atomic_init(&lace_bar.count, 0);
    atomic_init(&lace_bar.leaving, 0);
    atomic_init(&lace_bar.wait, 0);
    atomic_init(&lace_bar.sleeping, 0);
}

/**
 * Destroy the Lace barrier (just wait until all are exited)
 */
static void
lace_barrier_destroy(void)
{
    while (atomic_load_explicit(&lace_bar.leaving, memory_order_acquire) > 0) {
        // possibly pause/yield
    }
}

/**
 * For debugging purposes, check if memory is allocated on the correct memory nodes.
 */
static void LACE_UNUSED
lace_check_memory(void)
{
#if LACE_USE_HWLOC
    // get our current worker
    lace_worker *w = lace_get_worker();
    void* mem = workers_memory[w->worker];

    // get pinned PUs
    hwloc_cpuset_t cpuset = hwloc_bitmap_alloc();
    hwloc_get_cpubind(topo, cpuset, HWLOC_CPUBIND_THREAD);

    // get nodes of pinned PUs
    hwloc_nodeset_t cpunodes = hwloc_bitmap_alloc();
    hwloc_cpuset_to_nodeset(topo, cpuset, cpunodes);

    // get location of memory
    hwloc_nodeset_t memlocation = hwloc_bitmap_alloc();
    hwloc_get_area_memlocation(topo, mem, sizeof(worker_data), memlocation, HWLOC_MEMBIND_BYNODESET);

    // check if CPU and node are on the same place
    if (!hwloc_bitmap_isincluded(memlocation, cpunodes)) {
        fprintf(stdout, "Lace warning: Lace thread not on same memory domain as data!\n");

        char *strp, *strp2, *strp3;
        hwloc_bitmap_list_asprintf(&strp, cpuset);
        hwloc_bitmap_list_asprintf(&strp2, cpunodes);
        hwloc_bitmap_list_asprintf(&strp3, memlocation);
        fprintf(stdout, "Worker %d is pinned on PUs %s, node %s; memory is pinned on node %s\n", w->worker, strp, strp2, strp3);
        free(strp);
        free(strp2);
        free(strp3);
    }

    // free allocated memory
    hwloc_bitmap_free(cpuset);
    hwloc_bitmap_free(cpunodes);
    hwloc_bitmap_free(memlocation);
#endif
}

#if LACE_USE_HWLOC
/* The binding of a program thread before it became a worker (lace_start_external) */
static LACE_TLS hwloc_bitmap_t lace_saved_cpubind = NULL;
#endif

static void
lace_pin_worker(void)
{
#if LACE_USE_HWLOC
    if (!hwloc_pin) return;

    // Get the worker id
    unsigned int worker = lace_get_worker()->worker;

    if (external_mode && lace_saved_cpubind == NULL) {
        lace_saved_cpubind = hwloc_bitmap_alloc();
        if (lace_saved_cpubind != NULL) (void)hwloc_get_cpubind(topo, lace_saved_cpubind, HWLOC_CPUBIND_THREAD);
    }

    // Pin the thread and bind its memory to its NUMA node, where the system
    // supports it (macOS does neither); report a failure once
    static atomic_int warned_cpubind = 0, warned_membind = 0;
    const struct hwloc_topology_support *support = hwloc_topology_get_support(topo);

    if (support->cpubind->set_thisthread_cpubind &&
        hwloc_set_cpubind(topo, cpusets[worker], HWLOC_CPUBIND_THREAD) != 0 &&
        atomic_exchange(&warned_cpubind, 1) == 0) {
        fprintf(stderr, "Lace warning: hwloc could not pin worker %u (%s); workers may run unpinned\n",
            worker, strerror(errno));
    }

    if (support->membind->set_area_membind && support->membind->migrate_membind) {
        hwloc_bitmap_t nodeset = hwloc_bitmap_alloc();
        hwloc_cpuset_to_nodeset(topo, cpusets[worker], nodeset);
        if (hwloc_set_area_membind(topo, workers_memory[worker], workers_memory_size, nodeset, HWLOC_MEMBIND_BIND, HWLOC_MEMBIND_STRICT | HWLOC_MEMBIND_MIGRATE | HWLOC_MEMBIND_BYNODESET) != 0 &&
            atomic_exchange(&warned_membind, 1) == 0) {
            fprintf(stderr, "Lace warning: hwloc could not bind the memory of worker %u to its NUMA node (%s)\n",
                worker, strerror(errno));
        }
        hwloc_bitmap_free(nodeset);
    }

    // Check if everything is on the correct node
    lace_check_memory();
#endif
}

static inline uint64_t lace_splitmix64(uint64_t* x)
{
    uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static inline void lace_rng_seed(lace_worker* w, uint64_t seed)
{
    uint64_t x = seed ? seed : 0x123456789abcdefULL;
    w->rng.s0 = lace_splitmix64(&x);
    w->rng.s1 = lace_splitmix64(&x);
    if ((w->rng.s0 | w->rng.s1) == 0) w->rng.s1 = 1;
}

/* ========================================================================
 * Scratch arena: virtual-memory-backed per-worker bump allocator.
 *
 * Each worker reserves a large VM range (default 1 GiB) at startup with
 * no physical memory committed. Allocations bump a per-worker top
 * pointer. When the bump crosses the committed boundary, additional
 * pages are committed (in chunks, with a configurable band above the
 * top to absorb push/pop oscillation). When a worker reaches deep
 * idle in the backoff progression, its arena is trimmed: pages above
 * top+band are decommitted back to the OS.
 *
 * Tasks that need temporary storage call lace_scratch_mark() to save
 * the current arena position, lace_scratch_alloc() to obtain memory,
 * and lace_scratch_reset() to release it before returning. The arena
 * is private to one worker and follows a strict LIFO discipline; no
 * atomics, no locks. Tasks that do not allocate pay nothing.
 * ======================================================================== */

/**
 * Detect and cache the OS page size. Called once from lace_start().
 */
static LACE_UNUSED void
lace_scratch_init_page_size(void)
{
    if (lace_scratch_page_size != 0) return;
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    lace_scratch_page_size = (size_t)si.dwPageSize;
#else
    long ps = sysconf(_SC_PAGESIZE);
    lace_scratch_page_size = (ps > 0) ? (size_t)ps : 4096;
#endif
    if (lace_scratch_page_size == 0) lace_scratch_page_size = 4096;
}

static LACE_UNUSED inline size_t
lace_scratch_round_up_page(size_t n)
{
    size_t ps = lace_scratch_page_size;
    return (n + ps - 1) & ~(ps - 1);
}

static LACE_UNUSED inline char *
lace_scratch_round_up_ptr(char *p)
{
    size_t ps = lace_scratch_page_size;
    uintptr_t u = (uintptr_t)p;
    u = (u + ps - 1) & ~(uintptr_t)(ps - 1);
    return (char *)u;
}

/**
 * Portable wrapper around strerror(errno) for diagnostic messages.
 * MSVC deprecates plain strerror() and rejects it under /WX; use
 * strerror_s there instead. The caller supplies the buffer because
 * the MSVC variant needs one, and using a fixed static buffer would
 * be a code-smell across threads.
 */
static LACE_UNUSED const char *
lace_strerror(int err, char *buf, size_t bufsz)
{
#if defined(_MSC_VER)
    strerror_s(buf, bufsz, err);
    return buf;
#else
    (void)buf;
    (void)bufsz;
    return strerror(err);
#endif
}

/**
 * Reserve a VM range of @p size bytes. Returns the base address, or
 * NULL on failure. No physical memory is committed.
 */
static LACE_UNUSED char *
lace_scratch_reserve(size_t size)
{
#if defined(_WIN32)
    void *p = VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
    return (char *)p;
#else
    void *p = mmap(NULL, size, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) return NULL;
    return (char *)p;
#endif
}

/**
 * Commit @p len bytes starting at @p addr (must be page-aligned).
 * Returns 0 on success, -1 on failure.
 */
static LACE_UNUSED int
lace_scratch_commit(char *addr, size_t len)
{
    if (len == 0) return 0;
#if defined(_WIN32)
    void *p = VirtualAlloc(addr, len, MEM_COMMIT, PAGE_READWRITE);
    return (p == NULL) ? -1 : 0;
#else
    return mprotect(addr, len, PROT_READ | PROT_WRITE);
#endif
}

/**
 * Decommit @p len bytes starting at @p addr (must be page-aligned),
 * releasing the underlying physical pages back to the OS. The VM
 * reservation is preserved. Errors are ignored: decommit is advisory
 * and a failure here merely means the pages remain resident.
 */
static LACE_UNUSED void
lace_scratch_decommit(char *addr, size_t len)
{
    if (len == 0) return;
#if defined(_WIN32)
    VirtualFree(addr, len, MEM_DECOMMIT);
#else
    /* Drop the physical pages, then revoke access so a stray access
     * past the committed top is caught immediately rather than
     * silently re-faulting in zero pages. */
    (void)madvise(addr, len, MADV_DONTNEED);
    (void)mprotect(addr, len, PROT_NONE);
#endif
}

/**
 * Release the VM reservation entirely.
 */
static LACE_UNUSED void
lace_scratch_release(char *addr, size_t size)
{
    if (addr == NULL) return;
#if defined(_WIN32)
    (void)size;
    VirtualFree(addr, 0, MEM_RELEASE);
#else
    munmap(addr, size);
#endif
}

/**
 * Slow path for lace_scratch_alloc(): commits more pages to satisfy
 * an allocation that would have exceeded scratch_committed_end.
 * Aborts the process if the reservation is exhausted.
 *
 * @param lw            Calling worker
 * @param aligned_size  Allocation size, already aligned to 16 bytes
 * @return              Pointer to allocated memory
 */
void *
lace_scratch_grow(lace_worker *lw, size_t aligned_size)
{
    char *cur_top = lw->scratch_top;
    char *new_top = cur_top + aligned_size;

    if (LACE_UNLIKELY(new_top > lw->scratch_reserve_end)) {
        fprintf(stderr,
                "Lace fatal: scratch arena exhausted on worker %u "
                "(reservation %zu bytes, request %zu bytes). "
                "Call lace_set_scratch_size() with a larger value before lace_start().\n",
                (unsigned)lw->worker,
                (size_t)(lw->scratch_reserve_end - lw->scratch_base),
                aligned_size);
        abort();
    }

    /* Commit up to new_top + band, page-aligned, capped at reservation. */
    char *desired_end = new_top + scratch_band;
    if (desired_end < new_top /* overflow */
        || desired_end > lw->scratch_reserve_end) {
        desired_end = lw->scratch_reserve_end;
    }
    char *commit_to = lace_scratch_round_up_ptr(desired_end);
    if (commit_to > lw->scratch_reserve_end) commit_to = lw->scratch_reserve_end;

    char *commit_from = lw->scratch_committed_end;
    if (commit_to > commit_from) {
        if (lace_scratch_commit(commit_from, (size_t)(commit_to - commit_from)) != 0) {
            char ebuf[128];
            fprintf(stderr,
                    "Lace fatal: scratch commit failed on worker %u "
                    "(requested %zu bytes): %s\n",
                    (unsigned)lw->worker,
                    (size_t)(commit_to - commit_from),
                    lace_strerror(errno, ebuf, sizeof ebuf));
            abort();
        }
        lw->scratch_committed_end = commit_to;
    }

    lw->scratch_top = new_top;
    return cur_top;
}

/**
 * Trim the arena: decommit pages above scratch_top + band. Called
 * from the worker's main loop when it transitions into deep idle
 * (futex-wait) for the first time in a backoff sequence. Releases
 * memory accumulated by prior deep operations without affecting the
 * VM reservation, so a subsequent wake-and-allocate stays cheap.
 *
 * Natural floor: when scratch_top == scratch_base, this leaves
 * exactly one band committed (the first allocations after wake won't
 * fault). When scratch_top > scratch_base the worker is between
 * tasks, so this should not normally fire; we still tolerate it.
 */
static LACE_UNUSED void
lace_scratch_trim(lace_worker *lw)
{
    if (lw->scratch_base == NULL) return;

    char *keep_end = lw->scratch_top + scratch_band;
    if (keep_end < lw->scratch_top /* overflow */
        || keep_end > lw->scratch_reserve_end) {
        keep_end = lw->scratch_reserve_end;
    }
    keep_end = lace_scratch_round_up_ptr(keep_end);
    if (keep_end > lw->scratch_reserve_end) keep_end = lw->scratch_reserve_end;

    char *decommit_from = keep_end;
    char *decommit_to = lw->scratch_committed_end;
    if (decommit_to > decommit_from) {
        lace_scratch_decommit(decommit_from, (size_t)(decommit_to - decommit_from));
        lw->scratch_committed_end = decommit_from;
    }
}

static void
lace_init_worker(unsigned int worker)
{
    // Allocate our memory
#if defined(_WIN32)
    workers_memory[worker] = (worker_data*)VirtualAlloc(NULL, workers_memory_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (workers_memory[worker] == NULL) {
        fprintf(stderr, "Lace error: Unable to allocate VirtualAlloc memory for the Lace worker!\n");
        exit(1);
    }
#else
    workers_memory[worker] = mmap(NULL, workers_memory_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (workers_memory[worker] == MAP_FAILED) {
        fprintf(stderr, "Lace error: Unable to allocate mmapped memory for the Lace worker!\n");
        exit(1);
    }
#endif

    // Set pointers
    lace_worker_public *wt = workers[worker] = &workers_memory[worker]->worker_public;
    lace_worker *w = workers_p[worker] = &workers_memory[worker]->worker_private;
    lace_thread_worker = w;

    w->dq = workers_memory[worker]->deque;
    w->head = w->dq;

    // Initialize public worker data
    wt->dq = w->dq;
    atomic_store_explicit(&wt->ts.v, 0, memory_order_relaxed);
    wt->allstolen = 0;
    wt->movesplit = 0;
    atomic_store_explicit(&wt->waiting_on, NULL, memory_order_relaxed);

    // Initialize private worker data
    w->_public = wt;
    w->end = w->dq + dqsize;
    w->split = w->dq;
    w->allstolen = 0;
    w->worker = (uint16_t)worker;
    lace_set_idle(w, 1);

    // Seed from the worker index, not rand(): the program's rand() sequence
    // is its own, and this runs in the new worker thread, at no fixed point
    // relative to the program's own calls.
    lace_rng_seed(w, 0x9E3779B97F4A7C15ULL * ((uint64_t)worker + 1));

#if LACE_COUNT_EVENTS
    // Initialize counters
    { int k; for (k=0; k<CTR_MAX; k++) w->ctr[k] = 0; }
#endif

    w->time = lace_gethrtime();

#if LACE_PIE_TIMES
    w->level = 0;
#endif
#if LACE_COUNT_STEALS
    w->leap_task = NULL;
#endif

    if (scratch_arenas != NULL) {
        char *base = scratch_arenas[worker];
        w->scratch_base = base;
        w->scratch_reserve_end = base + scratch_arena_size;
        w->scratch_top = base;
        w->scratch_committed_end = base;  /* nothing committed yet; first alloc grows */
    } else {
        w->scratch_base = NULL;
        w->scratch_reserve_end = NULL;
        w->scratch_top = NULL;
        w->scratch_committed_end = NULL;
    }
    w->scratch_leak_reported = 0;
}

/**
 * "External" task management
 */

/**
 * Global "external" task
 *
 * A thread outside Lace puts its task in a free slot and waits on the state
 * word, which goes from EXT_RUNNING to EXT_DONE when a worker has run the
 * task. The waiting thread first spins for LACE_EXT_SPIN_US, since a short
 * task finishes sooner than a sleep and a wake-up take. Then it marks the
 * state EXT_SLEEPING and sleeps; the worker issues the wake-up only for that
 * state, so short tasks cost no system call on either side.
 */
#define EXT_RUNNING  0
#define EXT_DONE     1
#define EXT_SLEEPING 2

#ifndef LACE_EXT_SPIN_US
#define LACE_EXT_SPIN_US 20
#endif

typedef struct ext_lace_task {
    lace_task *task;
    atomic_int state;
    atomic_int taken; // a worker took the task
} ext_lace_task;

#define LACE_EXT_SLOTS 64
static _Atomic(ext_lace_task*)external_tasks[LACE_EXT_SLOTS];
static atomic_int external_task_count = 0;

/* Wake a sleeping worker to help with an external task that a worker took, if no sentinel watches */
static inline void
lace_wake_helper(void)
{
    if (atomic_load_explicit(&n_sleeping, memory_order_relaxed) > 0
        && atomic_load_explicit(&n_sentinels, memory_order_relaxed) == 0
        ) {
        atomic_fetch_add_explicit(&wake_futex, 1, memory_order_release);
        lace_futex_wake_one(&wake_futex);
    }
}

void
lace_run_task(lace_task *task)
{
    // check if we are really not in a Lace thread
    lace_worker* self = lace_get_worker();
    if (self != 0) {
        task->f(self, task);
        return;
    }

    ext_lace_task et;
    et.task = task;
    atomic_store_explicit(&et.task->thief, 0, memory_order_relaxed);
    atomic_store_explicit(&et.state, EXT_RUNNING, memory_order_relaxed);
    atomic_store_explicit(&et.taken, 0, memory_order_relaxed);

    // Push into any empty slot
    while (1) {
        for (int i = 0; i < LACE_EXT_SLOTS; i++) {
            ext_lace_task* expected = NULL;
            if (atomic_compare_exchange_weak_explicit(&external_tasks[i],
                &expected, &et, memory_order_release, memory_order_relaxed)) {
                atomic_fetch_add_explicit(&external_task_count, 1, memory_order_seq_cst);
                goto pushed;
            }
        }
        lace_sleep_us(1);
    }
pushed:

    /* Wake a worker only if one sleeps. A worker counts itself in n_sleeping
     * before it last checks external_task_count (both seq_cst, here too), so
     * either it sees our task or we see it sleeping. Awake workers check
     * external_task_count in their idle loop. */
    if (atomic_load_explicit(&n_sleeping, memory_order_seq_cst) > 0
        && atomic_load_explicit(&n_sentinels, memory_order_seq_cst) == 0
        ) {
        atomic_fetch_add_explicit(&wake_futex, 1, memory_order_release);
        lace_futex_wake_one(&wake_futex);
    }
    else if (atomic_load_explicit(&n_napping, memory_order_seq_cst) > 0) {
        /* End a sentinel's nap rather than waiting for it */
        atomic_fetch_add_explicit(&sentinel_futex, 1, memory_order_release);
        lace_futex_wake_one(&sentinel_futex);
    }

    uint64_t start = lace_now_us();
    int yielding = 0;
    /* The caller wakes a helper for the worker that took the task, so that
     * the worker starts the task at once instead of after a futex call (2 us
     * more for tiny regions), and a task that is done before the caller
     * notices wakes nobody. */
    int helped = 0;
    for (unsigned int spin = 1; ; spin++) {
        if (atomic_load_explicit(&et.state, memory_order_acquire) == EXT_DONE) return;
        if (!helped) {
            const int tk = atomic_load_explicit(&et.taken, memory_order_relaxed);
            if (tk != 0) {
                lace_wake_helper();
                helped = 1;
            }
        }
        /* After a pure spin of LACE_EXT_YIELD_US, yield now and then: the
         * caller may share its CPU with the worker that runs the task (more
         * threads than cores), which a pure spin would keep off the CPU for
         * the whole spin. The pure spin first keeps a short call cheap: a
         * yield after 64 rounds cost short calls 20-28 percent on AMD and
         * Intel 4-socket machines. */
        if (yielding && spin > 64 && (spin & 3) == 0) lace_cpu_yield(); else lace_cpu_pause();
        if ((spin & 15) == 0) {
            const uint64_t elapsed = lace_now_us() - start;
            if (elapsed >= LACE_EXT_SPIN_US) break;
            if (elapsed >= (uint64_t)LACE_EXT_YIELD_US) yielding = 1;
        }
    }

    int expected = EXT_RUNNING;
    if (!atomic_compare_exchange_strong_explicit(&et.state, &expected, EXT_SLEEPING,
            memory_order_seq_cst, memory_order_acquire)) {
        return; // EXT_DONE meanwhile
    }
    if (!helped) {
        /* seq_cst, as the store of taken and the load of state by the
         * worker: either we see the task taken, or the worker sees us
         * sleeping and wakes the helper itself. */
        const int tk = atomic_load_explicit(&et.taken, memory_order_seq_cst);
        if (tk != 0) lace_wake_helper();
    }
    while (atomic_load_explicit(&et.state, memory_order_acquire) != EXT_DONE) {
        lace_futex_wait(&et.state, EXT_SLEEPING, -1);
    }
}

/* Run one external task if there is one; returns whether it did. */
static inline int
lace_steal_external(lace_worker *self)
{
    for (int i = 0; i < LACE_EXT_SLOTS; i++) {
        if (atomic_load_explicit(&external_tasks[i], memory_order_relaxed) == NULL) continue;
        ext_lace_task* stolen = atomic_exchange_explicit(&external_tasks[i], NULL, memory_order_acquire);
        if (stolen != NULL) {
            atomic_fetch_sub_explicit(&external_task_count, 1, memory_order_relaxed);
            /* The task may spawn work while the other workers sleep, so a
             * helper is woken (unless a sentinel watches, see
             * lace_wake_helper). The waiting caller does that (see
             * lace_run_task), so that this worker starts the task at once;
             * if the caller sleeps already, this worker does. */
            atomic_store_explicit(&stolen->taken, 1, memory_order_seq_cst);
            if (atomic_load_explicit(&stolen->state, memory_order_seq_cst) == EXT_SLEEPING) {
                lace_wake_helper();
            }
            // execute task
            atomic_store_explicit(&stolen->task->thief, self->_public, memory_order_relaxed);
            lace_set_idle(self, 0);
            lace_time_event(self, 1);
            stolen->task->f(self, stolen->task);
            lace_time_event(self, 2);
            atomic_store_explicit(&stolen->task->thief, THIEF_COMPLETED, memory_order_relaxed);
            // After the exchange the caller may return and its frame (and
            // the state word) go away; a wake-up on that address is harmless.
            if (atomic_exchange_explicit(&stolen->state, EXT_DONE, memory_order_acq_rel) == EXT_SLEEPING) {
                lace_futex_wake_one(&stolen->state);
            }
            lace_time_event(self, 8);
            return 1;
        }
    }
    return 0;
}

/**
 * Pick a random victim among the n-1 workers other than self (n > 1).
 * Two 64-bit divisions (rng % (n-1), then % n) cost tens of cycles on every
 * steal attempt; instead scale 32 random bits to [0, n-1) by a multiply and
 * shift, then wrap past self with a compare.
 */
static inline unsigned int lace_random_victim(lace_worker *w, unsigned int self, unsigned int n)
{
    uint64_t r = lace_rng(w) >> 32;
    unsigned int v = self + 1U + (unsigned int)((r * (n - 1U)) >> 32);
    return v >= n ? v - n : v;
}

/**
 * (Try to) steal and execute a task from a random worker.
 */
void lace_steal_random(lace_worker *_lace_worker)
{
    lace_check_yield(_lace_worker);

    if (LACE_UNLIKELY(atomic_load_explicit(&external_task_count, memory_order_acquire) > 0)) {
        lace_steal_external(_lace_worker);
    } else if (n_workers > 1) {
        lace_worker_public *victim = workers[lace_random_victim(_lace_worker, _lace_worker->worker, n_workers)];

        PR_COUNTSTEALS(_lace_worker, CTR_steal_tries);
        lace_worker_public *res = lace_steal(_lace_worker, victim);
        if (res == LACE_STOLEN || res == LACE_STOLEN_LAST) {
            PR_COUNTSTEALS(_lace_worker, CTR_steals);
        } else if (res == LACE_BUSY) {
            PR_COUNTSTEALS(_lace_worker, CTR_steal_busy);
        }
    }
}

#if !defined(_WIN32)
/**
 * Release the physical pages in [from, to), rounded inward to whole pages;
 * touching a page again faults in a fresh zero page. Linux frees at once
 * with MADV_DONTNEED and keeps the mapping, including an hwloc NUMA binding.
 * Elsewhere madvise only marks pages for lazy reclaim (macOS keeps them
 * resident), so a fresh anonymous mapping replaces the range; without the
 * binding, first touch by the pinned worker places pages on its node.
 */
static void
lace_release_pages(uintptr_t from, uintptr_t to)
{
    uintptr_t ps = (uintptr_t)lace_scratch_page_size;
    from = (from + ps - 1) & ~(ps - 1);
    to &= ~(ps - 1);
    if (to <= from) return;
#if defined(__linux__)
    (void)madvise((void*)from, (size_t)(to - from), MADV_DONTNEED);
#else
    (void)mmap((void*)from, (size_t)(to - from), PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
#endif
}
#endif

/* Stack kept below the frame that releases a worker's stack: madvise and the
 * idle loop run there. */
#define LACE_RELEASE_STACK_MARGIN ((uintptr_t)64 << 10)

/**
 * Release the memory an idle worker does not use: its stack below this
 * frame, its deque past the head, and its scratch arena past the first band.
 * Runs on the worker itself, from its idle loop, where no task is running.
 */
static LACE_NOINLINE void
lace_release_worker_memory(lace_worker *lw)
{
#if !defined(_WIN32)
#if LACE_USE_HWLOC || !defined(__FreeBSD__)
    if (worker_stacks != NULL && worker_stacks[lw->worker] != NULL) {
        /* The frame address, not that of a local: AddressSanitizer may keep
         * locals on a fake stack of its own (detect_stack_use_after_return).
         * A top outside the stack releases nothing. */
        uintptr_t low = (uintptr_t)worker_stacks[lw->worker] + worker_stack_page_size;
        uintptr_t high = (uintptr_t)worker_stacks[lw->worker] + worker_stack_size;
        uintptr_t top = (uintptr_t)__builtin_frame_address(0);
        if (top > low + LACE_RELEASE_STACK_MARGIN && top <= high) lace_release_pages(low, top - LACE_RELEASE_STACK_MARGIN);
    }
#endif
    lace_release_pages((uintptr_t)(lw->head + 1), (uintptr_t)lw->end);
#endif
    lace_scratch_trim(lw);
}

void
lace_release_memory(void)
{
    if (!is_running || lace_get_worker() != NULL) return;

    while (atomic_flag_test_and_set_explicit(&release_lock, memory_order_acquire)) lace_sleep_us(50);
    atomic_store_explicit(&release_done, 0, memory_order_relaxed);
    atomic_fetch_add_explicit(&release_gen, 1, memory_order_release);

    /* Wake sleeping workers, so they release now instead of at their next timeout */
    atomic_fetch_add_explicit(&wake_futex, 1, memory_order_release);
    lace_futex_wake_all(&wake_futex);

    while (atomic_load_explicit(&release_done, memory_order_acquire) <
           atomic_load_explicit(&n_looping, memory_order_relaxed)) lace_sleep_us(50);
    atomic_flag_clear_explicit(&release_lock, memory_order_release);
}

/**
 * Main Lace worker implementation.
 * Steal from random victims until "quit" is set.
 */
/**
 * Sentinels (LACE_IDLE_SENTINELS = K): while some worker runs a task, up to K
 * idle workers stay awake instead of sleeping. A sentinel tries every other
 * worker once per round and naps LACE_SENTINEL_NAP_US between rounds, so it
 * finds new work within a round and wakes a sleeper, at a small fraction of a
 * core. It also takes external tasks (lace_run_task), so a caller need not
 * wake a sleeping worker while there is a sentinel. When no worker has run a
 * task for LACE_SENTINEL_GRACE_US, it goes to sleep like the others: calls
 * from outside with short gaps find a sentinel awake, and a program that
 * stopped using Lace pays only for that grace period. Sleeping workers then
 * back off exponentially to LACE_IDLE_FUTEX_TIMEOUT_MAX (100 ms).
 */
LACE_UNUSED static int
lace_claim_sentinel(void)
{
    unsigned int c = atomic_load_explicit(&n_sentinels, memory_order_relaxed);
    while (c < (unsigned int)LACE_IDLE_SENTINELS) {
        if (atomic_compare_exchange_weak_explicit(&n_sentinels, &c, c + 1,
                memory_order_relaxed, memory_order_relaxed)) return 1;
    }
    return 0;
}

static void
lace_nap(void)
{
    /* A futex nap: a caller of lace_run_task ends it at once. Count the nap
     * first (seq_cst, as the sleepers): either the caller sees it, or the
     * nap sees the caller's task. */
    atomic_fetch_add_explicit(&n_napping, 1, memory_order_seq_cst);
    int val = atomic_load_explicit(&sentinel_futex, memory_order_acquire);
    if (atomic_load_explicit(&external_task_count, memory_order_seq_cst) == 0) {
        lace_futex_wait(&sentinel_futex, val, LACE_SENTINEL_NAP_US);
    }
    atomic_fetch_sub_explicit(&n_napping, 1, memory_order_relaxed);
}

/**
 * Is there a task to steal at the victim? lace_steal without the steal: it
 * also asks a victim without shared tasks to share some (movesplit).
 */
static inline int
lace_peek(lace_worker_public *victim)
{
    if (victim == NULL || victim->allstolen) return 0;
    TailSplitNA ts;
    ts.v = atomic_load_explicit(&victim->ts.v, memory_order_relaxed);
    if (ts.ts.tail < ts.ts.split) return 1;
    if (victim->movesplit == 0) victim->movesplit = 1;
    return 0;
}

/**
 * The rounds of a sentinel. Returns 1 when it saw a task to steal (at
 * *last_victim), 0 when it retires (no worker ran a task for the grace
 * period), -1 when the steal loop has something to handle (an external task,
 * an interrupt, a memory release, quitting). It runs nothing itself.
 */
LACE_UNUSED static int
lace_sentinel(lace_worker *lw, atomic_int *quit, unsigned int *last_victim, unsigned int seen_release, int nap_first)
{
    const unsigned int n = n_workers, self = lw->worker;
    /* The last work it saw was a tiny task or gone: nap before looking, or
     * a storm of tiny tasks keeps it from backing off. */
    if (nap_first) lace_nap();
    const uint64_t start = lace_now_us();
    uint64_t last_active = start;
    while (!atomic_load_explicit(quit, memory_order_relaxed)) {
        /* Look, do not steal: a sentinel that ran a stolen task would still
         * count as watching, so lace_run_task would wake nobody and the
         * sleepers would miss the work. It returns, gives up its slot, and
         * the steal loop steals. */
        unsigned int v = lace_random_victim(lw, self, n);
        for (unsigned int k = 0; k + 1 < n; k++) {
            if (lace_peek(workers[v])) {
                *last_victim = v;
                return 1;
            }
            if (++v == n) v = 0;
            if (v == self && ++v == n) v = 0;
            if (atomic_load_explicit(&external_task_count, memory_order_relaxed) > 0) return -1;
        }
        /* An interrupt (NEWFRAME, TOGETHER) runs in the steal loop, after
         * this worker gave up its sentinel slot */
        if (atomic_load_explicit(&lace_newframe.t, memory_order_relaxed) != NULL) return -1;
        if (atomic_load_explicit(&external_task_count, memory_order_acquire) > 0) return -1;
        if (atomic_load_explicit(&release_gen, memory_order_acquire) != seen_release) return -1;
        uint64_t now = lace_now_us();
        if (lace_some_active(lw)) last_active = now;
        else if (now - last_active >= (uint64_t)LACE_SENTINEL_GRACE_US) return 0;
        /* Right after it became a sentinel (after activity), spin: the next
         * region or external call is likely soon, and a nap would add to the
         * latency. Timed from the start, not from the last time some worker
         * was busy, or a long sequential task keeps it spinning throughout. */
        if (!nap_first && now - start < (uint64_t)LACE_SENTINEL_SPIN_US) {
            /* Yield, not pause: under a stream of external calls the spin
             * never ends, and with more threads than cores it would keep the
             * caller off its CPU (197 us per call at 2 workers). */
            lace_cpu_yield();
            continue;
        }
        lace_nap();
    }
    return -1;
}

TASK(void, lace_steal_loop, atomic_int*, quit)

LACE_NO_SANITIZE_THREAD
void lace_steal_loop_CALL(lace_worker* lw, atomic_int* quit)
{
    // Determine who I am
    const unsigned int worker_id = lw->worker;

    // Prepare self, victim
    lace_worker_public ** victim = NULL;

    lw->time = lace_gethrtime();

    unsigned int n = n_workers;
    unsigned int last_victim = worker_id; // means no last victim
    unsigned int idle_count = 0;
    unsigned int seen_release = 0; // lace_start resets release_gen to 0
    int worked = 0; // ran a task since this worker last slept: may become a sentinel
    int sentinel_nap_first = 0; // its last sentinel episode saw work that turned out not worth it
    uint64_t last_work_us = 0; // when this worker last ran a task (lace_now_us)

    (void)worked;
    (void)sentinel_nap_first;
    (void)last_work_us;

    while(1) {
        /* In the steal loop a worker is idle until it steals; a task that
         * returned (stolen, external or an interrupt's) left the flag 0. */
        lace_set_idle(lw, 1);
        if (LACE_UNLIKELY(atomic_load_explicit(&release_gen, memory_order_acquire) != seen_release)) {
            seen_release = atomic_load_explicit(&release_gen, memory_order_acquire);
            lace_release_worker_memory(lw);
            atomic_fetch_add_explicit(&release_done, 1, memory_order_release);
        }

        if (n > 1) {
            if ((idle_count&1) == 0 && last_victim != worker_id) {
                victim = workers + last_victim;
            } else {
                victim = workers + lace_random_victim(lw, worker_id, n);
            }

            PR_COUNTSTEALS(lw, CTR_steal_tries);
            const uint64_t steal_t0 = lace_now_ns();
            lace_worker_public *res = lace_steal(lw, *victim);
            if (res == LACE_STOLEN || res == LACE_STOLEN_LAST) {
                PR_COUNTSTEALS(lw, CTR_steals);
                last_victim = (unsigned int)(victim - workers);
                /* A task shorter than a steal is worth counts as a failure:
                 * in a steal storm (tiny tasks, few workers with work) the
                 * thieves then back off and leave the work to its owner,
                 * instead of turning every one of its joins into a remote one.
                 * Worth means LACE_IDLE_STEAL_MIN_X times the average cost of
                 * this thread's steals: a few cache-line transfers, more across
                 * chiplets and sockets and under contention, so the threshold
                 * follows the machine and the load (a desktop processor with
                 * cheap steals gains from stealing tasks of 1 us, a server with
                 * chiplets or sockets does not). Each sample counts as at most
                 * twice and at least half the average (a preempted steal does
                 * not throw it off); LACE_IDLE_STEAL_MIN_NS divided by the
                 * factor is where the average starts. */
                uint64_t min_ns = (uint64_t)LACE_IDLE_STEAL_MIN_NS;
                if (lace_last_stolen_at > steal_t0) {
                    const uint64_t k = (uint64_t)LACE_IDLE_STEAL_MIN_X;
                    uint64_t avg = lace_steal_cost_avg ? lace_steal_cost_avg : (min_ns + k - 1) / k;
                    uint64_t cost = lace_last_stolen_at - steal_t0;
                    if (cost > 2 * avg) cost = 2 * avg;
                    if (cost < avg / 2) cost = avg / 2;
                    avg = cost >= avg ? avg + (cost - avg) / 8 : avg - (avg - cost) / 8;
                    lace_steal_cost_avg = avg > 0 ? avg : 1;
                    min_ns = k * lace_steal_cost_avg;
                }
                if (lace_last_stolen_ns < min_ns) {
                    idle_count++;
                } else
                {
                    idle_count = 0;
                    worked = 1;
                    sentinel_nap_first = 0;
                    last_work_us = lace_now_us();
                }
            } else if (res == LACE_BUSY) {
                PR_COUNTSTEALS(lw, CTR_steal_busy);
                /* A lost race does not reset the backoff: in a steal storm,
                 * thieves race for tasks that are not worth it. */
            } else { // LACE_NOWORK
                idle_count++;
                lace_set_idle(lw, 1);
            }
        } else {
            idle_count++; // a lone worker has no victims: back off and sleep too
        }

        if (lace_check_yield(lw)) {
            idle_count = 0;
        }

        /* Every round: the counter's cache line changes only when external
         * tasks come and go, so the load is cheap next to the yield below,
         * and a waiting caller gets its task picked up a round sooner. */
        if (LACE_UNLIKELY(atomic_load_explicit(&external_task_count, memory_order_acquire) > 0)) {
            /* Only the worker that ran the task is busy again: the others
             * keep backing off, or a stream of external calls (from a
             * thread outside Lace) keeps every idle worker spinning. */
            if (lace_steal_external(lw)) {
                idle_count = 0;
                worked = 1;
                last_work_us = lace_now_us();
            }
            continue;
        }

#if LACE_BACKOFF
        // Progressive idle: yield → futex sleep
        if (idle_count > LACE_IDLE_STAGE1_LIMIT) {
            /* Before sleeping: become a sentinel if fewer than
             * LACE_IDLE_SENTINELS workers are, and this worker ran a task
             * since it last slept or another one runs a task now. Not after
             * a mere timeout, or sentinels would form again and again in a
             * program that no longer uses Lace (test_idle). */
            if (n > 1 && (worked || lace_some_active(lw)) && lace_claim_sentinel()) {
                lace_set_idle(lw, 1); // a tiny steal this round may have left it 0
                int r = lace_sentinel(lw, quit, &last_victim, seen_release, sentinel_nap_first);
                /* seq_cst, as the sleeper count: a caller of lace_run_task that
                 * saw a sentinel and did not wake anyone must see it leave
                 * before this worker sleeps without seeing the caller's task. */
                atomic_fetch_sub_explicit(&n_sentinels, 1, memory_order_seq_cst);
                if (r == 1) {
                    /* Work seen at last_victim: steal it next round (an even
                     * idle_count picks last_victim). The steal decides: one
                     * worth it resets the backoff (and its early wake-up brings
                     * in a replacement while no sentinel watches); a tiny or
                     * failed one brings this worker back here, to nap before
                     * it looks again. */
                    idle_count = LACE_IDLE_STAGE1_LIMIT;
                    sentinel_nap_first = 1;
                    continue;
                }
                if (r == -1) {
                    idle_count = 0;
                    continue;
                }
            }
            worked = 0; // about to sleep
            lace_set_idle(lw, 1);
            /* Stage 2: futex wait with bounded timeout */
            unsigned int futex_iters = idle_count - LACE_IDLE_STAGE1_LIMIT;

            /* On the transition into deep idle, decommit unused scratch
             * pages back to the OS. We only do this once per backoff
             * sequence (idle_count resets on successful steal), so it
             * does not become hot.
             *
             * At this point all task frames have unwound: scratch_top
             * should equal scratch_base. If it doesn't, a task forgot
             * to lace_scratch_reset() before returning. We warn once
             * per worker and recover by resetting top to base, so the
             * trim below releases the full arena. */
            if (futex_iters == 1) {
                if (lw->scratch_top != lw->scratch_base) {
                    if (!lw->scratch_leak_reported) {
                        lw->scratch_leak_reported = 1;
                        fprintf(stderr,
                                "Lace warning: worker %u entered idle with "
                                "%zu bytes of leaked scratch. Some task "
                                "did not lace_scratch_reset() before returning.\n",
                                (unsigned)lw->worker,
                                (size_t)(lw->scratch_top - lw->scratch_base));
                    }
                    lw->scratch_top = lw->scratch_base;
                }
                lace_scratch_trim(lw);
            }

#if LACE_PIE_TIMES
            uint64_t prev = lace_gethrtime();
#endif
            int64_t timeout_us = (int64_t)LACE_IDLE_FUTEX_TIMEOUT_MIN << (futex_iters < 16 ? futex_iters - 1 : 15);
            /* Shortly after it ran a task, a worker sleeps in short naps: its
             * core stays in a shallow idle state and wakes fast for the next
             * region, at a few microseconds of CPU per nap. */
            if (lace_now_us() - last_work_us < (uint64_t)LACE_IDLE_SHALLOW_US) timeout_us = LACE_IDLE_FUTEX_TIMEOUT_MIN;
            if (timeout_us > LACE_IDLE_FUTEX_TIMEOUT_MAX) timeout_us = LACE_IDLE_FUTEX_TIMEOUT_MAX;

            /* Count ourselves as sleeping, then check for external tasks
             * once more (seq_cst, paired with lace_run_task): a thread that
             * submits a task after this check sees us and wakes us. */
            atomic_fetch_add_explicit(&n_sleeping, 1, memory_order_seq_cst);
            int val = atomic_load_explicit(&wake_futex, memory_order_acquire);
            if (atomic_load_explicit(&external_task_count, memory_order_seq_cst) == 0) {
                lace_futex_wait(&wake_futex, val, timeout_us);
            }
            atomic_fetch_sub_explicit(&n_sleeping, 1, memory_order_relaxed);
#if LACE_PIE_TIMES
            PR_ADD(lw, CTR_backoff, lace_gethrtime() - prev);
#endif

            if (atomic_load_explicit(&external_task_count, memory_order_acquire) > 0) {
                if (lace_steal_external(lw)) {
                    idle_count = 0;
                    worked = 1;
                    last_work_us = lace_now_us();
                }
                continue;
            }
        } else {
            /* Stage 1: yield the CPU briefly */
            lace_cpu_yield();
        }
#endif

        if (atomic_load_explicit(quit, memory_order_relaxed)) break;
    }
}

#if defined(__linux__) && !LACE_USE_HWLOC
/* The affinity of a program thread before it became a worker (lace_start_external) */
static LACE_TLS cpu_set_t lace_saved_mask;
static LACE_TLS int lace_saved_mask_set = 0;
#endif
#if defined(__linux__)
/* The timer slack of a program thread before it became a worker, or -1 */
static LACE_TLS int lace_saved_slack = -1;
#endif

/**
 * Make the calling thread Lace worker <worker> (lace.h).
 */
void
lace_worker_enter(unsigned int worker)
{
    if (!is_running || worker >= n_workers) {
        fprintf(stderr, "Lace error: lace_worker_enter(%u): Lace is not running, or has fewer workers\n", worker);
        exit(1);
    }
    if (lace_thread_worker != NULL) {
        fprintf(stderr, "Lace error: lace_worker_enter(%u): this thread is already Lace worker %u\n",
            worker, (unsigned int)lace_thread_worker->worker);
        exit(1);
    }
    int expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&worker_entered[worker], &expected, 1,
            memory_order_relaxed, memory_order_relaxed)) {
        fprintf(stderr, "Lace error: lace_worker_enter(%u): another thread is already worker %u\n", worker, worker);
        exit(1);
    }

    // Pin first, as the pinning mode says (with lace_start_external only when
    // asked): the worker then touches its memory (worker data, deque, stack,
    // scratch arena) on its own CPU, so first touch places it on its NUMA node
#if defined(__linux__) && !LACE_USE_HWLOC
    if (pin_sets != NULL) {
        if (external_mode) lace_saved_mask_set = sched_getaffinity(0, sizeof(cpu_set_t), &lace_saved_mask) == 0;
        (void)pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &pin_sets[worker]);
    }
#endif

    // Initialize data structures
    lace_init_worker(worker);

    // hwloc pins after the worker data exists, and moves that data to the node
    lace_pin_worker();
#if defined(__linux__)
    // A sentinel naps for microseconds; the default timer slack is 50 us
    if (external_mode) lace_saved_slack = prctl(PR_GET_TIMERSLACK, 0UL, 0UL, 0UL, 0UL);
    (void)prctl(PR_SET_TIMERSLACK, 1000UL, 0UL, 0UL, 0UL);
#endif

    // A worker counts as busy until it idles in the steal loop, so a thread
    // that runs tasks itself keeps the sentinels awake while it works
    lace_set_idle(lace_thread_worker, 0);

    // Start together: after the barrier, every worker sees all others set up
    // (lace_init_worker publishes each one in workers[]), without ordering on
    // every steal. The barrier backs off rather than spin, as on a busy machine
    // the threads may outnumber the CPUs.
    lace_barrier();
}

/**
 * Steal and run tasks until lace_stop (lace.h).
 */
void
lace_worker_loop(void)
{
    lace_worker* lw = lace_thread_worker;
    if (lw == NULL) {
        fprintf(stderr, "Lace error: lace_worker_loop: this thread is not a Lace worker\n");
        exit(1);
    }
    atomic_fetch_add_explicit(&n_looping, 1, memory_order_relaxed);
    lace_steal_loop_CALL(lw, &lace_quits);
    atomic_fetch_sub_explicit(&n_looping, 1, memory_order_relaxed);

    // Time worker exit event
    lace_time_event(lw, 9);
}

/**
 * The calling thread stops being a Lace worker (lace.h).
 */
void
lace_worker_leave(void)
{
    lace_worker* lw = lace_thread_worker;
    if (lw == NULL) {
        fprintf(stderr, "Lace error: lace_worker_leave: this thread is not a Lace worker\n");
        exit(1);
    }
    if (lw->head != lw->dq) {
        fprintf(stderr, "Lace error: lace_worker_leave: worker %u still has tasks (called from inside a task?)\n",
            (unsigned int)lw->worker);
        exit(1);
    }
    lace_set_idle(lw, 1);

    // Give a program thread its affinity and timer slack back
#if LACE_USE_HWLOC
    if (lace_saved_cpubind != NULL) {
        (void)hwloc_set_cpubind(topo, lace_saved_cpubind, HWLOC_CPUBIND_THREAD);
        hwloc_bitmap_free(lace_saved_cpubind);
        lace_saved_cpubind = NULL;
    }
#elif defined(__linux__)
    if (lace_saved_mask_set) {
        (void)pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &lace_saved_mask);
        lace_saved_mask_set = 0;
    }
#endif
#if defined(__linux__)
    if (lace_saved_slack >= 0) {
        (void)prctl(PR_SET_TIMERSLACK, (unsigned long)lace_saved_slack, 0UL, 0UL, 0UL);
        lace_saved_slack = -1;
    }
#endif

    lace_thread_worker = NULL;
    atomic_fetch_add_explicit(&n_left, 1, memory_order_release);
}

static void* lace_worker_thread(void* arg);
#if LACE_MSVC
static unsigned __stdcall lace_worker_thread_win(void* arg);
#endif

/* The stack size of Lace's own threads, where the system reserves the stacks */
static size_t spawn_stacksize = 0;

/**
 * Create the thread of worker <i>, one of Lace's own workers. Each creating
 * thread uses its own attributes, as threads are created by several at once.
 */
static void
lace_spawn_worker(unsigned int i)
{
#if !LACE_MSVC
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) {
        fprintf(stderr, "Lace error: unable to initialize thread attributes!\n");
        exit(1);
    }
#if LACE_USE_HWLOC || (!defined(_WIN32) && !defined(__FreeBSD__))
    if (pthread_attr_setstack(&attr, (char*)worker_stacks[i] + worker_stack_page_size,
                              worker_stack_size - worker_stack_page_size) != 0) {
        fprintf(stderr, "Lace error: unable to set stack for worker %u: %s\n", i, strerror(errno));
        exit(1);
    }
#else
    if (pthread_attr_setstacksize(&attr, spawn_stacksize) != 0) {
        fprintf(stderr, "Lace error: unable to set stack size to %zu bytes!\n", spawn_stacksize);
        exit(1);
    }
#endif
    int rc = pthread_create(&handles[i], &attr, lace_worker_thread, (void*)(size_t)i);
    if (rc != 0) {
        fprintf(stderr, "Lace error: unable to create worker thread %u: %s\n", i, strerror(rc));
        exit(1);
    }
    pthread_attr_destroy(&attr);
#else
    unsigned thread_id;
    handles[i] = (HANDLE)_beginthreadex(NULL, (unsigned)spawn_stacksize, lace_worker_thread_win,
        (void*)(size_t)i, STACK_SIZE_PARAM_IS_A_RESERVATION, &thread_id);
    if (handles[i] == 0) {
        fprintf(stderr, "Lace error: failed to create worker thread %u\n", i);
        exit(1);
    }
#endif
}

/**
 * Lace's own worker threads: create workers 2i+1 and 2i+2 (the threads grow
 * as a tree, so n threads take about log2(n) creation steps instead of n),
 * then enter as worker i, steal until lace_stop, and leave.
 */
static void*
lace_worker_thread(void* arg)
{
    const unsigned int worker = (unsigned int)(size_t)arg;
    if (2 * worker + 1 < n_workers) lace_spawn_worker(2 * worker + 1);
    if (2 * worker + 2 < n_workers) lace_spawn_worker(2 * worker + 2);
    lace_worker_enter(worker);
    lace_worker_loop();
    lace_worker_leave();
    return NULL;
}

#if LACE_MSVC
static unsigned __stdcall lace_worker_thread_win(void* arg)
{
    lace_worker_thread(arg);   // ignore return value
    return 0;
}
#endif

/**
 * Set the verbosity of Lace.
 */
void
lace_set_verbosity(int level)
{
    verbosity = level;
}

/**
 * Configure the per-worker scratch arena reservation size in bytes.
 * Call before lace_start(). Pass 0 to disable scratch entirely.
 * The reservation costs only address space until actually used.
 */
void
lace_set_scratch_size(size_t size)
{
    scratch_size = size;
    scratch_size_explicit = 1;
}

/**
 * Reserve a scratch arena of @p size bytes (page-aligned) for each worker,
 * in scratch_arenas. Returns 0, or -1 with errno set and nothing reserved.
 */
static int
lace_reserve_scratch(size_t size)
{
    for (unsigned int i = 0; i < n_workers; i++) {
        scratch_arenas[i] = lace_scratch_reserve(size);
        if (scratch_arenas[i] == NULL) {
#if defined(_WIN32)
            int err = ENOMEM; /* VirtualAlloc does not set errno */
#else
            int err = errno;
#endif
            while (i-- > 0) lace_scratch_release(scratch_arenas[i], size);
            errno = err;
            return -1;
        }
    }
    scratch_arena_size = size;
    return 0;
}

/**
 * Configure the per-worker scratch committed band in bytes.
 * Call before lace_start(). The band is rounded up to a page-size
 * multiple at use time.
 */
void
lace_set_scratch_band(size_t size)
{
    scratch_band = size;
}

/**
 * The conservative worker stack size: RLIMIT_STACK of the calling thread,
 * clamped to [LACE_STACK_MIN, LACE_STACK_SMALL_MAX].
 */
static LACE_UNUSED size_t
lace_small_stack_size(void)
{
    size_t size = 0;
#if !defined(_WIN32)
    struct rlimit lim;
    if (getrlimit(RLIMIT_STACK, &lim) == 0) size = (size_t)lim.rlim_cur;
#endif
    if (size < LACE_STACK_MIN) size = LACE_STACK_MIN;
    if (size > LACE_STACK_SMALL_MAX) size = LACE_STACK_SMALL_MAX;
    return size;
}

/**
 * The worker stack size used when lace_start() is given 0.
 */
static size_t
lace_default_stack_size(void)
{
#if LACE_LARGE_STACKS
    return LACE_STACK_LARGE;
#else
    return lace_small_stack_size();
#endif
}

#if LACE_USE_HWLOC || (!defined(_WIN32) && !defined(__FreeBSD__))
/**
 * Free the first @p count worker stacks.
 */
static void
lace_free_stacks(unsigned int count)
{
    for (unsigned int i = 0; i < count; i++) {
#if LACE_USE_HWLOC
        hwloc_free(topo, worker_stacks[i], worker_stack_size);
#else
        munmap(worker_stacks[i], worker_stack_size);
#endif
        worker_stacks[i] = NULL;
    }
}

/**
 * Free the @p count stacks allocated so far after a failed allocation;
 * keeps errno.
 */
static int
lace_alloc_stacks_failed(unsigned int count)
{
    int err = errno;
    lace_free_stacks(count);
    errno = err;
    return -1;
}

/**
 * Reserve a stack of @p stacksize bytes (rounded up to the page size) plus a
 * guard page at the low end for every worker. With hwloc the pages are bound
 * to the worker's NUMA node; otherwise first-touch places them after pinning.
 * Nothing is committed until touched. Returns 0, or -1 with errno set and no
 * stacks left allocated.
 */
static int
lace_alloc_stacks(size_t stacksize)
{
    long ps = sysconf(_SC_PAGESIZE);
    size_t page_size = (ps > 0) ? (size_t)ps : 4096;
    stacksize = (stacksize + page_size - 1) & ~(page_size - 1);
    worker_stack_size = stacksize + page_size;
    worker_stack_page_size = page_size;

    for (unsigned int i = 0; i < n_workers; i++) {
#if LACE_USE_HWLOC
        hwloc_bitmap_t nodeset = hwloc_bitmap_alloc();
        hwloc_cpuset_to_nodeset(topo, cpusets[i], nodeset);
        void* stack = hwloc_alloc_membind(topo, worker_stack_size, nodeset,
            HWLOC_MEMBIND_BIND, HWLOC_MEMBIND_BYNODESET);
        hwloc_bitmap_free(nodeset);
#else
        void* stack = mmap(NULL, worker_stack_size, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (stack == MAP_FAILED) stack = NULL;
#endif
        if (stack == NULL) return lace_alloc_stacks_failed(i);
        worker_stacks[i] = stack;

        // Guard page at the low end (stacks grow downward)
#if defined(_WIN32)
        DWORD old_protect;
        if (!VirtualProtect(stack, page_size, PAGE_NOACCESS, &old_protect)) {
#else
        if (mprotect(stack, page_size, PROT_NONE) != 0) {
#endif
            return lace_alloc_stacks_failed(i + 1);
        }
    }
    return 0;
}
#endif


/**
 * The part of lace_start that lace_start_external shares: everything but the
 * worker threads and their stacks.
 */
static void
lace_setup(unsigned int _n_workers, size_t dequesize, int external)
{
    if (is_running) {
        fprintf(stderr, "Lace error: Lace is already running\n");
        exit(1);
    }
    external_mode = external;

    /* Cache the OS page size for scratch-arena commit/decommit. */
    lace_scratch_init_page_size();

#if LACE_USE_HWLOC
    // Initialize topology and information about cpus, restricted to the CPUs
    // the process may run on (taskset, cgroups, SLURM)
    hwloc_topology_init(&topo);
    hwloc_topology_load(topo);
    hwloc_cpuset_t allowed = hwloc_bitmap_alloc();
    if (allowed && hwloc_get_cpubind(topo, allowed, HWLOC_CPUBIND_PROCESS) == 0
        && hwloc_topology_restrict(topo, allowed, 0) != 0 && errno == ENOMEM) {
        hwloc_topology_load(topo); // the failed restrict reset the topology
    }
    if (allowed) hwloc_bitmap_free(allowed);

    n_nodes = (unsigned int)hwloc_get_nbobjs_by_type(topo, HWLOC_OBJ_NODE);
    n_cores = (unsigned int)hwloc_get_nbobjs_by_type(topo, HWLOC_OBJ_CORE);
    n_pus = (unsigned int)hwloc_get_nbobjs_by_type(topo, HWLOC_OBJ_PU);

    hwloc_pin = lace_hwloc_pins();
    static int told_no_pinning = 0;
    if (hwloc_pin && !hwloc_topology_get_support(topo)->cpubind->set_thisthread_cpubind && !told_no_pinning) {
        told_no_pinning = 1;
        fprintf(stderr, "Lace warning: hwloc cannot pin threads on this system; the workers are not pinned\n");
    }

#elif defined(_WIN32)
    DWORD_PTR processMask = 0, systemMask = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask) && processMask) {
        unsigned count = 0;
        DWORD_PTR m = processMask;
        while (m) { m &= (m - 1); count++; }      // popcount
        n_pus = count ? count : 1;
    }
    else {
        SYSTEM_INFO sysinfo;
        GetSystemInfo(&sysinfo);
        n_pus = (unsigned)sysinfo.dwNumberOfProcessors;
    }

#elif defined(HAVE_SCHED_GETAFFINITY) && HAVE_SCHED_GETAFFINITY
    cpu_set_t cs;
    CPU_ZERO(&cs);
    if (sched_getaffinity(0, sizeof(cs), &cs) == 0) {
        n_pus = (unsigned)CPU_COUNT(&cs);
    }
    else {
        n_pus = (unsigned)sysconf(_SC_NPROCESSORS_ONLN);
    }

#elif defined(__linux__) || defined(__APPLE__) || defined(__unix__)
    n_pus = (unsigned)sysconf(_SC_NPROCESSORS_ONLN);

#else
    n_pus = 1;
#endif

    if (n_pus == 0) n_pus = 1;

    cache_line_size = get_cache_line_size();
    size_t task_size = sizeof(lace_task);
    if (cache_line_size % task_size != 0 && task_size % cache_line_size != 0) {
        // typical values of task_size: 32, 64, 128...
        // lace14.h on 32-bit: 32 bytes; lace14.h on 64-bit: 64 bytes
        // lace14.h on 32-bit: 64 bytes; lace14.h on 64-bit: 128 bytes
        // typical values of cache_line_size: 32, 64, 128...
        // for example some ARM chips have a 32-byte cache line size
        // and some modern ARM chips have a 128-byte cache line size like Apple's CPUs
        // if not aligned, this will result in excessive false sharing and therefore
        // significant performance loss

        fprintf(stderr, "Lace warning: task size %zu and cache line size %zu may be unaligned!\n",
            task_size, cache_line_size);
    }

    // Initialize globals
    n_workers = _n_workers == 0 ? n_pus : _n_workers;
    dqsize = dequesize > 0 ? dequesize : 100000;
    atomic_store_explicit(&lace_quits, 0, memory_order_relaxed);
    atomic_store_explicit(&release_gen, 0, memory_order_relaxed);
    atomic_store_explicit(&release_done, 0, memory_order_relaxed);

#if LACE_USE_HWLOC
    // Distribute workers over cores.
    // It tries to first use all cores, before using multiple PUs per core, avoiding hyperthreading.
    cpusets = malloc(n_workers * sizeof(*cpusets));
    // one way of doing this is just with hwloc_distrib, but this is suboptimal
    // for (int i=0; i<n_workers; i++) cpusets[i] = hwloc_bitmap_alloc();
    // hwloc_obj_t root = hwloc_get_root_obj(topo);
    // hwloc_distrib(topo, &root, 1, cpusets, n_workers, INT_MAX, 0);

    {
        unsigned int i=0;
        hwloc_obj_t cores[n_cores];
        {
            hwloc_obj_t core = NULL;
            while ((core = hwloc_get_next_obj_by_type(topo, HWLOC_OBJ_CORE, core)) != NULL) cores[i++] = core;
        }

        i = 0;
        unsigned int j=0, k=0;
        // i is index of worker, j is index of cpu, k is how many PUs per core we have used
        while (i < n_workers) {
            if (j < n_cores && k < (unsigned)hwloc_bitmap_weight(cores[j]->cpuset)) {
                cpusets[i] = cores[j]->cpuset;
                // grab the kth in cpuset
                // turns out this is slightly slower than just pinning to all threads
                // int idx = hwloc_bitmap_first(cores[j]->cpuset);
                // for (int kk=1; kk<k; kk++) idx = hwloc_bitmap_next(cores[j]->cpuset, idx);
                // hwloc_obj_t pu = hwloc_get_pu_obj_by_os_index(topo, idx);
                // cpusets[i] = pu->cpuset;
                i++;
                j++;
            } else {
                k++;
                for (j=0; j<n_cores; j++) {
                    if (k < (unsigned)hwloc_bitmap_weight(cores[j]->cpuset)) break;
                }
                if (j == n_cores) {
                    j = 0;
                    k = 0;
                }
            }
        }
    }
#endif

    // Initialize Lace barrier
    lace_barrier_init();

    // Allocate array with all workers
    // first make sure that the amount to allocate (n_workers times pointer) is a multiple of cache_line_size
    size_t to_allocate = n_workers * sizeof(void*);
    to_allocate = (to_allocate+cache_line_size-1) & (~(cache_line_size-1));
#if defined(_MSC_VER) || defined(__MINGW64_VERSION_MAJOR)
    workers = _aligned_malloc(to_allocate, cache_line_size);
    workers_p = _aligned_malloc(to_allocate, cache_line_size);
    workers_memory = _aligned_malloc(to_allocate, cache_line_size);
#elif defined(__MINGW32__)
    workers = __mingw_aligned_malloc(to_allocate, cache_line_size);
    workers_p = __mingw_aligned_malloc(to_allocate, cache_line_size);
    workers_memory = __mingw_aligned_malloc(to_allocate, cache_line_size);
#else
    workers = aligned_alloc(cache_line_size, to_allocate);
    workers_p = aligned_alloc(cache_line_size, to_allocate);
    workers_memory = aligned_alloc(cache_line_size, to_allocate);
#endif
    if (workers == 0 || workers_p == 0 || workers_memory == 0) {
        fprintf(stderr, "Lace error: unable to allocate memory for the workers!\n");
        exit(1);
    }

    // Ensure worker array is set to 0 initially
    memset(workers, 0, n_workers*sizeof(lace_worker_public*));

    {
        size_t flags_size = n_workers * sizeof(lace_idle_flag);
#if defined(_MSC_VER) || defined(__MINGW64_VERSION_MAJOR)
        idle_flags = _aligned_malloc(flags_size, LACE_PADDING_TARGET);
#elif defined(__MINGW32__)
        idle_flags = __mingw_aligned_malloc(flags_size, LACE_PADDING_TARGET);
#else
        idle_flags = aligned_alloc(LACE_PADDING_TARGET, flags_size);
#endif
        if (idle_flags == NULL) {
            fprintf(stderr, "Lace error: unable to allocate memory for the idle flags!\n");
            exit(1);
        }
        for (unsigned int i = 0; i < n_workers; i++) atomic_store_explicit(&idle_flags[i].idle, 1, memory_order_relaxed);
    }

#if defined(__linux__) && !LACE_USE_HWLOC
    lace_pin_setup();
#endif


    // Compute memory size for each worker
    workers_memory_size = sizeof(worker_data) + sizeof(lace_task) * dqsize;

    // Reserve the scratch arenas here, before the workers start, so the size
    // is one decision for all of them: like the stacks, a default that does
    // not fit (ulimit -v) falls back to the conservative size.
    if (scratch_size > 0) {
        scratch_arenas = (char**)malloc(n_workers * sizeof(char*));
        if (scratch_arenas == NULL) {
            fprintf(stderr, "Lace error: unable to allocate scratch arena array!\n");
            exit(1);
        }
        /* Page-align the reservation for clean commit/decommit math. */
        size_t reserve = lace_scratch_round_up_page(scratch_size);
        if (lace_reserve_scratch(reserve) != 0) {
            int err = errno;
            size_t fallback = lace_scratch_round_up_page(LACE_SCRATCH_SMALL);
            char ebuf[128];
            if (!scratch_size_explicit && fallback < reserve && lace_reserve_scratch(fallback) == 0) {
                if (verbosity) {
                    fprintf(stdout, "Lace startup: could not reserve %zu-byte scratch arenas (%s), using %zu bytes.\n",
                        reserve, lace_strerror(err, ebuf, sizeof ebuf), fallback);
                }
            } else {
                fprintf(stderr, "Lace error: unable to reserve a %zu-byte scratch arena for each of %u workers: %s\n",
                    reserve, n_workers, lace_strerror(err, ebuf, sizeof ebuf));
                exit(1);
            }
        }
    }

    if (verbosity) {
#if LACE_USE_HWLOC
        fprintf(stdout, "Lace startup: %u nodes, %u cores, %u logical processors, %d workers.\n", n_nodes, n_cores, n_pus, n_workers);
        // Print resulting CPU sets
        if (verbosity != 0) {
            for (unsigned int m = 0; m < n_workers; ++m) {
                int id;
                hwloc_bitmap_foreach_begin(id, cpusets[m]);
                hwloc_obj_t pu = hwloc_get_pu_obj_by_os_index(topo, (unsigned int)id);
                // find the core
                if (pu != 0) {
                    hwloc_obj_t core = hwloc_get_ancestor_obj_by_type(topo, HWLOC_OBJ_CORE, pu);
                    if (core != 0) {
                        printf("Lace startup: will pin worker thread %u to pu %u (on core %u)\n", m, pu->logical_index, core->logical_index);
                    }
                }
                hwloc_bitmap_foreach_end();
            }
        }
#else
        fprintf(stdout, "Lace startup: %u available cores, %u workers.\n", n_pus, n_workers);
#endif
    }

    // Prepare lace_init structure
    atomic_store_explicit(&lace_newframe.t, NULL, memory_order_relaxed);

    // Initialize counters for pie times
    us_elapsed_start();
    count_at_start = lace_gethrtime();

    // Per run: the worker ids taken, the workers that left, the workers in the steal loop
    worker_entered = malloc(n_workers * sizeof(atomic_int));
    if (worker_entered == NULL) {
        fprintf(stderr, "Lace error: unable to allocate memory for the workers!\n");
        exit(1);
    }
    for (unsigned int i = 0; i < n_workers; i++) atomic_init(&worker_entered[i], 0);
    atomic_store_explicit(&n_left, 0, memory_order_relaxed);
    atomic_store_explicit(&n_looping, 0, memory_order_relaxed);
}

/**
 * Start Lace and spawn worker threads.
 *
 * Allocates per-worker deques and launches @p n_workers threads.
 * Workers begin busy-waiting for tasks immediately. If LACE_BACKOFF
 * is enabled (the default), CPU usage drops to near zero after roughly
 * one second of inactivity.
 *
 * **Deque allocation.** Each worker's task deque is allocated as a
 * virtual memory mapping (mmap on Unix, VirtualAlloc on Windows).
 * Physical pages are committed lazily on first access, so a large
 * @p dqsize has no upfront memory cost.
 *
 * **Stack allocation.** Worker thread stacks are allocated explicitly
 * rather than by pthreads, with a guard page at the low end to catch
 * stack overflow. On Unix without hwloc, stacks are allocated with
 * mmap and MAP_NORESERVE; the kernel's first-touch policy places
 * physical pages on the NUMA node where the worker thread runs. When
 * LACE_USE_HWLOC is enabled, stacks are allocated with
 * hwloc_alloc_membind, which binds pages to the correct NUMA node at
 * allocation time. On Windows without hwloc, the OS manages stacks.
 * The @p stacksize parameter is rounded up to the system page size.
 *
 * **Thread pinning.** When LACE_USE_HWLOC is enabled, each worker
 * thread is pinned to a CPU core. Workers are distributed across
 * physical cores first (avoiding hyperthreading) before assigning
 * multiple workers to the same core.
 *
 * @param n_workers  Number of worker threads. Pass 0 to auto-detect
 *                   available cores (respects process affinity masks).
 * @param dqsize     Task deque size per worker (number of task slots).
 *                   Pass 0 for a default of 1 048 576. Each live SPAWN
 *                   that has not yet been SYNCed occupies one slot.
 *                   Since deques are backed by virtual memory, only
 *                   pages actually touched consume physical memory.
 * @param stacksize  Worker thread stack size in bytes, at least 16 MB.
 *                   Pass 0 for the default: a 1 GB reservation on 64-bit
 *                   systems where stack pages are committed on first touch
 *                   (not with LACE_DONT_OVERCOMMIT, not on Windows except
 *                   MSVC), otherwise RLIMIT_STACK clamped to 16-64 MB. If
 *                   the 1 GB stacks cannot be reserved (for example under
 *                   ulimit -v), Lace falls back to the latter.
 *
 * @see lace_stop
 */
void
lace_start(unsigned int _n_workers, size_t dequesize, size_t stacksize)
{
    lace_setup(_n_workers, dequesize, 0);

    const int default_stack = (stacksize == 0);
    if (default_stack) stacksize = lace_default_stack_size();
    if (stacksize < LACE_STACK_MIN) stacksize = LACE_STACK_MIN;

#if LACE_MSVC
    // Windows MSVC: the stack size is passed to thread creation directly, as
    // a reservation (see _beginthreadex below); it must fit in an unsigned.
    if (stacksize > ((size_t)1 << 31)) stacksize = (size_t)1 << 31;
#else

#if LACE_USE_HWLOC || (!defined(_WIN32) && !defined(__FreeBSD__))
    // Allocate the stacks ourselves: with hwloc bound to the worker's NUMA
    // node, otherwise with mmap so first-touch places pages on the right node
    // after pinning.
    worker_stacks = malloc(n_workers * sizeof(void*));
    if (worker_stacks == NULL) {
        fprintf(stderr, "Lace error: unable to allocate stack pointer array!\n");
        exit(1);
    }
    if (lace_alloc_stacks(stacksize) != 0) {
        int err = errno;
        size_t fallback = lace_small_stack_size();
        if (default_stack && fallback < stacksize && lace_alloc_stacks(fallback) == 0) {
            // e.g. a limit on address space (ulimit -v) or strict overcommit
            if (verbosity) {
                fprintf(stdout, "Lace startup: could not reserve %zu-byte stacks (%s), using %zu bytes.\n",
                    stacksize, strerror(err), fallback);
            }
        } else {
            fprintf(stderr, "Lace error: unable to allocate a %zu-byte stack for each of %u workers: %s\n",
                stacksize, n_workers, strerror(err));
            exit(1);
        }
    }
    stacksize = worker_stack_size - worker_stack_page_size;
#endif
#endif /* !LACE_MSVC */
    spawn_stacksize = stacksize; // where the system reserves the stacks (lace_spawn_worker)

    /* Report startup if verbose */
    if (verbosity) {
        fprintf(stdout, "Lace startup: creating %u worker threads with program stack %zu bytes.\n", n_workers, stacksize);
    }

#if !LACE_MSVC
    handles = (pthread_t*)malloc(n_workers * sizeof(*handles));
#else
    handles = (HANDLE*)malloc(n_workers * sizeof(*handles));
#endif

    is_running = 1; // before the threads start, as they enter

    /* Create worker 0; each worker creates the next two (lace_worker_thread) */
    lace_spawn_worker(0);

}

/**
 * Start Lace on the program's own threads (lace.h).
 */
void
lace_start_external(unsigned int _n_workers, size_t dequesize)
{
    lace_setup(_n_workers, dequesize, 1);
    if (verbosity) {
        fprintf(stdout, "Lace startup: %u workers on the program's own threads.\n", n_workers);
    }
    is_running = 1;
}

int lace_is_running(void)
{
    return is_running;
}


#if LACE_COUNT_EVENTS
static uint64_t ctr_all[CTR_MAX];
#endif

#if LACE_COUNT_EVENTS && !defined(_WIN32) && (LACE_USE_HWLOC || !defined(__FreeBSD__))
#define LACE_STACK_HIGH_WATER 1
/**
 * How much of worker @p i's stack has been touched since lace_start: from the
 * top of the stack down to its lowest resident page, found with mincore().
 * Untouched pages of the reservation are never resident. With transparent
 * huge pages the granularity can be 2 MB. Returns 0 if unknown.
 */
static size_t
lace_stack_high_water(unsigned int i)
{
    if (worker_stacks == NULL || worker_stacks[i] == NULL) return 0;
    size_t page = worker_stack_page_size;
    size_t npages = (worker_stack_size - page) / page;
    char *low = (char*)worker_stacks[i] + page; // above the guard page
#if defined(__linux__)
    unsigned char *vec = malloc(npages);
#else
    char *vec = malloc(npages);
#endif
    if (vec == NULL) return 0;
    size_t used = 0;
    if (mincore((void*)low, npages * page, vec) == 0) {
        for (size_t k = 0; k < npages; k++) {
            if (vec[k] & 1) {
                used = (npages - k) * page;
                break;
            }
        }
    }
    free(vec);
    return used;
}
#else
#define LACE_STACK_HIGH_WATER 0
#endif

/**
 * Reset the counters of Lace.
 */
void
lace_count_reset(void)
{
#if LACE_COUNT_EVENTS
    unsigned int i;
    size_t j;

    for (i=0;i<n_workers;i++) {
        for (j=0;j<CTR_MAX;j++) {
            workers_p[i]->ctr[j] = 0;
        }
    }

#if LACE_PIE_TIMES
    for (i=0;i<n_workers;i++) {
        workers_p[i]->time = lace_gethrtime();
        if (i != 0) workers_p[i]->level = 0;
    }

    us_elapsed_start();
    count_at_start = lace_gethrtime();
#endif
#endif
}

/**
 * Report counters to the given file.
 */
void
lace_count_report_file(FILE *file)
{
#if LACE_COUNT_EVENTS
    unsigned int i;
    size_t j;

    for (j=0;j<CTR_MAX;j++) ctr_all[j] = 0;
    for (i=0;i<n_workers;i++) {
        uint64_t *wctr = workers_p[i]->ctr;
        for (j=0;j<CTR_MAX;j++) {
            ctr_all[j] += wctr[j];
        }
    }

#if LACE_COUNT_TASKS
    for (i=0;i<n_workers;i++) {
        fprintf(file, "Tasks (%u): %" PRIu64 "\n", i, workers_p[i]->ctr[CTR_tasks]);
    }
    fprintf(file, "Tasks (sum): %" PRIu64 "\n", ctr_all[CTR_tasks]);
    fprintf(file, "\n");
#endif

#if LACE_COUNT_STEALS
    for (i=0;i<n_workers;i++) {
        fprintf(file, "Steals (%u): %" PRIu64 " good/%" PRIu64 " busy of %" PRIu64 " tries; leaps: %" PRIu64 " good/%" PRIu64 " busy of %" PRIu64 " tries\n", i,
            workers_p[i]->ctr[CTR_steals], workers_p[i]->ctr[CTR_steal_busy],
            workers_p[i]->ctr[CTR_steal_tries], workers_p[i]->ctr[CTR_leaps],
            workers_p[i]->ctr[CTR_leap_busy], workers_p[i]->ctr[CTR_leap_tries]);
    }
    fprintf(file, "Steals (sum): %" PRIu64 " good/%" PRIu64 " busy of %" PRIu64 " tries; leaps: %" PRIu64 " good/%" PRIu64 " busy of %" PRIu64 " tries\n", 
        ctr_all[CTR_steals], ctr_all[CTR_steal_busy],
        ctr_all[CTR_steal_tries], ctr_all[CTR_leaps],
        ctr_all[CTR_leap_busy], ctr_all[CTR_leap_tries]);
    fprintf(file, "\n");
#endif

#if LACE_COUNT_STEALS && LACE_COUNT_TASKS
    for (i=0;i<n_workers;i++) {
        if ((workers_p[i]->ctr[CTR_steals]+workers_p[i]->ctr[CTR_leaps]) > 0) {
            fprintf(file, "Tasks per steal (%u): %" PRIu64 "\n", i,
                workers_p[i]->ctr[CTR_tasks]/(workers_p[i]->ctr[CTR_steals]+workers_p[i]->ctr[CTR_leaps]));
        }
    }
    if ((ctr_all[CTR_steals]+ctr_all[CTR_leaps]) > 0) {
        fprintf(file, "Tasks per steal (sum): %" PRIu64 "\n", ctr_all[CTR_tasks]/(ctr_all[CTR_steals]+ctr_all[CTR_leaps]));
    }
    fprintf(file, "\n");
#endif

#if LACE_COUNT_SPLITS
    for (i=0;i<n_workers;i++) {
        fprintf(file, "Splits (%u): %" PRIu64 " shrinks, %" PRIu64 " grows, %" PRIu64 " outgoing requests\n", i,
            workers_p[i]->ctr[CTR_split_shrink], workers_p[i]->ctr[CTR_split_grow], workers_p[i]->ctr[CTR_split_req]);
    }
    fprintf(file, "Splits (sum): %" PRIu64 " shrinks, %" PRIu64 " grows, %" PRIu64 " outgoing requests\n",
        ctr_all[CTR_split_shrink], ctr_all[CTR_split_grow], ctr_all[CTR_split_req]);
    fprintf(file, "\n");
#endif

#if LACE_COUNT_STEALS
    fprintf(file, "Leap strays (sum): %" PRIu64 " of %" PRIu64 " leaps; transitive leaps: %" PRIu64 "\n",
        ctr_all[CTR_leap_strays], ctr_all[CTR_leaps], ctr_all[CTR_leap_trans]);
    fprintf(file, "\n");
#endif

#if LACE_STACK_HIGH_WATER
    {
        size_t max_used = 0;
        for (i=0;i<n_workers;i++) {
            size_t used = lace_stack_high_water(i);
            if (used > max_used) max_used = used;
            fprintf(file, "Stack high-water (%u): %zu KB\n", i, used / 1024);
        }
        fprintf(file, "Stack high-water (max): %zu KB of %zu KB\n", max_used / 1024,
            (worker_stack_size - worker_stack_page_size) / 1024);
        fprintf(file, "\n");
    }
#endif

#if LACE_PIE_TIMES
    uint64_t count_at_end = lace_gethrtime();
    double ms = (double)us_elapsed() / 1000.0;
    double count_per_ms = (double)(count_at_end - count_at_start) / ms;
    double dcpm = (double)count_per_ms;

    uint64_t sum_count;
    sum_count = ctr_all[CTR_init] + ctr_all[CTR_wapp] + ctr_all[CTR_lapp] + ctr_all[CTR_wsteal] + ctr_all[CTR_lsteal]
              + ctr_all[CTR_close] + ctr_all[CTR_wstealsucc] + ctr_all[CTR_lstealsucc] + ctr_all[CTR_wsignal]
              + ctr_all[CTR_lsignal];

    fprintf(file, "Measured clock (tick) frequency: %.2f GHz\n", count_per_ms / 1000000.0);
    fprintf(file, "Aggregated time per pie slice, total time: %.2f CPU seconds\n\n", (double)sum_count / (1000*dcpm));

    for (i=0;i<n_workers;i++) {
        fprintf(file, "Startup time (%d):    %10.2f ms\n", i, (double)workers_p[i]->ctr[CTR_init] / dcpm);
        fprintf(file, "Steal work (%d):      %10.2f ms\n", i, (double)workers_p[i]->ctr[CTR_wapp] / dcpm);
        fprintf(file, "Leap work (%d):       %10.2f ms\n", i, (double)workers_p[i]->ctr[CTR_lapp] / dcpm);
        fprintf(file, "Steal overhead (%d):  %10.2f ms\n", i, (double)(workers_p[i]->ctr[CTR_wstealsucc]+workers_p[i]->ctr[CTR_wsignal]) / dcpm);
        fprintf(file, "Leap overhead (%d):   %10.2f ms\n", i, (double)(workers_p[i]->ctr[CTR_lstealsucc]+workers_p[i]->ctr[CTR_lsignal]) / dcpm);
        fprintf(file, "Steal search (%d):    %10.2f ms\n", i, (double)(workers_p[i]->ctr[CTR_wsteal]-workers_p[i]->ctr[CTR_wstealsucc]-workers_p[i]->ctr[CTR_wsignal]) / dcpm);
        fprintf(file, "Leap search (%d):     %10.2f ms\n", i, (double)(workers_p[i]->ctr[CTR_lsteal]-workers_p[i]->ctr[CTR_lstealsucc]-workers_p[i]->ctr[CTR_lsignal]) / dcpm);
        fprintf(file, "Backoff time (%d):    %10.2f ms\n", i, (double)workers_p[i]->ctr[CTR_backoff] / dcpm);
        fprintf(file, "Exit time (%d):       %10.2f ms\n", i, (double)workers_p[i]->ctr[CTR_close] / dcpm);
        fprintf(file, "\n");
    }

    fprintf(file, "Startup time (sum):    %10.2f ms\n", (double)ctr_all[CTR_init] / dcpm);
    fprintf(file, "Steal work (sum):      %10.2f ms\n", (double)ctr_all[CTR_wapp] / dcpm);
    fprintf(file, "Leap work (sum):       %10.2f ms\n", (double)ctr_all[CTR_lapp] / dcpm);
    fprintf(file, "Steal overhead (sum):  %10.2f ms\n", (double)(ctr_all[CTR_wstealsucc]+ctr_all[CTR_wsignal]) / dcpm);
    fprintf(file, "Leap overhead (sum):   %10.2f ms\n", (double)(ctr_all[CTR_lstealsucc]+ctr_all[CTR_lsignal]) / dcpm);
    fprintf(file, "Steal search (sum):    %10.2f ms\n", (double)(ctr_all[CTR_wsteal]-ctr_all[CTR_wstealsucc]-ctr_all[CTR_wsignal]) / dcpm);
    fprintf(file, "Leap search (sum):     %10.2f ms\n", (double)(ctr_all[CTR_lsteal]-ctr_all[CTR_lstealsucc]-ctr_all[CTR_lsignal]) / dcpm);
    fprintf(file, "Backoff time (sum):    %10.2f ms\n", (double)ctr_all[CTR_backoff] / dcpm);
    fprintf(file, "Exit time (sum):       %10.2f ms\n", (double)ctr_all[CTR_close] / dcpm);
    fprintf(file, "\n" );
#endif
#endif
    (void)file;
    return;
}

/**
 * End Lace. All Workers are signaled to quit.
 * This function waits until all threads are done, then returns.
 */
void lace_stop(void)
{
    if (lace_thread_worker != NULL) {
        fprintf(stderr, "Lace error: lace_stop called by Lace worker %u "
            "(call lace_worker_leave first, and never from inside a task)\n",
            (unsigned int)lace_thread_worker->worker);
        exit(1);
    }

    atomic_store_explicit(&lace_quits, 1, memory_order_relaxed);

    /* Wake all sleeping workers so they observe the quit flag */
    atomic_fetch_add_explicit(&wake_futex, 1, memory_order_release);
    lace_futex_wake_all(&wake_futex);

    /* Every worker has left: Lace's own threads before they end, the
     * program's threads with lace_worker_leave once their loops returned */
    while (atomic_load_explicit(&n_left, memory_order_acquire) < n_workers) lace_sleep_us(50);

    /* Lace's own threads: the workers created them (lace_worker_thread), and
     * have all left, so every handle is set */
    if (!external_mode) {
        for (unsigned int i = 0; i < n_workers; i++) {
#if !LACE_MSVC
            pthread_join(handles[i], NULL);
#else
            WaitForSingleObject(handles[i], INFINITE);
            CloseHandle(handles[i]);
#endif
        }
        free(handles);
        handles = NULL;
    }

#if LACE_COUNT_EVENTS
    // before the stacks are freed: the report includes their high-water marks
    lace_count_report_file(stdout);
#endif

#if LACE_USE_HWLOC || (!defined(_WIN32) && !defined(__FreeBSD__))
    if (worker_stacks != NULL) {
        lace_free_stacks(n_workers);
        free(worker_stacks);
        worker_stacks = NULL;
    }
#endif

    // finally, destroy the barriers
    lace_barrier_destroy();

    for (unsigned int i=0; i<n_workers; i++) {
        if (workers_p[i]->scratch_base != NULL) {
            lace_scratch_release(workers_p[i]->scratch_base,
                                 (size_t)(workers_p[i]->scratch_reserve_end
                                          - workers_p[i]->scratch_base));
        }
#if defined(_WIN32)
        VirtualFree(workers_memory[i], 0, MEM_RELEASE);
#else
        munmap(workers_memory[i], workers_memory_size);
#endif
    }
    free(scratch_arenas);
    scratch_arenas = NULL;

#if defined(_MSC_VER) || defined(__MINGW64_VERSION_MAJOR)
    _aligned_free(workers);
    _aligned_free(workers_p);
    _aligned_free(workers_memory);
#elif defined(__MINGW32__)
    __mingw_aligned_free(workers);
    __mingw_aligned_free(workers_p);
    __mingw_aligned_free(workers_memory);
#else
    free(workers);
    free(workers_p);
    free(workers_memory);
#endif

    workers = 0;
    workers_p = 0;
    workers_memory = 0;

#if defined(__linux__) && !LACE_USE_HWLOC
    free(pin_sets);
    pin_sets = NULL;
#endif


#if defined(_MSC_VER) || defined(__MINGW64_VERSION_MAJOR)
    _aligned_free(idle_flags);
#elif defined(__MINGW32__)
    __mingw_aligned_free(idle_flags);
#else
    free(idle_flags);
#endif
    idle_flags = NULL;

    free((void*)worker_entered); // MSVC: no implicit loss of _Atomic (C4090)
    worker_entered = NULL;
    external_mode = 0;

    is_running = 0;
}

/**
 * Execute the given <root> task in a new frame (synchronizing with all Lace threads)
 * 1) Creates a new frame
 * 2) LACE BARRIER
 * 3) Execute the <root> task
 * 4) LACE BARRIER
 * 5) Restore the old frame
 */
static void
lace_exec_in_new_frame(lace_worker* _lace_worker, lace_task *root)
{
    lace_task *_lace_dq_head = _lace_worker->head;

    TailSplitNA old;
    uint8_t old_as;

    // save old tail, split, allstolen and initiate new frame
    {
        lace_worker_public *wt = _lace_worker->_public;

        old_as = wt->allstolen;
        wt->allstolen = 1;
        old.ts.split = atomic_load_explicit(&wt->ts.ts.split, memory_order_relaxed);
        atomic_store_explicit(&wt->ts.ts.split, 0, memory_order_relaxed);
        atomic_thread_fence(memory_order_seq_cst);
        old.ts.tail = atomic_load_explicit(&wt->ts.ts.tail, memory_order_relaxed);

        TailSplitNA ts_new;
        ts_new.ts.tail = (uint32_t)(_lace_dq_head - _lace_worker->dq);
        ts_new.ts.split = (uint32_t)(_lace_dq_head - _lace_worker->dq);
        atomic_store_explicit(&wt->ts.v, ts_new.v, memory_order_relaxed);

        _lace_worker->split = _lace_dq_head;
        _lace_worker->allstolen = 1;
    }

    // wait until all workers are ready
    lace_barrier();

    // execute task
    root->f(_lace_worker, root);

    // wait until all workers are back (else they may steal from previous frame)
    lace_barrier();

    // restore tail, split, allstolen
    {
        lace_worker_public *wt = _lace_worker->_public;
        wt->allstolen = old_as;
        atomic_store_explicit(&wt->ts.v, old.v, memory_order_relaxed);
        _lace_worker->split = _lace_worker->dq + old.ts.split;
        _lace_worker->allstolen = old_as;
    }
}

/**
 * This method is called when there is a new frame (NEWFRAME or TOGETHER)
 * Each Lace worker executes lace_yield to execute the task in a new frame.
 */
void
lace_yield(lace_worker *worker)
{
    // make a local copy of the task
    lace_task _t;
    memcpy(&_t, atomic_load_explicit(&lace_newframe.t, memory_order_relaxed), sizeof(lace_task));

    // wait until all workers have made a local copy
    lace_barrier();

    /* The new frame's steal loop sets the idle flag; the worker returns to
     * what it did before (a task, or its own steal loop). */
    unsigned char was_idle = atomic_load_explicit(&idle_flags[worker->worker].idle, memory_order_relaxed);
    lace_exec_in_new_frame(worker, &_t);
    lace_set_idle(worker, was_idle);
}

/**
 * Root task for the TOGETHER method.
 * Ensures after executing, to steal random tasks until done.
 */
TASK(void, lace_together_root, lace_task*, t, atomic_int*, finished)

void
lace_together_root_CALL(lace_worker* lw, lace_task* t, atomic_int* finished)
{
    // run the root task
    t->f(lw, t);

    // signal out completion
    atomic_fetch_sub_explicit(finished, 1, memory_order_relaxed);

    // while threads aren't done, steal randomly
    while (atomic_load_explicit(finished, memory_order_relaxed) != 0) lace_steal_random(lw);
}

TASK(void, lace_wrap_together, lace_task*, task)

void
lace_wrap_together_CALL(lace_worker* worker, lace_task* task)
{
    /* synchronization integer (decrease by 1 when done...) */
    atomic_int done = (int)n_workers;

    /* wrap task in lace_together_root */
    lace_task _t2;
    task_lace_together_root *t2 = (task_lace_together_root *)&_t2;
    t2->f = lace_together_root_WRAP;
    atomic_store_explicit(&t2->thief, THIEF_TASK, memory_order_relaxed);
    t2->d.args.arg_1 = task;
    t2->d.args.arg_2 = &done;
    atomic_thread_fence(memory_order_release); // no StoreStore reordering

    /* now try to be the one who sets it! */
    while (1) {
        lace_task *expected = 0;
        if (atomic_compare_exchange_weak(&lace_newframe.t, &expected, &_t2)) break;
        lace_yield(worker);
    }

    /* Wake sleeping workers so they see the newframe and enter lace_yield */
    atomic_fetch_add_explicit(&wake_futex, 1, memory_order_release);
    lace_futex_wake_all(&wake_futex);

    // wait until other workers have made a local copy
    lace_barrier();

    // reset the newframe struct
    atomic_store_explicit(&lace_newframe.t, NULL, memory_order_relaxed);

    lace_exec_in_new_frame(worker, &_t2);
}

TASK(void, lace_newframe_root, lace_task*, t, atomic_int*, done)

void
lace_newframe_root_CALL(lace_worker *lw, lace_task* t, atomic_int *done)
{
    t->f(lw, t);
    atomic_thread_fence(memory_order_release); // no StoreStore reordering
    atomic_store_explicit(done, 1, memory_order_relaxed);
}

TASK(void, lace_wrap_newframe, lace_task*, task)

void
lace_wrap_newframe_CALL(lace_worker* worker, lace_task* task)
{
    /* synchronization integer (set to 1 when done...) */
    atomic_int done = 0;

    /* create the lace_steal_loop task for the other workers */
    lace_task _s;
    task_lace_steal_loop *s = (task_lace_steal_loop *)&_s;
    s->f = &lace_steal_loop_WRAP;
    atomic_store_explicit(&s->thief, THIEF_TASK, memory_order_relaxed);
    s->d.args.arg_1 = &done;
    atomic_thread_fence(memory_order_release); // no StoreStore reordering

    /* now try to be the one who sets it! */
    while (1) {
        lace_task *expected = 0;
        if (atomic_compare_exchange_weak(&lace_newframe.t, &expected, &_s)) break;
        lace_yield(worker);
    }

    /* Wake sleeping workers so they see the newframe and enter lace_yield */
    atomic_fetch_add_explicit(&wake_futex, 1, memory_order_release);
    lace_futex_wake_all(&wake_futex);

    // wait until other workers have made a local copy
    lace_barrier();

    // reset the newframe struct, then wrap and run ours
    atomic_store_explicit(&lace_newframe.t, NULL, memory_order_relaxed);

    /* wrap task in lace_newframe_root */
    lace_task _t2;
    task_lace_newframe_root *t2 = (task_lace_newframe_root *)&_t2;
    t2->f = lace_newframe_root_WRAP;
    atomic_store_explicit(&t2->thief, THIEF_TASK, memory_order_relaxed);
    t2->d.args.arg_1 = task;
    t2->d.args.arg_2 = &done;

    lace_exec_in_new_frame(worker, &_t2);
}

void
lace_run_together(lace_task *t)
{
    lace_worker* self = lace_get_worker();
    if (self != 0) {
        lace_wrap_together_CALL(self, t);
    } else {
        lace_wrap_together(t);
    }
}

void
lace_run_newframe(lace_task *t)
{
    lace_worker* self = lace_get_worker();
    if (self != 0) {
        lace_wrap_newframe_CALL(self, t);
    } else {
        lace_wrap_newframe(t);
    }
}

/**
 * Called by _SPAWN functions when the lace_task stack is full.
 */
void
lace_abort_stack_overflow(void)
{
    fprintf(stderr, "Lace fatal error: lace_task stack overflow! Aborting.\n");
    exit(-1);
}

LACE_NO_SANITIZE_THREAD
lace_worker_public*
lace_steal(lace_worker *self, lace_worker_public *victim)
{
    if (victim != NULL && !victim->allstolen) {
        TailSplitNA ts;
        ts.v = atomic_load_explicit(&victim->ts.v, memory_order_relaxed);
        if (ts.ts.tail < ts.ts.split) {
            TailSplitNA ts_new;
            ts_new.v = ts.v;
            ts_new.ts.tail++;
            /* A scalar expected value, not &ts.v: the union would have to live
             * on the stack, and with -fstack-protector-strong that adds a
             * canary check to every steal attempt. */
            uint64_t expected = ts.v;
            if (atomic_compare_exchange_weak(&victim->ts.v, &expected, ts_new.v)) {
                // Stolen
                lace_task *t = &victim->dq[ts.ts.tail];
#if LACE_COUNT_STEALS
                /* A leap normally steals a descendant of the awaited task,
                 * which cannot complete before that descendant has run. So
                 * if it has completed already, the thief finished it and
                 * moved on before our steal: this task is unrelated. */
                if (self->leap_task != NULL) {
                    if (atomic_load_explicit(&self->leap_task->thief, memory_order_relaxed) == THIEF_COMPLETED) {
                        PR_COUNTSTEALS(self, CTR_leap_strays);
                    }
                    self->leap_task = NULL;
                }
#endif
                atomic_store_explicit(&t->thief, self->_public, memory_order_relaxed);
                const uint64_t stolen_at = lace_now_ns();
                int woke = 0;
                /* A steal from the steal loop (the thief is idle): wake a
                 * sleeping worker now, rather than after the stolen task,
                 * which may be most of the work, if the victim has tasks left
                 * or no sentinel watches for the work this task will spawn (a
                 * region after a long idle). */
                if ((ts_new.ts.tail < ts_new.ts.split ||
                     atomic_load_explicit(&n_sentinels, memory_order_relaxed) == 0) &&
                    atomic_load_explicit(&idle_flags[self->worker].idle, memory_order_relaxed) &&
                    atomic_load_explicit(&n_sleeping, memory_order_relaxed) > 0) {
                    atomic_fetch_add_explicit(&wake_futex, 1, memory_order_release);
                    woke = 1;
                    /* As many as the victim has tasks left (at most the
                     * sleepers): a large region gets all workers in one round
                     * of wake-ups instead of a chain of them. */
                    int left = (int)(ts_new.ts.split - ts_new.ts.tail);
                    if (left < 1) left = 1;
                    int sl = atomic_load_explicit(&n_sleeping, memory_order_relaxed);
                    lace_futex_wake_n(&wake_futex, left < sl ? left : sl);
                }
                lace_set_idle(self, 0);
                lace_time_event(self, 1);
                const uint64_t task_t0 = woke ? lace_now_ns() : stolen_at;
                t->f(self, t);
                /* After the task: steals nested in it set these too */
                lace_last_stolen_ns = lace_now_ns() - task_t0;
                lace_last_stolen_at = stolen_at;
                lace_time_event(self, 2);
                atomic_store_explicit(&t->thief, THIEF_COMPLETED, memory_order_release);
                lace_time_event(self, 8);
                return LACE_STOLEN;
            }

            lace_time_event(self, 7);
            return LACE_BUSY;
        }

        if (victim->movesplit == 0) {
            victim->movesplit = 1;
            PR_COUNTSPLITS(self, CTR_split_req);
        }
    }

    lace_time_event(self, 7);
    return LACE_NOWORK;
}

LACE_NO_SANITIZE_THREAD
int
lace_shrink_shared(lace_worker *w)
{
    lace_worker_public *wt = w->_public;
    TailSplitNA ts; /* Use non-atomic version to emit better code */
    ts.v = atomic_load_explicit(&wt->ts.v, memory_order_relaxed); /* Force in 1 memory read */
    uint32_t tail = ts.ts.tail;
    uint32_t split = ts.ts.split;

    if (tail != split) {
        uint32_t newsplit = (tail + split)/2;
        atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed); /* emit normal write */
        atomic_thread_fence(memory_order_seq_cst); /* prevent StoreLoad rerdering */
        tail = atomic_load_explicit(&wt->ts.ts.tail, memory_order_relaxed); /* emit normal read */
        if (tail != split) {
            if (LACE_UNLIKELY(tail > newsplit)) {
                newsplit = (tail + split) / 2;
                atomic_store_explicit(&wt->ts.ts.split, newsplit, memory_order_relaxed); /* emit normal write */
            }
            w->split = w->dq + newsplit;
            PR_COUNTSPLITS(w, CTR_split_shrink);
            return 0;
        } else {
            w->split = w->dq + split;
        }
    }

    wt->allstolen = 1;
    w->allstolen = 1;
    return 1;
}

void
lace_leapfrog(lace_worker *worker)
{
    lace_time_event(worker, 3);
    lace_task *t = worker->head;
    lace_worker_public *thief = atomic_load_explicit(&t->thief, memory_order_relaxed);
    if (thief != THIEF_COMPLETED) {
        while ((size_t)thief <= 1) thief = atomic_load_explicit(&t->thief, memory_order_relaxed);

        /* PRE-LEAP: increase head again */
        worker->head += 1;

        /* Publish whom we wait for, so that workers waiting for us can steal
         * from our thief too; a nested leapfrog restores the outer value.
         * Only once the thief had nothing to steal: most waits end before
         * that, and the store costs the thieves that read this cache line a
         * miss. */
        lace_worker_public *outer_wait = atomic_load_explicit(&worker->_public->waiting_on, memory_order_relaxed);
        int published = 0;
        /* The worker to steal from: the thief, or a worker down the chain of
         * workers that wait for each other, starting at the thief. They all
         * work on parts of the awaited task (bar a race with a worker that
         * just stopped waiting), so stealing from them cannot delay the join. */
        lace_worker_public *victim = thief;
        int hops = 0;

        /* Now leapfrog */
        while (thief != THIEF_COMPLETED) {
            PR_COUNTSTEALS(worker, CTR_leap_tries);
#if LACE_COUNT_STEALS
            worker->leap_task = t;
#endif
            lace_worker_public *res = lace_steal(worker, victim);
#if LACE_COUNT_STEALS
            worker->leap_task = NULL;
#endif
            if (res == LACE_NOWORK) {
                if (!published) {
                    atomic_store_explicit(&worker->_public->waiting_on, thief, memory_order_relaxed);
                    published = 1;
                }
                /* Next time, try the worker that this victim waits for. One
                 * load from the line the steal attempt just read. */
                lace_worker_public *next = hops < LACE_LEAP_HOPS ?
                    atomic_load_explicit(&victim->waiting_on, memory_order_relaxed) : NULL;
                if ((uintptr_t)next > (uintptr_t)THIEF_COMPLETED && next != worker->_public) {
                    victim = next;
                    hops++;
                } else {
                    victim = thief;
                    hops = 0;
                }
                lace_check_yield(worker);
            } else if (res == LACE_STOLEN || res == LACE_STOLEN_LAST) {
                PR_COUNTSTEALS(worker, CTR_leaps);
                if (hops > 0) { PR_COUNTSTEALS(worker, CTR_leap_trans); }
            } else if (res == LACE_BUSY) {
                PR_COUNTSTEALS(worker, CTR_leap_busy);
            }
            atomic_thread_fence(memory_order_acquire); // no LoadLoad or LoadStore reordering
            thief = atomic_load_explicit(&t->thief, memory_order_relaxed);
        }
        if (published) atomic_store_explicit(&worker->_public->waiting_on, outer_wait, memory_order_relaxed);

        /* POST-LEAP: really pop the finished task */
        atomic_thread_fence(memory_order_acquire);
        if (worker->allstolen == 0) {
            /* Assume: tail = split = head (pre-pop) */
            /* Now we do a real pop ergo either decrease tail,split,head or declare allstolen */
            lace_worker_public *wt = worker->_public;
            wt->allstolen = 1;
            worker->allstolen = 1;
        }
        worker->head -= 1;
    }

    /*compiler_barrier();*/
    atomic_thread_fence(memory_order_acquire);
    atomic_store_explicit(&t->thief, THIEF_EMPTY, memory_order_relaxed);
    lace_time_event(worker, 4);
}

int
lace_sync(lace_worker *w, lace_task *head)
{
    /* note: head is decreased prior to entering lace_sync,
       so w->split > head just means w->split == head + 1 */
    if ((w->allstolen) || (w->split > head && lace_shrink_shared(w))) {
        lace_leapfrog(w);
        return 1;
    }

    lace_worker_public *wt = w->_public;
    if (wt->movesplit) {
        lace_task *t = w->split;
        ptrdiff_t diff = head - t;
        diff = (diff + 1) / 2;
        lace_task *newsplit = t + diff;
        w->split = newsplit;
        atomic_store_explicit(&wt->ts.ts.split, (uint32_t)(newsplit-w->dq), memory_order_release);
        wt->movesplit = 0;
        PR_COUNTSPLITS(w, CTR_split_grow);
    }

    return 0;
}

void
lace_drop_slow(lace_worker *w, lace_task *head)
{
    if ((w->allstolen) || (w->split > head && lace_shrink_shared(w))) lace_leapfrog(w);
}

void
lace_drop(lace_worker *_lace_worker)
{
    lace_task* lace_head = _lace_worker->head - 1;
    _lace_worker->head = lace_head;
    if (LACE_LIKELY(0 == _lace_worker->_public->movesplit)) {
        if (LACE_LIKELY(_lace_worker->split <= lace_head)) {
            return;
        }
    }
    lace_drop_slow(_lace_worker, lace_head);
}

#if defined(_WIN32)
void lace_sleep_us(int64_t microseconds)
{
    if (microseconds <= 0) return;

    // Cache QPC frequency once
    static LARGE_INTEGER qpc_freq;
    static LONG qpc_init = 0;
    if (qpc_init == 0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        qpc_freq = f;
        InterlockedExchange(&qpc_init, 1);
    }

    // For very short waits, a spin is usually better than Sleep()/timers.
    const uint32_t SPIN_THRESHOLD_US = 300;

    if (microseconds > SPIN_THRESHOLD_US) {
        // Reuse a per-thread waitable timer to avoid Create/Close per call
        static LACE_TLS HANDLE tls_timer = NULL;

        if (tls_timer == NULL) {
            tls_timer = CreateWaitableTimerExW(
                NULL, NULL,
                0, /* optionally CREATE_WAITABLE_TIMER_HIGH_RESOLUTION if you want to try */
                TIMER_MODIFY_STATE | SYNCHRONIZE
            );
        }

        if (tls_timer) {
            // Clamp to avoid signed overflow in 100ns units
            uint64_t us = (uint64_t)microseconds;
            if (us > (uint64_t)(INT64_MAX / 10)) us = (uint64_t)(INT64_MAX / 10);

            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)(10ULL * us); // relative, 100ns units
            if (SetWaitableTimer(tls_timer, &due, 0, NULL, NULL, FALSE)) {
                WaitForSingleObject(tls_timer, INFINITE);
                return;
            }
        }

        // Fallback
        Sleep((DWORD)((microseconds + 999) / 1000));
        return;
    }

    // Spin-wait using QPC for sub-ms delays
    LARGE_INTEGER start, now;
    QueryPerformanceCounter(&start);

    const int64_t target_ticks =
        ((int64_t)microseconds * (int64_t)qpc_freq.QuadPart) / 1000000LL;

    const int64_t deadline = start.QuadPart + target_ticks;

    do {
        YieldProcessor();
        QueryPerformanceCounter(&now);
    } while (now.QuadPart < deadline);
}
#endif
