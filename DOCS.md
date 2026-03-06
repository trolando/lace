# Lace API Reference

`lace.h` provides the public API for defining tasks and controlling the Lace
work-stealing framework.

---

## Choosing a Lace variant

Lace comes in three variants that trade task struct size against available
parameter space:

| Header | CMake target | Task size | Space for parameters + result |
|--------|-------------|-----------|-------------------------------|
| `lace32.h` | `lace::lace32` | 32 bytes | 16 bytes |
| `lace.h` | `lace::lace` | 64 bytes | 48 bytes |
| `lace128.h` | `lace::lace128` | 128 bytes | 112 bytes |

The 16-byte overhead is fixed (function pointer and thief status field). A
`static_assert` in the generated code will catch it at compile time if a
task's parameters and return type exceed the available space.

The standard `lace.h` variant supports up to 10 parameters. The `lace128.h`
variant supports up to 14 parameters. The `lace32.h` variant is primarily
intended for architectures with 32-byte cache lines.

---

## Defining Tasks

Tasks are declared with the `TASK_N` family of macros, where `N` is the number
of parameters. Place the macro in a header or at the top of a source file; it
generates the task descriptor and all associated functions. Then provide the
task body as a regular C function named `NAME_CALL`.

```c
TASK_0(int, my_task)
int my_task_CALL(lace_worker* lw) { ... }

TASK_1(int, fibonacci, int, n)
int fibonacci_CALL(lace_worker* lw, int n) { ... }
```

For `void` return types, use the `VOID_TASK_N` variants:

```c
VOID_TASK_1(my_void_task, int, n)
void my_void_task_CALL(lace_worker* lw, int n) { ... }

VOID_TASK_2(process, int*, data, int, size)
void process_CALL(lace_worker* lw, int* data, int size) { ... }
```

Each `TASK_N(RTYPE, NAME, ...)` macro generates the following functions:

| Function | Description |
|----------|-------------|
| `NAME_CALL(lw, ...)` | Your task body — implement this |
| `NAME(...)` | Run the task, blocking until done; works from inside or outside a Lace worker |
| `NAME_SPAWN(lw, ...)` | Fork: push a task onto the deque so it can be stolen; returns a pointer to the task |
| `NAME_SYNC(lw)` | Join: retrieve the result of the last spawned task (LIFO order) |
| `NAME_DROP(lw)` | Drop: cancel the last spawned task if not yet stolen, or discard its result if already stolen |
| `NAME_NEWFRAME(...)` | Interrupt all workers and run this task (see below) |
| `NAME_TOGETHER(...)` | Interrupt all workers and run a copy on each worker (see below) |

The `lace_worker*` pointer passed to `_CALL` must not be modified. It is
required by `SPAWN`, `SYNC`, and other Lace operations.

### SPAWN and SYNC

`SPAWN` and `SYNC` must be matched and used in **LIFO order**: if you spawn A then B, you must sync B before A. Syncing out of order is undefined behavior.

Each `SPAWN` pushes a task onto the deque where it can be stolen by another worker. `SYNC` retrieves the result of the last spawned task — waiting for it if stolen, or executing it directly if not.

```c
int fibonacci_CALL(lace_worker* lw, int n)
{
    if (n < 2) return n;
    fibonacci_SPAWN(lw, n-1);         // push onto deque (may be stolen)
    int a = fibonacci_CALL(lw, n-2);  // execute directly
    int b = fibonacci_SYNC(lw);       // retrieve spawned result
    return a + b;
}
```

### Calling NAME() from any context

`NAME(...)` can be called from both inside and outside a Lace worker thread. If called from inside a worker, it detects this automatically and calls `NAME_CALL` directly, skipping task submission entirely. This means you can write library code that calls `NAME()` without knowing whether the caller is a Lace worker.

### Dropping a spawned task

Instead of `SYNC`, use `NAME_DROP(lw)` to abandon the last spawned task. If the task has not yet been stolen, it is cancelled and never executed. If it has already been stolen, the thief will still complete it but the result is discarded. Like `SYNC`, `DROP` must follow LIFO order relative to other `SPAWN`/`SYNC`/`DROP` calls.

```c
my_task_SPAWN(lw, arg);
// ... decide we don't need the result
my_task_DROP(lw);
```

### Interrupting workers

Two special run modes interrupt currently executing tasks at the next steal
point (i.e. at `SYNC` or when idle):

**`NAME_NEWFRAME(...)`** — halts all workers and runs the given task on the
worker pool. The current task frame is suspended and resumed after the new
task completes. Typical use: stop-the-world garbage collection.

**`NAME_TOGETHER(...)`** — halts all workers and runs a copy of the given task
on *every* worker simultaneously. All workers start together and all complete
together (barrier semantics). Typical use: per-worker initialization of
thread-local state.

Long-running tasks should call `lace_check_yield(lw)` periodically to
cooperate with interruptions.

### Example

```c
#include <lace.h>
#include <stdio.h>

TASK_1(int, fibonacci, int, n)

int fibonacci_CALL(lace_worker* lw, int n)
{
    if (n < 2) return n;
    fibonacci_SPAWN(lw, n-1);
    int a = fibonacci_CALL(lw, n-2);
    int b = fibonacci_SYNC(lw);
    return a + b;
}

int main(void)
{
    int n_workers = 0;  // 0 workers = use all available cores
    int dqsize = 0;     // use default task deque size
    int stacksize = 0;  // use default program stack size

    lace_start(n_workers, dqsize, stacksize);
    int result = fibonacci(42);   // run from outside Lace
    printf("fibonacci(42) = %d\n", result);
    lace_stop();
}
```

See the [benchmarks](benchmarks/) directory for more examples.

---

## Lifecycle

These functions must be called from outside the Lace framework, i.e. not from
within a Lace worker thread.

### `void lace_start(unsigned int n_workers, size_t dqsize, size_t stacksize)`

Start Lace and spawn worker threads.

- `n_workers`: number of worker threads; `0` auto-detects available cores
- `dqsize`: task deque size per worker in number of tasks; `0` uses a default of 100K tasks
- `stacksize`: worker thread stack size; `0` uses the minimum of 16 MB and the calling thread's stack size

The deque size limits recursion depth in work-stealing terms: each live `SPAWN` that has not yet been `SYNC`ed occupies one slot. For algorithms with very deep recursion and little stealing (e.g. branch-and-bound on a single worker), increase `dqsize` accordingly. When `LACE_USE_MMAP` is enabled, deques are allocated as virtual memory and physical pages are committed lazily — so a large `dqsize` has no upfront memory cost and it is safe to be generous.

Workers begin busy-waiting for tasks immediately. If `LACE_BACKOFF` is enabled
(the default), CPU usage drops to near 0% after roughly one second of
inactivity.

If `LACE_USE_MMAP` is set, deques are allocated in virtual memory and physical
pages are committed lazily by the OS. If `LACE_USE_HWLOC` is set, worker
threads are pinned to CPU cores.

---

### `void lace_stop(void)`

Stop all workers and free resources. May be called from any thread that is not a Lace worker. Do not call from a signal handler — the implementation calls `free`, `munmap`, or `VirtualFree` depending on platform and build configuration, none of which are async-signal-safe.

---

### `int lace_is_running(void)`

Returns `1` if Lace is currently running, `0` otherwise.

---

### `void lace_set_verbosity(int level)`

Set the verbosity level. Call this before `lace_start`.

- `0`: no output (default)
- `1`: print startup information

---

## Worker Context

### `unsigned int lace_worker_count(void)`

Returns the number of Lace worker threads.

---

### `int lace_is_worker(void)`

Returns `1` if called from a Lace worker thread, `0` otherwise.

---

### `lace_worker* lace_get_worker(void)`

Returns a pointer to the current worker's private data, or `NULL` if not called
from a Lace worker thread.

---

### `int lace_worker_id(void)`

Returns the current worker's integer ID (0-based), or `-1` if not called from
a Lace worker thread.

---

### `uint64_t lace_rng(lace_worker* lw)`

Thread-local pseudo-random number generator. Each worker has its own RNG state,
avoiding contention on a shared RNG.

---

## Task Operations

### `void lace_barrier(void)`

Collective barrier: all workers block until every worker has reached this call. Must be called from inside a Lace task.

> **Important:** `lace_barrier` requires that every worker will reach the barrier. If any worker is blocked waiting on a `SYNC` for a task that has not yet been stolen, and the other workers are all inside `lace_barrier`, the system will deadlock. Only use `lace_barrier` when you can guarantee all workers are free to reach it — typically after all outstanding spawns have been synced.

---

### `NAME_DROP(lw)`

Drop the last spawned `NAME` task without retrieving its result. If the task has not been stolen, it is cancelled and never executed. If it has already been stolen, the result is discarded once the thief completes it. Must be paired with a prior `NAME_SPAWN` and follows the same LIFO ordering requirement as `NAME_SYNC`.

---

### `int lace_is_stolen_task(lace_task* t)`

Returns `1` if the given task has been stolen by another worker.

---

### `int lace_is_completed_task(lace_task* t)`

Returns `1` if the given task has been completed.

---

### `lace_task_result(t)`

Macro that returns a pointer to the result storage inside the given `lace_task`.
The result is available after `lace_is_completed_task` returns `1`.

---

### `void lace_steal_random(lace_worker* lw)`

Attempt to steal and execute a task from a randomly chosen worker. This is a low-level function intended for the uncommon case where a Lace task needs to block on an external condition (e.g. waiting for I/O or a lock) and wants to keep its worker productive in the meantime. In normal fork-join code you should not need to call this — the framework handles work distribution automatically through `SYNC`.

---

### `void lace_check_yield(lace_worker* lw)`

Check whether a `NEWFRAME` or `TOGETHER` interruption is pending, and yield
if so. Call periodically from long-running tasks to cooperate with
interruptions.

---

### `void lace_make_all_shared(void)`

Mark all tasks on the current worker's deque as shared (stealable). Normally
only tasks up to the split point are stealable; this moves the split to the
head, making everything available to thieves.

---

### `lace_task* lace_get_head(void)`

Returns the current head pointer of the calling worker's deque.

---

## Statistics

Statistics functions report data collected by the optional `LACE_COUNT_*` and
`LACE_PIE_TIMES` build options. If none of these are enabled, the report will
be empty.

### `void lace_count_reset(void)`

Reset all internal statistics counters.

---

### `void lace_count_report_file(FILE* file)`

Write a statistics report to the given `FILE*`.

---

### `void lace_count_report(void)`

Write a statistics report to `stdout`.

---

## Miscellaneous

### `void lace_sleep_us(int64_t microseconds)`

Sleep for the given number of microseconds. On Linux and macOS this uses
`nanosleep` and is precise to the microsecond. On Windows, resolution is
limited to whole milliseconds.
