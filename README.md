# Lace

[![Linux](https://github.com/trolando/lace/actions/workflows/linux.yml/badge.svg)](https://github.com/trolando/lace/actions/workflows/linux.yml)
[![macOS](https://github.com/trolando/lace/actions/workflows/macos.yml/badge.svg)](https://github.com/trolando/lace/actions/workflows/macos.yml)
[![Windows](https://github.com/trolando/lace/actions/workflows/windows.yml/badge.svg)](https://github.com/trolando/lace/actions/workflows/windows.yml)
[![License: Apache](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

Lace is a C framework for fine-grained fork-join parallelism on multi-core computers.

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

For more examples, see [DOCS.md](./DOCS.md) and the contents of the [benchmarks](./benchmarks/) folder.

## Table of Contents

- [Features](#features)
- [Installation](#installation)
- [Building](#building)
- [Configuration Options](#configuration-options)
- [Usage](#usage)
- [Benchmarking](#benchmarking)
- [Academic publications](#academic-publications)
- [License](#license)

## Features

- ⚡ Low-overhead, lock-free work-stealing
- 💤 Suspend and resume workers to save CPU time
- 📌 Optional thread pinning with `hwloc`
- 📊 Low-overhead statistics per worker
- ⏭️ Interrupt support (e.g. for garbage collection, or initialisation)

Lace uses a **scalable** double-ended queue for its implementation of work-stealing, which is **wait-free** for the thread spawning tasks and **lock-free** for the threads stealing tasks. The design of the datastructure minimizes interaction between CPUs.
Lace can report the number of tasks, steals and queue splits per Lace worker. It can also report the amount of time spent in startup/shutdown, performing stolen work, overhead of stealing and of searching for work, per worker. Gathering these statistics is done with virtually no overhead.

Please [let us know](https://github.com/trolando/lace/issues) if you need features that are currently not implemented in Lace.

## Installation

Lace requires a C11-compatible compiler (tested with GCC and Clang) and optionally `hwloc` (`libhwloc-dev`).

Lace works on:
- 🐧 Linux
- 🪟 Windows (via MSYS2)
- 🍎 macOS

You can install Lace via `make install`, or integrate it into your project via CMake:

<details>
  <summary>Example for CMake with FetchContent</summary>

```cmake
if(NOT TARGET lace)
  find_package(lace QUIET)
  if(NOT lace_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        lace
        GIT_REPOSITORY https://github.com/trolando/lace.git
        GIT_TAG        v2.0.0
    )
    FetchContent_MakeAvailable(lace)
  endif()
endif()
```

This example first tests if Lace is already a target in the project, for example
when included as a submodule.  Otherwise it tries to find an installed version,
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

Setting | Description
--------|------------
`LACE_BUILD_TESTS` | Build the testing programs (not when subproject)
`LACE_BUILD_BENCHMARKS` | Build the included set of benchmark programs (not when subproject)
`LACE_USE_MMAP` | Use `mmap` to allocate memory instead of `aligned_alloc`
`LACE_USE_HWLOC` | Use the `hwloc` library to pin threads to CPUs
`LACE_COUNT_TASKS` | Let Lace record the number of executed tasks
`LACE_COUNT_STEALS` | Let Lace count how often tasks were stolen
`LACE_COUNT_SPLITS` | Let Lace count how often the queue split point was moved
`LACE_PIE_TIMES` | Let Lace record precise overhead times

**Recommendations**:

- Use `LACE_USE_MMAP` to reduce physical memory usage. Memory is allocated 
  lazily by the OS.
- Use `LACE_USE_HWLOC` to ensure threads are pinned to CPU cores appropriately.

## Usage

See the [documentation](DOCS.md) for more details.

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
`bench.py` script for running them.

## Academic publications

The following two academic publications are directly related to Lace.

T. van Dijk (2016) [Sylvan: Multi-core Decision Diagrams](http://dx.doi.org/10.3990/1.9789036541602). PhD Thesis.

T. van Dijk and J.C. van de Pol (2014) [Lace: Non-blocking Split Deque for Work-Stealing](http://dx.doi.org/10.1007/978-3-319-14313-2_18). In: Euro-Par 2014: Parallel Processing Workshops. LNCS 8806, Springer.

## License

Lace is licensed with the [Apache 2.0 license](https://opensource.org/licenses/Apache-2.0). 

