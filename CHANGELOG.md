# Changelog

All notable changes to Lace will be documented in this file.

## [1.11.0] - 2026-11-09

### Changed

- Each sleeping worker waits on its own futex word, and a waker wakes chosen
  sleepers, the lowest-numbered first: one shared word caused wake storms at
  96-192 workers. The globals of the idle path have their own cache lines.
- An idle worker searches while its search balance is positive. Searching
  spends it, and a worthwhile stolen or external task earns 8 times its
  useful time (within 1 ms either way): workers that find work search
  through gaps, workers that find only crumbs soon sleep.
- With pinned workers on Linux, 75% of random steal attempts go to the
  thief's own package (socket), and consecutive wake-ups go to different
  packages.
- A thief whose previous steal was a crumb wakes no sleepers for its
  victim's remaining tasks, and does not prefer that victim next.
- On a 4-socket Xeon (96 cores, 192 threads), the last three changes make
  mCRL2's symbolic reachability 5-28% faster with the same or less CPU time;
  the geometric mean of the Lace benchmarks changes by less than 2% on 16-96
  cores.

## [1.10.0] - 2026-11-08

### Fixed

- In a NEWFRAME or TOGETHER, a worker that went into deep idle in the frame's
  steal loop reset its scratch arena under the task it had interrupted,
  which still used it: a "leaked scratch" warning, then crashes or wrong
  data (seen in mCRL2 with Sylvan's garbage collection as the frame). The
  loop now resets only to the scratch it started with (`test_scratch_frame`).

### Removed

- `LACE_USE_HWLOC` and the hwloc code, deprecated in 1.9.0: Lace pins its
  workers itself on Linux, FreeBSD and Windows. CMake warns when the option
  is still set, and ignores it.

## [1.9.0] - 2026-11-07

### Added

- Lace pins its workers on FreeBSD (13.1 or later) as on Linux, with the CPU
  topology from the sysctl `kern.sched.topology_spec`.
- Lace pins its workers on Windows, within the processor group of the thread
  that calls `lace_start`, with the topology from
  `GetLogicalProcessorInformationEx`.

### Changed

- `lace_start` reads the CPU topology for pinning once per process, and fewer
  files (one per core and one per package instead of two per CPU): on Linux
  machines with many CPUs a start took 1-3 ms, mostly reading sysfs.

### Deprecated

- `LACE_USE_HWLOC`: Lace pins its workers itself on Linux, FreeBSD and
  Windows, and `lace_start` takes about 50 ms less without hwloc. Binding the
  memory of the workers to their NUMA node, as hwloc does, was not faster
  than first touch by the pinned worker.

### Fixed

- A worker set up its data and deque before Lace pinned it, so first touch
  could place them on another NUMA node than the worker's (half of the
  workers on a two-socket machine); Lace now pins first (`test_numa`).
- A worker could steal from a worker still being set up, and its request to
  move the split put that worker's data on the thief's NUMA node; the
  workers now start together, at a barrier.

## [1.8.2] - 2026-11-07

This release takes over the local patches of mCRL2.

### Changed

- `LACE_NATIVE_OPT` (`-march=native`) is ON by default only when Lace is the
  top-level project, so programs that include Lace stay portable.

### Fixed

- On MSVC, Lace read the clock with `rdtscp`, which some (virtual) CPUs
  lack; it now uses `rdtsc`.
- Builds with `LACE_SANITIZE_THREAD` failed with Clang.
- CMake failed when the build directory was the source directory.
- The configuration summary was also printed when Lace is a subproject, and
  its last lines (tests, benchmarks, sanitizers) were never printed.

## [1.8.1] - 2026-11-07

### Changed

- SPAWN no longer executes a memory fence: the tasks are published by a
  release store when the split point moves up. On ARM this removes a full
  barrier from every spawn; on x86 the code is unchanged.

## [1.8.0] - 2026-11-06

This release backports the scheduling changes of Lace 2.5.0.

### Added

- Workers are pinned by default on Linux: each to its own CPU if the
  workers fill the available cores, otherwise to all of them.
  `lace_set_pinning()`, `lace_set_pin_cpus()` and the `LACE_PIN` environment
  variable choose other modes, and the CMake option `LACE_DEFAULT_PIN` sets
  the default (`test_pin`). With hwloc, the mode `off` turns pinning off.
- `lace_release_memory()` gives the memory that idle workers hold back to
  the operating system (`test_release`).
- Statistics builds count leap strays and report the stack high-water mark
  of each worker.
- A test for mixed SYNC and DROP, `test_drop`.
- A test that Lace starts under an address-space limit, `test_stack_limit`.

### Changed

- A worker that waits at a join no longer steals random tasks. It steals
  only from its thief and, transitively, from the workers that thief waits
  for.
- Idle workers use much less CPU. Two idle workers stay awake as sentinels
  and wake the others when work appears, so sleeping workers back off to
  100 ms instead of 1 ms. TOGETHER and NEWFRAME wake sleeping workers.
- Idle workers no longer fight over tasks that are too small to be worth
  stealing (steal storms).
- Cheaper calls from outside Lace: `RUN` spins briefly before it sleeps,
  and wake-ups are only sent to threads that sleep.
- Lace's own targets are built without stack protector, unless
  `LACE_HARDENING` is set.
- Leaner steal and leapfrog code. Random victims are picked without
  division.
- SPAWN, SYNC and DROP find the worker's public struct at a fixed offset
  instead of loading a pointer.
- Larger worker stacks: a stack size set with `lace_set_stacksize` is no
  longer clamped to 64 MB, and the default is a 1 GB reservation per worker
  on 64-bit systems. The new option `LACE_DONT_OVERCOMMIT` keeps the old
  default (`test_stack`).
- The benchmarks print a checksum of their result.

### Removed

- The `LACE_LEAP_RANDOM` macro (random steals while waiting at a join).

### Fixed

- A single worker kept a core busy while idle (`test_idle`).
- Without hwloc, Lace ignored the affinity mask on Linux (`taskset`,
  cgroups, SLURM) and started one worker per online CPU (`test_affinity`).
- With hwloc, Lace pinned workers to cores outside the affinity mask
  (`test_pin`).
- With hwloc, every worker printed errors where the system cannot pin
  threads or bind memory (macOS); Lace now says once that it cannot pin,
  and CMake warns about it on macOS.
- Newer Clang failed to build Lace (`-Wunused-but-set-global` with `-Werror`).
- Clang 18 failed to build Lace with `-march=native` on CPUs with AVX10.1.
- Parallel regions started while the workers slept ran almost serially.
- Lace no longer consumes the program's `rand()` sequence (`test_rand`).
- `lace_start` exited the program under an address-space limit
  (`ulimit -v`); the scratch arenas now fall back to 16 MiB per worker.
- Workers waiting at a barrier now back off like idle workers instead of
  spinning; with more threads than CPUs, the spinning made TOGETHER and
  NEWFRAME take minutes.


## [1.7.0] - 2026-05-23

### Added

- **Per-worker scratch arena.** Each worker owns a private bump-allocated
  arena backed by a virtual-memory reservation. Tasks can use
  `lace_scratch_alloc` / `lace_scratch_mark` / `lace_scratch_reset` for
  fast, contention-free task-local temporary storage; the
  `LACE_SCRATCH_ALLOC` / `LACE_SCRATCH_MARK` / `LACE_SCRATCH_RESET`
  convenience macros use the implicit `__lace_worker` inside TASK
  bodies. The arena commits pages lazily on first use and releases
  them back to the OS during deep backoff (the futex-wait stage of
  the idle progression). Designed as an alternative to `alloca`/VLAs
  (which overflow without warning and are unportable to MSVC) and
  `malloc`/`free` (allocator contention dominates fine-grained
  workloads). The default reservation is 1 GiB per worker on 64-bit
  systems and 16 MiB per worker on 32-bit systems. Configurable via
  `lace_set_scratch_size` and `lace_set_scratch_band`; set the size
  to 0 to disable. Tasks that do not allocate scratch are unaffected:
  the arena costs only virtual address space until something allocates.
- **Idle leak detection for the scratch arena.** When a worker enters
  the futex-wait stage with `scratch_top != scratch_base`, Lace warns
  once per worker and automatically resets the arena, protecting
  long-running programs from leaking missed `lace_scratch_reset` calls.


## [1.6.3] - 2026-04-06

### Added

- Now also tests FreeBSD in the CI.
- Added crash handler to the test programs to get better diagnostics on
  CI failures.

### Fixed

- Minor fixes for FreeBSD systems.


## [1.6.2] - 2026-04-05

### Changed

- Fixed libraries in the pkg-config files on Windows systems.
- Tests now check if Lace can be installed and used correctly with pkg-config
  and CMake.

## [1.6.1] - 2026-04-04

### Changed

- Replaced nanosleep-based backoff with a futex-based progressive idle
  system. Workers yield briefly, then enter `futex_wait` with a ramping
  timeout (100 µs → 1 ms). Sleeping workers are woken promptly when
  work appears (external task submission, successful steal with
  remaining work). Uses platform-native futex primitives on Linux,
  FreeBSD, macOS, and Windows.
- Various changes to reduce external task overhead: per-task semaphore
  replaced by futex on `atomic_int`, spin-before-futex on the
  completion path, load-before-exchange in slot scanning, and
  `LACE_STOLEN_LAST` to avoid spurious wake signals.
- External-task-heavy workloads (e.g., JNI bridge of NDD): 77.8 s → 25.9 s
  (3× improvement) on NQueens-12 with 20 workers. Single-worker
  external task throughput improved from 80 s to 17 s. Standard Lace
  benchmarks show no regression.
- On Windows, now requires linking with `Synchronization.lib`.


## [1.6.0] - 2026-03-31

This release backports correctness fixes, portable abstractions, and
performance improvements from Lace v2.3.0 into the v1 branch. The v1
task macro API (`SPAWN`/`SYNC`/`CALL`/`RUN`, `TASK_N`/`VOID_TASK_N`,
`__lace_worker`/`__lace_dq_head` parameters) is preserved. Existing
code using the v1 API should continue to work, with the exception of
the removed `lace_suspend`/`lace_resume` functions and the repurposed
`TASK` macro (the old `TASK(foo)` shorthand for `foo_CALL` is replaced
by the new generic `TASK(rtype, name, ...)` dispatch macro).

Instead of `VOID_TASK_N` and `TASK_N` macros, users can now use the more
convenient `TASK(rtype, name, ...)` macro for defining tasks. For void
tasks, use `TASK(void, name, ...)`. The explicit `TASK_N`, `VOID_TASK_N`,
`TASK_DECL_N`, and `TASK_IMPL_N` macros remain available.

### Added

- Generic `TASK(rtype, name, ...)` macro that dispatches to `TASK_N` or
  `VOID_TASK_N` automatically. Use `TASK(void, name, ...)` for void tasks.
  The explicit `TASK_N`, `VOID_TASK_N`, `TASK_DECL_N`, and `TASK_IMPL_N`
  macros remain available. The old `TASK(foo)` macro is replaced; use
  `foo_CALL` instead.
- `LACE_BACKOFF` option (default ON): idle workers sleep with exponential
  backoff (50 iterations per doubling, 1 ms cap), reducing CPU usage to
  near zero when there is no work.
- `lace_is_running()` returns 1 if Lace is running, 0 otherwise.
- `lace_sleep_us()` portable microsecond sleep. On Windows uses a
  per-thread waitable timer with QPC spin-wait for sub-300 µs delays.
- Multi-threaded external task submission: using `RUN` to run a task can
  now be called concurrently from up to 64 non-Lace threads without
  contention on a single atomic slot.
- New stress test `test_external` exercising concurrent external task
  submission with simple tasks, fibonacci tasks, and tree-recursive
  mixed workloads.
- New test `test_backoff` for verifying backoff behavior.
- New test `test_barrier` for verifying barrier correctness.
- Portable abstraction layer backported from Lace v2: `lace_sem_t`,
  `lace_mutex_t`, `lace_cond_t`, `LACE_TLS`, `LACE_LIKELY`/`LACE_UNLIKELY`,
  `LACE_NOINLINE`, `LACE_NORETURN`, `LACE_ALIGN`, `LACE_UNUSED`.
- Portable high-resolution timer `lace_gethrtime()` replacing the
  x86-only `rdtsc` inline assembly. Supports x86 (rdtsc/rdtscp),
  Windows (QueryPerformanceCounter), macOS (mach_absolute_time), and
  POSIX (clock_gettime).
- Worker thread stacks are now explicitly allocated with `mmap` (Unix)
  or `hwloc_alloc_membind` (when hwloc is enabled), with a guard page
  at the low end. On NUMA systems this ensures stack pages are placed
  on the correct memory node.

### Changed

- Default task deque size increased from 100 000 to 1 048 576 entries.
  Since deques are now backed by virtual memory (`mmap` / `VirtualAlloc`),
  only pages actually touched consume physical memory.
- Random number generator replaced: LCG (`LACE_TRNG`) replaced by
  xoroshiro128** (`lace_rng`). `LACE_TRNG` remains as a compatibility
  alias.
- Barrier implementation now uses correct memory ordering: `acq_rel` on
  the arrival counter, `acq_rel` on the wait flip, `acquire` on the
  spin loop, and `release` on the leaving counter. Previously used
  `relaxed` everywhere, which was incorrect on ARM/POWER.
- SPAWN fence corrected from `memory_order_acquire` to
  `memory_order_release`. The purpose is to prevent task data stores
  from reordering past the split update. The old fence direction was
  incorrect on weak-memory architectures (correct on x86 by accident).
- Steal, shrink, and leapfrog functions now use explicit
  `atomic_load_explicit` / `atomic_store_explicit` instead of plain
  reads/writes of `TailSplit` members.
- TOGETHER and NEWFRAME wrappers now have a `release` fence before
  the CAS on `lace_newframe.t`, ensuring task data is visible to
  workers on weak-memory architectures.
- `YIELD_NEWFRAME` now includes an `acquire` fence after observing
  a non-NULL `lace_newframe.t`, ensuring the task data read by
  `lace_yield` is consistent.
- `TASK_IS_STOLEN` and `TASK_IS_COMPLETED` macros now use
  `atomic_load_explicit` instead of plain reads.
- Checked return values of `pthread_create`, `pthread_attr_init`,
  `pthread_attr_setstack`, `getrlimit`, and `lace_sem_init`. All
  failures now produce a diagnostic message and exit.

### Removed

- `lace_suspend` and `lace_resume` are removed. Use `LACE_BACKOFF`
  (enabled by default) instead — idle workers sleep automatically.
- `lace_run_task_exclusive` and `RUNEX` are removed.
- Retired the `LACE_USE_MMAP` CMake option. Deques are now always
  allocated with `mmap` (Unix) or `VirtualAlloc` (Windows).
- Removed the old macOS semaphore `#define` hacks. Replaced by the
  portable `lace_sem_t` abstraction using GCD dispatch semaphores.


## [1.5.4] - 2026-03-26

### Changed

- Now allocates memory on the right memory node with HWLOC enabled.

### Fixed

- Now checks the return value of various important calls, including pthread_create.
- If the detected stack size (with stacksize == 0) is insanely high, default to max 64MB


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
