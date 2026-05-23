# Lace

[![Linux](https://github.com/trolando/lace/actions/workflows/linux.yml/badge.svg)](https://github.com/trolando/lace/actions/workflows/linux.yml)
[![macOS](https://github.com/trolando/lace/actions/workflows/macos.yml/badge.svg)](https://github.com/trolando/lace/actions/workflows/macos.yml)
[![Windows](https://github.com/trolando/lace/actions/workflows/windows.yml/badge.svg)](https://github.com/trolando/lace/actions/workflows/windows.yml)
[![License: Apache](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

Lace is a C framework for fine-grained fork-join parallelism on multi-core computers.

```c
TASK(int, fibonacci, int, n) {
    if(n < 2) return n;
    SPAWN(fibonacci, n-1);
    int a = CALL(fibonacci, n-2);
    int b = SYNC(fibonacci);
    return a+b;
}

int main(int argc, char **argv)
{
    int n_workers = 4;
    lace_start(n_workers, 0);
    int result = RUN(fibonacci, 42);
    printf("fibonacci(42) = %d\n", result);
    lace_stop();
}
```

## Features

Feature | Description
---------|------------
Low overhead | Lace uses a **scalable** double-ended queue for its implementation of work-stealing, which is **wait-free** for the thread spawning tasks and **lock-free** for the threads stealing tasks. The design of the datastructure minimizes interaction between CPUs.
Backoff | When idle, Lace workers sleep with exponential backoff (up to 1 ms), reducing CPU usage to near zero when there is no work. This is controlled by the `LACE_BACKOFF` option (enabled by default).
Multi-threaded task submission | Non-Lace threads can submit tasks concurrently via `RUN`. Up to 64 external threads can submit tasks simultaneously without contention.
Interrupting | Lace threads can be (cooperatively) interrupted to execute another task first. This is for example used by [Sylvan](https://github.com/trolando/sylvan) to perform garbage collection.
Portable | Lace runs on Linux (GCC, Clang), macOS (Apple Clang), and Windows (MSVC, MSYS2/MinGW), with correct memory ordering on both x86 and ARM/AArch64.

Please [let us know](https://github.com/trolando/lace/issues) if you need features that are currently not implemented in Lace.

## Installation

Lace requires a modern compiler supporting C11. It is tested with GCC, Clang, and MSVC.
Lace can use hwloc (`libhwloc-dev`) to pin workers and allocate memory on the correct CPUs/memory domains on NUMA systems.

It is possible to install Lace with `cmake --install build` if that is desired.
We recommend using Lace as a submodule in your repository or as a dependency in your CMake script,
for example using the `FetchContent` or `ExternalProject` features of CMake.

<details>
  <summary>Example for FetchContent</summary>

```cmake
if(NOT TARGET lace)
  find_package(lace QUIET)
  if(NOT lace_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        lace
        GIT_REPOSITORY https://github.com/trolando/lace.git
        GIT_TAG        v1.7.0
    )
    FetchContent_MakeAvailable(lace)
  endif()
endif()
```

This example first tests if Lace is already a target in the project, for example when included as a submodule.
If this is not the case, it will try to find a locally installed version of Lace and use that.
Otherwise, it will use `FetchContent` to download Lace from GitHub.
</details>

## Building Lace

It is recommended to build Lace in a separate build directory:
```bash
cmake -B build
cmake --build build
```

Lace can be configured with the following CMake settings:

Setting | Description | Default
--------|-------------|--------
`LACE_USE_HWLOC` | Use the `hwloc` library to pin threads to CPUs and allocate memory on the correct NUMA node | OFF
`LACE_BACKOFF` | Workers sleep with exponential backoff when no work is available, reducing CPU usage without affecting throughput | ON
`LACE_NATIVE_OPT` | Optimise for the host CPU architecture (`-march=native`) | ON
`LACE_ENABLE_PIC` | Compile with position-independent code (`-fPIC`) | OFF
`BUILD_SHARED_LIBS` | Build shared libraries instead of static | OFF
`LACE_COUNT_TASKS` | Let Lace record the number of executed tasks | OFF
`LACE_COUNT_STEALS` | Let Lace count how often tasks were stolen | OFF
`LACE_COUNT_SPLITS` | Let Lace count how often the queue split point was moved | OFF
`LACE_PIE_TIMES` | Let Lace record precise overhead times | OFF

The following options are only available when Lace is the top-level project:

Setting | Description | Default
--------|-------------|--------
`LACE_BUILD_TESTS` | Build the test suite | ON
`LACE_BUILD_BENCHMARKS` | Build the benchmark programs | ON
`LACE_SANITIZE_ADDRESS` | Build with AddressSanitizer | OFF
`LACE_SANITIZE_THREAD` | Build with ThreadSanitizer | OFF
`LACE_SANITIZE_UB` | Build with UndefinedBehaviorSanitizer | OFF

Worker deques are allocated using virtual memory (`mmap` on Unix, `VirtualAlloc` on Windows).
The default deque size is 1 048 576 task slots, but only pages actually touched consume physical memory.

## Using Lace

There are two versions of Lace:
- The standard version `lace` consisting of `lace.h` and `lace.c` uses 64 bytes per task and supports at most 10 parameters per task.
- The extended version `lace14` consisting of `lace14.h` and `lace14.c` uses 128 bytes per task and supports at most 14 parameters per task.

### Starting and stopping Lace

Start the Lace framework using `lace_start(unsigned int n_workers, size_t dqsize)`.
This creates `n_workers` new threads that will immediately start work-stealing.
Each thread allocates its own task deque for `dqsize` tasks.
* When `n_workers` is set to 0, Lace automatically detects the available cores.
* When `dqsize` is set to 0, the default of 1 048 576 is used.

Use `lace_stop()` to stop the framework, terminating all workers.
Use `lace_is_running()` to check whether Lace is currently active.

When idle, workers automatically sleep with exponential backoff (if `LACE_BACKOFF` is enabled),
so there is no need to manually manage worker activity.

### Defining tasks

Tasks are defined using the `TASK` macro:

```c
TASK(int, fib, int, n) { ... }          // task with int return and one int parameter
TASK(void, do_work, int, n) { ... }     // void task with one parameter
TASK(void, init) { ... }                // void task with no parameters
```

The explicit `TASK_n` and `VOID_TASK_n` macros also remain available.
Declaration and implementation can be separated using `TASK_DECL_n` and `TASK_IMPL_n`.

From Lace tasks (running in a Lace thread):
- Use `SPAWN` to create a task and `SYNC` to obtain the result (if stolen) or execute the task (if not stolen)
- Use `CALL` to directly execute a task without putting it in the queue
- Use `DROP` instead of `SYNC` to not execute a task (unless already stolen)

From external threads (not running in a Lace thread):
- Use `RUN` to submit a task to the Lace framework. This method blocks until the task is completed.
- Multiple external threads can call `RUN` concurrently (up to 64 concurrent submissions).

See the `benchmarks` directory for examples.

### Interrupting

Lace offers two methods to interrupt currently running tasks and run something else:
- the `NEWFRAME` macro, e.g. `NEWFRAME(fib, 40)` halts current tasks and offers the `fib` task to the framework.
- the `TOGETHER` macro halts current tasks and lets **all Lace workers** execute a copy of the given task.

The `TOGETHER` macro is useful to initialize thread-local variables on each worker.

Interrupting is cooperative. Lace checks for interrupting tasks when stealing work, i.e., during `SYNC` or when idle.
Large tasks can use the `YIELD_NEWFRAME()` macro to manually check for interrupting tasks.

Lace offers the `lace_barrier` method to let all Lace workers synchronize.
Typically used in Lace tasks created using the `TOGETHER` macro.

## Benchmarking Lace

Lace comes with a number of example programs, which can be used to test the performance of Lace.
Many of these benchmark programs have been obtained from benchmark collections of other frameworks such as Cilk, Wool, and Nowa.
After building Lace with `LACE_BUILD_BENCHMARKS` set to `ON`, the `build/benchmarks` directory contains the benchmarks programs, as well as the `bench.py` Python script that runs the benchmarks.

Workloads such as `matmul` and `queens` are easy to load balance.
The `fib` workload has a very high number of nearly empty tasks and is therefore a stress test on the overhead of the framework, but is not very representative for real world workloads.
The `uts t3l` is a more challenging workload as it offers a unpredictable tree search.
See for further details the academic publications on Lace mentioned below.

## Academic publications

The following two academic publications are directly related to Lace.

T. van Dijk (2016) [Sylvan: Multi-core Decision Diagrams](http://dx.doi.org/10.3990/1.9789036541602). PhD Thesis.

T. van Dijk and J.C. van de Pol (2014) [Lace: Non-blocking Split Deque for Work-Stealing](http://dx.doi.org/10.1007/978-3-319-14313-2_18). In: Euro-Par 2014: Parallel Processing Workshops. LNCS 8806, Springer.

## License

Lace is licensed with the [Apache 2.0 license](https://opensource.org/licenses/Apache-2.0). 
