# Changelog

All notable changes to Lace will be documented in this file.

## [2.2.2] - 2026-03-24

### Changed

- Documentation now via Doxygen and Sphinx.

### Fixed

- Fixed compilation on systems without `CLOCK_MONOTONIC_RAW`.
- Fixed several more compilation warnings and 32-bit systems.


## [2.2.1] - 2026-03-06

### Added

- Every task now comes with a `_DROP` function that discards the result of a stolen task
  or skips executing the task if it is not yet stolen (instead of `_SYNC`).

### Removed

- Removed the undocumented run-exclusive feature.

### Changed

- Minor changes to the core algorithm to bring it in line with earlier descriptions.
- Improvements to atomic memory ordering in the core algorithm.


## [2.2.0] - 2026-03-05

### Added

- Sanitize options for when Lace is the top project.

### Removed

- Using `lace_suspend` and `lace_resume` has worse outcomes than letting the
  threads sleep when idle (via `LACE_BACKOFF`). An attempt to redesign this
  functionality also exposed a race condition in the main worker loop, so it was
  easier to just remove the entire functionality as it's practically obsolete.

### Changed

- When building as a top project, will now default to `Release` build type.

### Fixed

- Fixed a lot of warnings from the compiler and from sanitizers.


## [2.1.0] - 2026-02-22

### Added

- Full support for Windows with the MSVC compiler, i.e., without MSYS2 or POSIX.
- Option `LACE_BACKOFF` (default `ON`) to let idle threads sleep if there is no work.
- Method `lace_is_running` returns `1` if Lace is running, or `0` if not.

### Changed

- Rewrote the `lace_rng` so it can be used on 32-bit systems.


## [2.0.3] - 2025-08-05

In addition to the API-breaking change in 2.0.2, since we're still barely into
2.0 anyway, I'm going to change the naming for lace14. Now, we have `lace.h`
which has 64-byte task structs, then `lace32.h` has 32-byte task structs and
`lace128.h` has 128-byte task structs. On a 64-bit system with 8-byte pointers,
it is likely that `lace32` is too small, but on 32-bit systems with 4-byte
pointers and maybe a 32-byte cache line, `lace32` might perform a bit better.

### Added

- By default, Lace will now build with `-march=native` and with no PIC. This
  can be controlled with CMake settings.
- A test program for `lace_barrier` called `test_barrier`.

### Changed

- Improved handling of different sizes of pointers and cache lines on different
  architectures, such as 32-bit vs 64-bit, and cache line sizes such as 32
  bytes, 64 bytes and 128 bytes.
- The `lace.h` header now allows tasks with up to 10 parameters, as long as
  they still fit in the 64-byte tasks. This is checked during compilation.

### Fixed

- The benchmarks now use an `int` for the return value of `getopt` rather than
  a `char`. On some systems, a `char` is `unsigned` by default, so we need to
  have `int` to compare to `-1`.
- Addressed potential problems with `lace_barrier` and weak memory systems.


## [2.0.2] - 2025-08-01

Technically this is an API-breaking change, but since we have practically few
users and this is rather quick after the release of 2.0.0 anyway, I'll mark it
as a patch update.

I realized when working on Sylvan that it is much better to use something like
`pfib_CALL` for calling a task from inside Lace (which is typically developer-
facing) and have `pfib(..)` instead of `pfib_RUN(..)` to run a task from a
non-Lace thread. This way, an API can look cleaner and a library user does not
need to concern themselves with adding `_RUN` to every call.

Also removed some of the CamelCase style structs in favor of more traditional
C-style: `lace_worker`, `lace_task`, etc.


## [2.0.1] - 2025-07-31

### Added

- the `_SPAWN` functions now return a pointer to the created task.


## [2.0.0] - 2025-07-28

This is a major rewrite of the Lace API. The goal is to make the framework more
intuitive to work with by reducing the amount of macro-style code. For example, 
instead of `SPAWN(fib, 40)` we write `fib_SPAWN(lace, 40)`. In most cases, the 
performance is not changed by this, except the `knapsack` benchmark, as in the 
new style the head of the deque is now stored in the worker struct in the heap, 
rather than as a register or on the program stack.
See also [#10](https://github.com/trolando/lace/pull/10).

### Added

- Documentation is much improved now.
- Added a more 'proper' Changelog (this file).
- Added a large number of benchmarks from the Nowa repository.

### Changed

- Significant API-breaking rewrite to be a bit more user-friendly.
- Changed algorithm for `lace_rng` from lcg to lehmer64.
- `lace_start` now takes three parameters: the number of workers, the size of
  the task deque, the size of the program stack. There are reasonable defaults
  for the size of the task deque (100K) and program stack (16M).
- Improved algorithm to determine which cores to pin threads to with `hwloc`

### Fixed

- The flags `LACE_USE_HWLOC` and `LACE_USE_MMAP` were ignored because they were
  not propagated in the config header. This is now fixed.
- Correctly set the program stack size to at least 16M


## [1.4.2] - 2023-11-18

### Added

- Once again install pkg-config files.


## [1.4.1] - 2023-10-25

### Added

- Allow installing Lace again.

### Fixed

- Several "pedantic" warnings are now solved.


## [1.4.0] - 2023-03-19

## Added

- Improved support for Windows and added windows tests to the GitHub CI script.
- When suspended and a task is queued (via `RUN`), Lace now automatically
  resumes until the task is finished, then suspends again.

## Changed

- Don't use `thread_local` from `threads.h` since it's reportedly unreliable.
- Now allocate memory with `aligned_alloc` instead of `posix_memalign`.
- The `pi` benchmark now uses a local rng rather than `rand_r`.

## Fixed

- If `mmap` is not found, automatically disable `LACE_USE_MMAP`.
- Fixed Windows implementation of getting the number of processors.


## [1.3.1] - 2022-09-04

Lace no longer automatically generates the header files when building; instead,
the generated header files are part of the repository.


## [1.3.0] - 2022-09-04


## [1.2] - 2021-06-19


## [1.1] - 2021-04-17

In this new version of Lace, it is no longer possible to use the current thread
as a Lace thread.

Instead, `lace_start` starts the worker threads, `lace_stop` stops the worker
threads.
Then use `RUN(...)` to run a Lace task from outside Lace.

Only use `LACE_ME` and `CALL` macros from inside a Lace thread, i.e., if you are
running deep inside some Lace task.init

To use Lace:
- make new tasks like in the benchmark examples: `VOID_TASK_n` for tasks that
  don't return values and have `n` parameters, and `TASK_n` otherwise
- you can separate the `TASK_DECL_n` for header files and `TASK_IMPL_n` for
  implementation files if you want that
- to have Lace run a Lace task, use `RUN(taskname, param1, param2, ...)`
- if `RUN` is used from a Lace thread, this is detected and the Lace thread runs
  the task
- when using `RUN`, the current thread halts until the task is completed
- use macros `SPAWN`, `SYNC`, `CALL` from inside a Lace task to spawn/sync/call other
  Lace tasks
- if you are in a Lace worker thread but not a Lace task, use `LACE_ME` before
  using `SPAWN`/`SYNC`/`CALL`
- use `lace_suspend` and `lace_resume` to temporarily halt Lace workers
- use `lace_stop` (not while threads are suspended) to end Lace workers and
  reclaim memory


## [1.0] - 2017-02-02
