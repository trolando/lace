# Changelog

All notable changes to Lace will be documented in this file.

## [1.5.3] - 2026-03-17

###  Fixed

- Fixed a few compilation warnings


## [1.5.2] - 2026-03-02

### Added

- CMakeLists.txt now has flags to enable sanitizing (GCC/CLang only)

### Changed

- If Lace is the top level project, the default build type is Release

### Fixed

- Fixed several issues that were flagged by the sanitizers


## [1.5.1] - 2025-08-05

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


## [1.5.0] - 2025-08-03

### Added

- Added a more 'proper' Changelog (this file).
- Added a large number of benchmarks from the Nowa repository.

### Changed

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
