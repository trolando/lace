# Lace

[![Linux](https://github.com/trolando/lace/actions/workflows/linux.yml/badge.svg)](https://github.com/trolando/lace/actions/workflows/linux.yml)
[![macOS](https://github.com/trolando/lace/actions/workflows/macos.yml/badge.svg)](https://github.com/trolando/lace/actions/workflows/macos.yml)
[![Windows](https://github.com/trolando/lace/actions/workflows/windows.yml/badge.svg)](https://github.com/trolando/lace/actions/workflows/windows.yml)
[![FreeBSD](https://github.com/trolando/lace/actions/workflows/freebsd.yml/badge.svg)](https://github.com/trolando/lace/actions/workflows/freebsd.yml)
[![License: Apache](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

Lace is a C framework for fine-grained fork-join parallelism on multi-core computers.

```c
TASK(int, fibonacci, int, n)          // declare a Lace task (place in header or source)

int fibonacci_CALL(lace_worker* lw, int n) {
    if (n < 2) return n;
    fibonacci_SPAWN(lw, n-1);         // fork: push task onto deque
    int a = fibonacci_CALL(lw, n-2);  // run another instance directly
    int b = fibonacci_SYNC(lw);       // join: retrieve result of spawned task
    return a + b;
}

int main(int argc, char** argv)
{
    lace_start(0, 0, 0);              // 0 = auto-detect cores, default deque, default stack
    int result = fibonacci(42);
    printf("fibonacci(42) = %d\n", result);
    lace_stop();
}
```

**[Full documentation](https://trolando.github.io/lace/)** — user guide, API reference, and benchmarks.

## Features

- ⚡ Low-overhead, lock-free work-stealing
- 💤 Sleeps when idle (exponential backoff) to save CPU time
- 📌 Optional thread pinning with `hwloc`
- 📈 Low-overhead statistics per worker
- ⛔ Interrupt support for coordinating all workers to a safe point (e.g. stop-the-world GC)

Lace uses a **scalable** double-ended queue for work-stealing. The owner thread pushes and pops tasks from its own deque in a **wait-free** manner. Thief threads steal from the other end in a **lock-free** manner. The design minimizes cache line contention between workers.

## Platform support

- 🐧 Linux (GCC, Clang)
- 🪟 Windows (MSVC, MSYS2/MinGW)
- 🍎 macOS (Apple Clang)
- 😈 FreeBSD (Clang)


## Quick start

```bash
cmake -B build
cmake --build build
```

Integrate into your project with CMake:

```cmake
add_subdirectory(external/lace)
target_link_libraries(my_app PRIVATE lace::lace)
```

Or fetch automatically:

```cmake
if(NOT TARGET lace::lace)
  find_package(lace 2.3.1 CONFIG QUIET)
  if(NOT lace_FOUND)
    include(FetchContent)
    FetchContent_Declare(lace
        GIT_REPOSITORY https://github.com/trolando/lace.git
        GIT_TAG        v2.3.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(lace)
  endif()
endif()
```

See the [documentation](https://trolando.github.io/lace/) for CMake options, configuration recommendations, task variants, and the full API reference.

## Academic publications

If you use Lace in your research, please cite:

T. van Dijk and J.C. van de Pol (2014) [Lace: Non-blocking Split Deque for Work-Stealing](http://dx.doi.org/10.1007/978-3-319-14313-2_18). In: Euro-Par 2014: Parallel Processing Workshops. LNCS 8806, Springer.

T. van Dijk (2016) [Sylvan: Multi-core Decision Diagrams](http://dx.doi.org/10.3990/1.9789036541602). PhD Thesis, University of Twente.

## License

Lace is licensed under the [Apache 2.0 license](https://opensource.org/licenses/Apache-2.0).

