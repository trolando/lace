# Lace API

`lace.h` provides the public API for initializing, controlling, and interacting
with the Lace framework for fine-grained fork-join parallelism.

There are two versions of Lace. The standard version uses 64 bytes per task and
allows up to 6 parameters. The "14" version uses 128 bytes per task and allows
up to 14 parameters.

- To use the default version, include `lace.h` and add `lace.c` to your build
  script.
- To use the "14" version, include `lace14.h` and add `lace14.c` to your build
  script.

---

## Defining Tasks

To define tasks, Lace offers the following macros:
- `TASK_1(return_type, name, type_1, name_1)`
- `TASK_2(return_type, name, type_1, name_1, type_2, name_2)`
- `TASK_3(return_type, name, type_1, name_1, type_2, name_2, type_3, name_3)`
- etc.

This defines a task with the given name, return type, parameter types and
parameter names. The macro defines a number of functions:
- `Task* <name>_SPAWN(LaceWorker* lw, <type_1> <name_1>, ...)` to spawn a new
  task which can then be stolen by other workers.
- `<return_type> <name>_RUN(<type_1> <name_1>, ...)` to create a task and run it
  on a Lace worker.
- `<return_type> <name>_SYNC(LaceWorker* lw)` to get the result of the last
  spawned task (LIFO order). This either waits for the result of the stolen task
or if not stolen, executes the task.
- `<return_type> <name>_NEWFRAME(...)` is a version of `<name>_RUN` that halts
  all workers (between tasks) and runs the `<name>` task on the Lace workers.
One use case is stop-the-world garbage collection.
- `void <name>_TOGETHER(...)` is a version of `<name>_RUN` that lets all workers
  run the same task concurrently. One use case is initialization of thread local
storage of the program on every Lace worker.

Use the `SPAWN` and `SYNC` methods to create tasks that can be stolen and to
obtain the result of the last spawned tasks. The number of `SPAWN` and `SYNC`
calls should match.

For defining tasks with no return type (`void`), use these macros:
- `VOID_TASK_1(name, type_1, name_1)`
- `VOID_TASK_2(name, type_1, name_1, type_2, name_2)`
- `VOID_TASK_3(name, type_1, name_1, type_2, name_2, type_3, name_3)`
- etc.

**Important**: the actual task must be a function with a signature like:
- `<return_type> <name>(LaceWorker*, <type_1>, <type_2>, ...)`
- `void <name>(LaceWorker*, <type_1>, <type_2>, ...)`

The tasks are given the `LaceWorker*` pointer that corresponds to the worker
that the task is currently running in. This pointer and its contents must not be
changed and the pointer is necessary for a number of function calls such as
`SPAWN`, `SYNC`, etc.

### Example

Typically a recursive function with two 'child tasks' is parallelized as follows:

```c
TASK_1(int, fibonacci, int, n)   // macro to create Lace functions (can be in header file)

int fibonacci(LaceWorker* lw, int n) {
    if(n < 2) return n;
    fibonacci_SPAWN(lw, n-1);    // SPAWN a task (fork)
    int a = fibonacci(lw, n-2);  // run another task in parallel
    int b = fibonacci_SYNC(lw);  // SYNC the spawned task (join)
    return a + b;
}

int main(int argc, char** argv)
{
    int n_workers = 4;           // create 4 workers
                                 // use 0 to automatically use all available cores
    int dqsize = 0;              // use default task deque size
    int stacksize = 0;           // use default program stack size

    lace_start(n_workers, dqsize, stacksize);
    int result = fibonacci_RUN(42);
    printf("fibonacci(42) = %d\n", result);
    lace_stop();
}
```

See further the [benchmarks](benchmarks/) directory for more examples.

### Interrupting

Lace offers two methods to interrupt currently running tasks and run something else:
- the `fib_NEWFRAME(40)` function halts current tasks and offers the `fib(40)`
  task to the framework.
- the `fib_TOGETHER(40)` function halts current tasks and lets **all Lace
  workers** execute a copy of the given `fib(40)` task.

The `NEWFRAME` functions are used for example to implement stop-the-world
garbage collection, interrupting current tasks in order to run a more important
task first.
The `TOGETHER` functions are typically used to initialize thread-local variables
on each worker, or to perform some other operation on each individual worker
thread.

Interrupting is cooperative. Lace checks for interrupting tasks when stealing
work, i.e., during `SYNC` or when idle.  Long-running tasks should use
`lace_check_yield` to manually check if an interruption is occuring.

---

## Lifecycle

These methods control the Lace workers, starting and stopping the workers, and
suspending and resuming them. These methods must be called from outside the Lace
framework, i.e., not from a Lace worker thread.

A typical Lace program starts with `lace_start` and ends with `lace_stop`, and
uses `<task>_RUN(...)` to let the Lace worker threads execute tasks.

When Lace is started, worker threads are created and immediately start
busy-waiting for work.  Each thread allocates its own task queue of the
requested size.

If `LACE_USE_MMAP` is set, then the queue is preallocated in virtual memory, and
real memory is allocated by the OS on demand.  If `LACE_USE_HWLOC` is set, the
worker threads will pin to a CPU core.

Lace workers busy-wait for tasks to steal, increasing the CPU load to 100%.  Use
`lace_suspend` and `lace_resume` (in a non-Lace thread) to temporarily stop the
work-stealing framework.  Suspending and resuming typically requires less than 1
ms.

---

### `void lace_start(unsigned int n_workers, size_t dqsize, size_t stacksize);`

Start Lace workers, allowing tasks to be run.

- `n_workers`: number of worker threads (0 = auto-detect)
- `dqsize`: size of each task deque (0 = default size, i.e., 100K tasks)
- `stacksize`: worker thread stack size (0 = min of 16MB and current thread stack)

---

### `void lace_suspend(void);`  

Suspend all workers.

---

### `void lace_resume(void);`

Resume all workers.

---

### `void lace_stop(void);`

Stop the Lace runtime.

---

### `void lace_set_verbosity(int level);`

Set verbosity level for Lace runtime. This should be set *before* calling `lace_start`.

- `level = 0`: no startup messages (default)
- `level = 1`: show startup messages

---

## Worker Context

### `unsigned int lace_worker_count(void);`

Returns the number of Lace worker threads.

---

### `int lace_is_worker(void);`

Returns 1 if called from a Lace worker thread, 0 otherwise.

---

### `LaceWorker* lace_get_worker(void);`  

Returns a pointer to the current worker’s data (or `NULL` if not a worker).

---

### `int lace_worker_id(void);`

Returns the worker ID (or `-1` if not in a Lace thread).

---

### `uint64_t lace_rng(LaceWorker* worker);`

Thread-local pseudo-random number generator. Each Lace worker has its own thread-local RNG state. The purpose is to avoid contention on a shared RNG.

---

## Task Operations

### `void lace_barrier(void);`

Barrier synchronization for Lace tasks.
All workers block until all have reached the barrier.
**Must be called from inside a Lace task.**

---

### `void lace_drop(LaceWorker* worker);`

Instead of `<name>_SYNC`, if `lace_drop` is used this drops the last spawned task. Effectively this means that if the task was already stolen, it is still executed, but if it was not yet stolen, then it is not executed.

---

### `int lace_is_stolen_task(Task* t);`  

Returns 1 if the given task is stolen.

---

### `int lace_is_completed_task(Task* t);`

Returns 1 if the given task is completed.

---

### `<return_type>* lace_task_result(Task* t);`

Returns a pointer to where the result of a task will be stored after its completion. This is a pointer in the Task* struct of the given task.

---

### `void lace_steal_random(LaceWorker* worker);`

Try to steal and execute a task from a random worker.

---

### `void lace_check_yield(LaceWorker* worker);`

Checks if the current task should yield to a `TOGETHER` or `NEWFRAME` interruption, and yields if necessary. Useful in long-running tasks.

---

### `void lace_make_all_shared(void);`

Mark all tasks in the current worker’s deque as shared.

---

### `Task* lace_get_head(void);`

Returns the current head of the worker's deque.

---

## Statistics

### `void lace_count_reset(void);`

Reset all internal counters.

---

### `void lace_count_report_file(FILE* file);`

Print current Lace statistics to the given `FILE*`.

---

### `void lace_count_report(void);`

Shortcut to print statistics to `stdout`.
