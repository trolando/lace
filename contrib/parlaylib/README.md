# Lace scheduler plugin for ParlayLib

[ParlayLib](https://github.com/cmuparlay/parlaylib) selects its scheduler at
compile time from a few plugins (its own, OpenMP, TBB, OpenCilk). `lace.h`
adds Lace as one more, so ParlayLib programs, such as the PBBS v2 benchmarks,
run on Lace without changes.

## Install

```sh
cd parlaylib
patch -p1 < /path/to/lace/contrib/parlaylib/parallel.h.patch
cp /path/to/lace/contrib/parlaylib/lace.h include/parlay/internal/scheduler_plugins/
```

The patch adds a `PARLAY_LACE` branch to `parlay/parallel.h`. It applies to
ParlayLib from 2023 (f8e38c7, the version PBBS v2 uses) to 2025 (5101769).

Compile with `-DPARLAY_LACE`, Lace's headers on the include path (`src/` and
the build directory, for `lace_config.h`), and link `liblace.a` and
`-pthread`.

```sh
c++ -std=c++17 -O2 -DPARLAY_LACE -Iparlaylib/include -Ilace/src -Ilace/build \
    prog.cpp lace/build/liblace.a -pthread
```

## Behaviour

- `par_do(left, right)` spawns `right`, calls `left`, and syncs on `right`.
- `parallel_for` halves the range with `par_do`. With granularity 0 it picks
  the grain the way Parlay's own scheduler does: it times a growing prefix
  of the loop until a round takes 1 µs.
- Lace starts on first use with `PARLAY_NUM_THREADS` workers (default: all
  cores Lace may use), unless the program started Lace itself. If the plugin
  started Lace, it stops Lace at exit.
- A thread that is not a Lace worker, such as `main`, enters Lace for each
  outermost `par_do` or `parallel_for`. The call runs as one task on a
  worker while the calling thread waits.
- `worker_id()` is 0..n-1 on the workers and n on any other thread, so
  `num_workers()` is n + 1. Only one outside thread at a time may use
  worker-indexed state such as `parlay::WorkerSpecific`.
- The `conservative` flag is ignored. While it waits for a stolen task, a
  worker runs tasks spawned by that task (leapfrogging), and rarely, when
  the thief has just finished it, another task, so holding a lock across a
  `par_do` can deadlock, as with Parlay's own scheduler without
  `conservative`.
- `execute_with_scheduler` is not available, as with the OpenMP and TBB
  plugins.
- Exceptions must not leave `left` or `right`.

## Test

`test_parlay.cpp` checks `par_do`, `parallel_for` (several grains, empty
and nested ranges), worker ids, and sort, reduce, scan and filter on top,
plus 20000 short loops started from `main`.

```sh
c++ -std=c++17 -O2 -DPARLAY_LACE -Iparlaylib/include -Ilace/src -Ilace/build \
    test_parlay.cpp lace/build/liblace.a -pthread -o test_parlay
PARLAY_NUM_THREADS=4 ./test_parlay
```
