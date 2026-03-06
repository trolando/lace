# Lace

[![Linux](https://github.com/trolando/lace/actions/workflows/linux.yml/badge.svg)](https://github.com/trolando/lace/actions/workflows/linux.yml)
[![macOS](https://github.com/trolando/lace/actions/workflows/macos.yml/badge.svg)](https://github.com/trolando/lace/actions/workflows/macos.yml)
[![Windows](https://github.com/trolando/lace/actions/workflows/windows.yml/badge.svg)](https://github.com/trolando/lace/actions/workflows/windows.yml)
[![License: Apache](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

Lace is a C framework for fine-grained fork-join parallelism on multi-core computers.

```c
TASK_1(int, fibonacci, int, n)  // declare a Lace task (place in header or source)

int fibonacci_CALL(lace_worker* lw, int n) {
    if (n < 2) return n;
    fibonacci_SPAWN(lw, n-1);         // fork: push task onto deque
    int a = fibonacci_CALL(lw, n-2);  // run another instance directly
    int b = fibonacci_SYNC(lw);       // join: retrieve result of spawned task
    return a + b;
}

int main(int argc, char** argv)
{
    int n_workers = 0;  // 0 workers = use all available cores
    int dqsize = 0;     // use default task deque size
    int stacksize = 0;  // use default program stack size

    lace_start(n_workers, dqsize, stacksize);
    int result = fibonacci(42);
    printf("fibonacci(42) = %d\n", result);
    lace_stop();
}
```

For more examples and the full API reference, see [DOCS.md](./DOCS.md) and the [benchmarks](./benchmarks/) folder.

## Table of Contents

- [Features](#features)
- [Installation](#installation)
- [Building](#building)
- [Configuration Options](#configuration-options)
- [Usage](#usage)
- [Migrating from Lace v1](#migrating-from-lace-v1)
- [Benchmarking](#benchmarking)
- [Academic publications](#academic-publications)
- [License](#license)

## Features

- ⚡ Low-overhead, lock-free work-stealing
- 💤 Sleeps when idle (exponential backoff) to save CPU time
- 📌 Optional thread pinning with `hwloc`
- 📈 Low-overhead statistics per worker
- ⛔ Interrupt support for coordinating all workers to a safe point (e.g. stop-the-world GC)

Lace uses a **scalable** double-ended queue for work-stealing. The owner thread pushes and pops tasks from its own deque in a **wait-free** manner. Thief threads steal from the other end in a **lock-free** manner. The design minimizes cache line contention between workers.

Lace can report the number of tasks, steals and queue splits per worker. It can also report time spent in startup/shutdown, stolen work, steal overhead and idle search time per worker. Gathering these statistics is done with virtually no overhead.

Please [let us know](https://github.com/trolando/lace/issues) if you need features that are currently not implemented in Lace.

## Installation

Lace requires a C11-compatible compiler (tested with GCC and Clang) and optionally `hwloc` (`libhwloc-dev`).

Lace works on:
- 🐧 Linux
- 🪟 Windows (with MSYS2)
- 🪟 Windows (with MSVC)
- 🍎 macOS

You can install Lace via `make install`, or integrate it into your project via CMake:

<details>
  <summary>Example for CMake with FetchContent</summary>

```cmake
if(NOT TARGET lace::lace)
  find_package(lace 2.2 CONFIG QUIET)
  if(NOT lace_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        lace
        GIT_REPOSITORY https://github.com/trolando/lace.git
        GIT_TAG        v2.2.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(lace)
  endif()
endif()
```

This example first tests if Lace is already a target in the project, for example
when included as a submodule. Otherwise it tries to find an installed version,
or fetch it from GitHub.
</details>

## Building

Create a separate build directory:
```bash
cmake -B build
cmake --build build
```

## Configuration Options

Lace can be configured with the following CMake options:

Setting | Description | Default
--------|-------------|--------
`LACE_BUILD_TESTS` | Build the testing programs (disabled when used as a subproject) | OFF
`LACE_BUILD_BENCHMARKS` | Build the included benchmark programs (disabled when used as a subproject) | OFF
`LACE_USE_MMAP` | Use `mmap` to allocate task deques instead of `aligned_alloc`. Physical pages are lazily allocated by the OS, which reduces startup memory usage. | ON
`LACE_USE_HWLOC` | Use the `hwloc` library to pin worker threads to CPU cores. Important for NUMA systems where memory locality affects performance. | OFF
`LACE_COUNT_TASKS` | Record the number of tasks executed per worker | OFF
`LACE_COUNT_STEALS` | Record the number of successful steals per worker | OFF
`LACE_COUNT_SPLITS` | Record the number of deque split-point adjustments per worker | OFF
`LACE_PIE_TIMES` | Record precise overhead times per worker (startup, steal overhead, idle search) | OFF
`LACE_BACKOFF` | Workers sleep with exponential backoff when no work is available, reducing CPU usage without affecting throughput | ON
`LACE_ENABLE_PIC` | Compile Lace with position-independent code (`-fPIC`). Required when embedding Lace inside a shared library. | OFF
`LACE_NATIVE_OPT` | Optimize for the host CPU architecture (`-march=native`). Improves performance on the build machine but produces binaries that may not run on other CPUs. | OFF
`LACE_SANITIZE_ADDRESS` | Build with AddressSanitizer to detect memory errors. For development and testing only. | OFF
`LACE_SANITIZE_THREAD` | Build with ThreadSanitizer to detect data races. For development and testing only. | OFF
`LACE_SANITIZE_UB` | Build with UndefinedBehaviorSanitizer to detect undefined behavior. For development and testing only. | OFF

**Recommendations**:

- Enable `LACE_USE_MMAP` to let the OS lazily allocate physical memory for task deques.
- Enable `LACE_USE_HWLOC` to pin threads to cores, especially on NUMA systems.
- Leave `LACE_BACKOFF` on. Benchmarks show it does not affect throughput.
- Use `LACE_NATIVE_OPT` for local benchmarking, but not for distributed or portable builds.
- The sanitizer options are mutually exclusive. Use them individually during development.

## Usage

Lace comes in three variants that differ in the size of the task struct allocated on the deque:

Variant | Task size | Available for parameters and result
--------|-----------|------------------------------------
`lace32` | 32 bytes | 16 bytes
`lace` | 64 bytes | 48 bytes
`lace128` | 128 bytes | 112 bytes

The 16-byte overhead is fixed (function pointer and thief status). Choose the variant based on how much data your tasks need to store — parameters and return value must fit in the available space. The default `lace` variant (48 bytes usable) is sufficient for most use cases. A `static_assert` in the generated code will catch it at compile time if your task's parameters and return type exceed the available space.

Lace tasks are declared with the `TASK_N` family of macros, where `N` is the number of parameters. The macro generates the task descriptor and function signatures; you provide the body as a regular C function named `TASKNAME_CALL`.

```c
// No parameters
TASK_0(int, compute)
int compute_CALL(lace_worker* lw) { ... }

// One parameter
TASK_1(int, fibonacci, int, n)
int fibonacci_CALL(lace_worker* lw, int n) { ... }

// Two parameters
VOID_TASK_2(process, int*, data, int, size)
void process_CALL(lace_worker* lw, int* data, int size) { ... }
```

Inside a `_CALL` function, use `SPAWN`, `CALL`, and `SYNC` to fork and join work:

```c
int fibonacci_CALL(lace_worker* lw, int n) {
    if (n < 2) return n;
    fibonacci_SPAWN(lw, n-1);         // push onto deque (may be stolen)
    int a = fibonacci_CALL(lw, n-2);  // execute directly
    int b = fibonacci_SYNC(lw);       // retrieve spawned result
    return a + b;
}
```

To start Lace and run a top-level task from outside a worker thread, call the task by name as a regular function:

```c
lace_start(0, 0, 0);   // (n_workers, dqsize, stacksize), 0 = use defaults
int result = fibonacci(42);
lace_stop();
```

If `fibonacci()` is called from inside a Lace worker thread (e.g. from within another task), it automatically detects this and calls `fibonacci_CALL` directly instead of submitting a task to the framework. This means you can safely call `NAME()` from both Lace and non-Lace contexts without branching in your own code.

See [DOCS.md](./DOCS.md) for the full API, including interrupts, worker queries, and advanced usage.

## Migrating from Lace v1

Lace v2 changes how task bodies are written. In v1, the task body was placed directly inside the `TASK_N` macro and `SPAWN`/`CALL`/`SYNC` were macros that hid the worker pointer. In v2, the body is a regular C function (`TASKNAME_CALL`) and the worker pointer is explicit.

**v1:**
```c
TASK_1(int, fibonacci, int, n)
{
    if (n < 2) return n;
    int m, k;
    SPAWN(fibonacci, n-1);
    k = CALL(fibonacci, n-2);
    m = SYNC(fibonacci);
    return m + k;
}
```

**v2:**
```c
TASK_1(int, fibonacci, int, n)

int fibonacci_CALL(lace_worker* lw, int n)
{
    if (n < 2) return n;
    fibonacci_SPAWN(lw, n-1);
    int k = fibonacci_CALL(lw, n-2);
    int m = fibonacci_SYNC(lw);
    return m + k;
}
```

The v2 task body is a plain C function, which means it is visible to debuggers, can be stepped through in GDB, and can call non-Lace helper functions while still propagating the worker pointer. The old `RUN(task, args...)` macro is replaced by simply calling the task by name as a regular function: `fibonacci(42)`.

## Benchmarking

Lace includes a set of benchmark programs to evaluate its performance. Many of
these benchmarks are adapted from well-known frameworks such as **Cilk**,
**Wool**, and **Nowa**.

To enable the benchmarks, build Lace with:

```bash
cmake -B build -DLACE_BUILD_BENCHMARKS=ON
cmake --build build
```

The compiled benchmarks will be placed in `build/benchmarks/`, along with the
`bench.py` script for running them. Pass `-w 0` to use all available cores.

## Academic publications

The following two academic publications are directly related to Lace.

T. van Dijk (2016) [Sylvan: Multi-core Decision Diagrams](http://dx.doi.org/10.3990/1.9789036541602). PhD Thesis.

T. van Dijk and J.C. van de Pol (2014) [Lace: Non-blocking Split Deque for Work-Stealing](http://dx.doi.org/10.1007/978-3-319-14313-2_18). In: Euro-Par 2014: Parallel Processing Workshops. LNCS 8806, Springer.

## License

Lace is licensed with the [Apache 2.0 license](https://opensource.org/licenses/Apache-2.0).
