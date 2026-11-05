Design Notes
============

This page documents the internal design of Lace for contributors and
advanced users who need to understand the concurrency model. It is not
required reading for normal use.


Architecture overview
---------------------

Each Lace worker thread owns a double-ended queue (deque) of tasks.
The owner pushes and pops from the **head** (private end) in a
wait-free manner. Thieves steal from the **tail** (public end) using
a lock-free compare-and-swap on a shared ``TailSplit`` word.

The deque is split into a *private* region (between split and head)
and a *shared* region (between tail and split). Only tasks in the
shared region are visible to thieves. The split point is adjusted
lazily via a ``movesplit`` flag: when a thief finds no stealable
tasks, it sets ``movesplit`` on the victim, and the victim moves
the split on its next SPAWN.

Each task slot contains a function pointer and an atomic ``thief``
field. The ``thief`` field serves as a state machine:

* ``THIEF_TASK`` (0x1) — task is live, not yet stolen
* ``THIEF_EMPTY`` (0x0) — slot is empty / task cancelled
* ``THIEF_COMPLETED`` (0x2) — task was stolen and has finished
* any other pointer — task is being executed by that worker


Memory model
------------

Lace is designed for relaxed-memory architectures (ARM, POWER) and
uses explicit fences and atomic operations rather than relying on
x86's strong ordering. The key synchronisation points are:

**SPAWN (publishing a task).** After writing the task's function
pointer, arguments, and thief field, a ``release`` fence ensures
these stores are visible before the split is advanced. Without this
fence, a thief on ARM could observe the updated split but read
stale task data.

.. code-block:: text

   owner writes:   t->f, t->thief, t->args
   owner executes: atomic_thread_fence(release)
   owner writes:   split (making the task stealable)

**Steal (claiming a task).** A thief reads the victim's
``TailSplit`` word with a relaxed load and attempts a CAS to
increment the tail. The CAS itself provides acquire-release
semantics on the ``TailSplit`` word. After a successful CAS, the
thief has exclusive ownership of that task slot. The thief reads
the task data — these reads are ordered after the CAS, so they
observe the stores made by the owner before the release fence in
SPAWN.

.. code-block:: text

   thief reads:    victim->ts.v (relaxed)
   thief executes: CAS(&victim->ts.v, old, new) — acquires on success
   thief reads:    t->f, t->args (ordered after CAS)
   thief writes:   t->thief = self (relaxed — informational only)
   thief calls:    t->f(...)
   thief writes:   t->thief = THIEF_COMPLETED (release)

The ``release`` store of ``THIEF_COMPLETED`` ensures that all
stores made during task execution are visible to the owner when
it observes the completion.

**SYNC (fast path).** When a task has not been stolen
(``split <= head`` and ``movesplit == 0``), the owner simply
executes it directly. No atomic operations are needed — the owner
is the only thread that accesses its private deque region.

**SYNC (slow path / leapfrog).** When a task has been stolen, the
owner enters leapfrog mode: it follows the thief pointer and
attempts to steal work from the thief's deque, recursively. When the
thief has nothing to steal, the owner publishes the thief in its own
``waiting_on`` field and steals from the worker that the thief waits
for, down the chain (at most ``LACE_LEAP_HOPS``, 4): all of these work on
parts of the awaited task (transitive leapfrogging, as in Wool). A
waiting worker never steals unrelated tasks: such a task would keep
the join blocked under it until it finishes, which delayed the next
phase of phased algorithms (cholesky took twice as long). The owner
reads ``t->thief`` in a loop until it observes ``THIEF_COMPLETED``,
at which point an ``acquire`` fence ensures all stores made by the
thief (including the task result) are visible.

.. code-block:: text

   owner reads:    t->thief (relaxed, in loop)
   owner observes: t->thief == THIEF_COMPLETED
   owner executes: atomic_thread_fence(acquire)
   owner reads:    t->d.res (ordered after fence)

**Shrink shared region.** When the owner detects that its shared
region is too large (during SYNC slow path), it moves the split
point closer to the tail. A ``seq_cst`` fence between the split
write and the tail read prevents the StoreLoad reordering that
could cause both owner and thief to believe they own the same
task slot:

.. code-block:: text

   owner writes:   split = newsplit (relaxed)
   owner executes: atomic_thread_fence(seq_cst)
   owner reads:    tail (relaxed)

**TOGETHER / NEWFRAME handshake.** The initiator writes a task
into the ``lace_newframe.t`` pointer with a ``release`` fence
before the CAS. Workers poll this pointer with a relaxed load
in ``lace_check_yield``. When non-NULL, a worker executes an
``acquire`` fence before reading the task data via ``memcpy``.
This release-acquire pair ensures the task contents are fully
visible before any worker copies them.


External task submission
------------------------
 
Non-Lace threads submit tasks via ``lace_run_task``. The task is
placed into one of 64 atomic slots (a fixed-size array). Workers
check an atomic counter ``external_task_count`` in every round of the
steal loop; when non-zero, they scan the array and claim a task.
The scan uses a two-step approach: a relaxed ``atomic_load`` to
skip empty slots (read-only, no cache line invalidation), followed
by ``atomic_exchange`` only on non-NULL slots. This minimises
write traffic on the shared slot array when most slots are empty.
 
The submitting thread waits on a per-task state word, which the
worker moves from *running* to *done*. It first spins for
``LACE_EXT_SPIN_US`` (20 µs), since a short task finishes sooner than a
sleep and a wake-up take; a tiny task from outside Lace then costs
2-3 µs instead of a context switch. For the first ``LACE_EXT_YIELD_US``
(5 µs) it spins with PAUSE only; after that it also yields now and then,
in case it shares its CPU with the worker that runs its task. After the
spin it marks the state *sleeping* and waits on the futex without a
timeout. The worker swaps the state to *done* and calls ``futex_wake``
only if it was *sleeping*, so a short task costs no system call on
either side. The ``ext_lace_task`` struct lives on the submitting
thread's stack, so no heap allocation is needed and no initialisation
or destruction is required for the futex word (a wake-up that reaches
the address after the submitter returned is harmless).

When an external task is published, the submitter wakes one sleeping
worker if any sleeps and no sentinel is awake (see below), or ends a
sentinel's nap. A worker counts itself in ``n_sleeping`` and then checks
``external_task_count`` once more before it sleeps; both sides use
sequentially consistent operations, so either the worker sees the task
or the submitter sees the sleeper.

The task may spawn work while the other workers sleep, so a second
worker is woken to help (if no sentinel watches). The waiting submitter
does that, once it sees its task taken: the worker that took the task
then starts it at once instead of after a futex call, and a task that is
done before the submitter looks wakes nobody. If the submitter sleeps
already when the task is taken, the worker wakes the helper itself; the
submitter checks for a taken task after it marked itself sleeping, and
the worker checks for a sleeping submitter after it marked the task
taken, both sequentially consistent, so one of them always does.
 
Multiple external threads can submit concurrently without contention
on a single slot. The atomic counter keeps the fast path (no external
tasks) to a single relaxed load.


Worker idle strategy
--------------------

An idle Lace should use almost no CPU, and a parallel region that starts
after an idle period should start quickly; the scheduling experiments
(:doc:`scheduling-experiments`) measured each of the mechanisms below
against alternatives on several machines.

**Stage 1: Yield (0-256 failed attempts).** The worker calls
``sched_yield()`` (or ``SwitchToThread()`` on Windows) on each
iteration. This gives the OS scheduler an opportunity to run other
threads while remaining responsive to new work. No PAUSE stage
precedes this: on modern out-of-order CPUs, PAUSE instructions
provide insufficient throttling and the core continues to drive
cache-coherence traffic at near-full rate.

**Tiny steals.** A successful steal resets the backoff only if the stolen
task was worth a steal. Otherwise, with many idle workers and only small
tasks, the thieves keep succeeding at stealing worthless tasks and never
back off (a *steal storm*), and every stolen task makes its owner's join
remote. A stolen task counts as a failed steal if it ran shorter than
``LACE_IDLE_STEAL_MIN_X`` (16) times the thief's running average of its
steal cost (the time from the attempt until it holds the task, a few
cache-line transfers, more across chiplets and sockets and under
contention). Each sample moves the average by at most an eighth, between
half and twice its value, so a preempted steal does not throw it off. A
lost race for a task (``LACE_BUSY``) does not reset the backoff either.

**Sentinels.** Before it sleeps, a worker becomes a *sentinel* if fewer
than ``LACE_IDLE_SENTINELS`` (2) workers are, and it ran a task since it
last slept or another worker runs one now. A sentinel looks at every
other worker's deque in rounds (it only looks, it never steals), spins
for ``LACE_SENTINEL_SPIN_US`` (200 µs) after it became one, and then
naps ``LACE_SENTINEL_NAP_US`` (20 µs, with a timer slack of 1 µs on Linux)
on a futex between rounds; ``lace_run_task`` ends such a nap at once.
When a sentinel sees work, or an external task, it gives up its slot and
returns to the steal loop, which steals; while it was a sentinel it
never ran a task, so the slot always stands for a watching worker. A
sentinel retires when no worker has run a task for
``LACE_SENTINEL_GRACE_US`` (10 ms).

**Stage 2: Futex sleep.** The worker increments an ``n_sleeping`` counter,
then enters ``futex_wait`` on a shared ``wake_futex`` word. Because a
sentinel watches while any worker is active, the timeout can grow: it
starts at 100 µs and doubles per sleep up to 100 ms, so an idle Lace
wakes rarely. For ``LACE_IDLE_SHALLOW_US`` (20 ms) after it ran a task, a
worker keeps the shortest timeout, so that its core stays in a shallow
idle state and the next region starts fast.

**Wake signals.** Workers are woken from futex sleep by:

1. ``lace_run_task``: wakes one worker when an external task is
   submitted, a worker sleeps and no sentinel watches, and the
   submitter wakes a helper once the task is taken (see above).

2. A steal from the steal loop, before the stolen task runs: if the
   victim still has stealable tasks, the thief wakes as many sleepers as
   the victim has tasks left (one ``futex_wake``), so a large region gets
   all workers in one round of wake-ups instead of a chain of them; if
   no sentinel watches, it wakes one, for the work that the stolen task
   will spawn (a region after a long idle).

3. ``lace_stop``: wakes all sleeping workers so they observe the
   quit flag and exit promptly.

The ``wake_futex`` word is incremented (``fetch_add``) before each
``futex_wake`` call. This ensures the futex value has changed,
preventing the lost-wake race where a worker enters ``futex_wait``
between the value change and the wake signal.

Only the worker that ran an external task resets its backoff; the
others keep backing off, so that a stream of calls from outside Lace
does not keep every idle worker spinning.

A lone worker (``n_workers`` 1) has no victims, so every round of its
loop counts as a failed attempt and it goes through the same stages.

**Steal result signalling.** ``lace_steal`` returns one of:
 
- ``LACE_STOLEN`` — task stolen, victim still has more work
  (``tail < split`` after CAS).
- ``LACE_STOLEN_LAST`` — task stolen, but it was the last
  stealable task.
- ``LACE_BUSY`` — CAS failed, another thief was concurrent.
- ``LACE_NOWORK`` — victim has no stealable tasks.
 
 
Futex portability
-----------------
 
Lace uses address-based wait/wake primitives (commonly called
"futex") for the idle system and external task completion. These
operate on plain ``atomic_int`` values with no initialisation or
destruction required. The kernel maintains wait queues internally,
keyed by virtual address.
 
Platform implementations:
 
- **Linux**: ``futex(FUTEX_WAIT_PRIVATE)`` /
  ``futex(FUTEX_WAKE_PRIVATE)`` via ``syscall(SYS_futex, ...)``.
  The ``_PRIVATE`` variants avoid the overhead of shared-memory
  mapping lookups since all threads are in the same process.
 
- **FreeBSD**: ``_umtx_op(UMTX_OP_WAIT_UINT_PRIVATE)`` /
  ``_umtx_op(UMTX_OP_WAKE_PRIVATE)``. Timeout is passed via
  a ``struct _umtx_time`` for relative timeouts.
 
- **macOS 14.4+**: ``os_sync_wait_on_address_with_timeout`` /
  ``os_sync_wake_by_address_any`` / ``os_sync_wake_by_address_all``.
  This is the public, documented Apple API introduced in macOS 14.4.
 
- **macOS < 14.4**: ``__ulock_wait`` / ``__ulock_wake``. This is
  a private Darwin API used by the Rust standard library, Go
  runtime, and LLVM libc++. It has been stable since macOS 10.12.
 
- **Windows**: ``WaitOnAddress`` / ``WakeByAddressSingle`` /
  ``WakeByAddressAll`` from ``synchapi.h`` (Windows 8+). Requires
  linking with ``Synchronization.lib`` (``-lsynchronization`` for
  MinGW).


Barrier
-------

Lace uses a custom sense-reversing barrier. A shared ``wait`` flag is
flipped by the last thread to arrive (the leader). Other threads spin
on this flag with ``acquire`` loads for ``LACE_BARRIER_SPIN_US``
(20 µs), then sleep on it like an idle worker: in naps of 100 µs for
20 ms, then until woken. The leader wakes the sleepers if the
``sleeping`` counter says that a thread sleeps, which ends a nap at
once; the flip and the counter are ``seq_cst``, so either the leader
sees a sleeper or the sleeper sees the flip before it sleeps. Without
the sleep, threads that share CPUs with the threads they wait for (more
threads than CPUs) keep those off the CPU, and a TOGETHER can take
minutes. Without the naps, the core of a sleeper goes into a deep idle
state, which took 30-140 µs to leave against 9-26 µs from a nap. A
``leaving`` counter tracks how
many threads have exited the barrier, used by ``lace_barrier_destroy``
to wait for all threads to leave before tearing down the barrier.

The barrier provides full memory ordering: all memory operations by
any thread before the barrier are visible to all threads after the
barrier. This includes StoreLoad ordering — a store before the
barrier and a load after the barrier within the same thread cannot be
reordered. The guarantee follows from the ``acq_rel`` read-modify-write
on ``count``: unlike a separate release-store / acquire-load pair,
an ``acq_rel`` RMW is indivisible, so it prevents both prior stores
from floating past it (release) and later loads from floating before
it (acquire). Cross-thread visibility follows from the transitive
chain: each ``fetch_add`` on ``count`` acquires all stores released
by prior ``fetch_add`` operations (via the modification order of
``count``), so the leader acquires all pre-barrier stores from all
threads. The leader then releases through the ``wait`` flip, which
the waiting threads acquire.


Stack and deque allocation
--------------------------

Worker thread stacks and task deques are allocated as virtual memory
mappings (``mmap`` on Unix, ``VirtualAlloc`` on Windows). Pages are
committed lazily by the kernel on first access. This means:

* A 64 MB deque (1M task slots × 64 bytes) consumes only as much
  physical memory as the actual recursion depth requires.
* Worker stacks similarly consume physical memory proportional to
  actual call depth, not the reserved size.

On NUMA systems, first-touch policy places pages on the node where
the accessing thread runs. Since each worker is pinned before
executing any tasks, its stack and deque pages are allocated locally.
When hwloc is available, stacks are allocated with
``hwloc_alloc_membind`` for explicit NUMA binding.

Each stack has a guard page (``PROT_NONE`` / ``PAGE_NOACCESS``) at
the low end. The guard page is excluded from the region passed to
``pthread_attr_setstack``, so pthreads does not attempt to use it.

Because a reserved stack costs only address space, the default stack
(``stacksize`` 0 in ``lace_start``) is a 1 GB reservation per worker on
64-bit systems, like the scratch arena. Deep task trees need it: a
worker that waits for a stolen task runs other tasks on top of the
waiting frame (leapfrogging), so its stack can hold several root-to-leaf
paths at once.

On Windows only MSVC builds get the large default, since they create
threads with ``STACK_SIZE_PARAM_IS_A_RESERVATION``; MinGW and 32-bit
builds, and builds with ``LACE_DONT_OVERCOMMIT``, use the calling
thread's stack limit clamped to 16-64 MB. If the large stacks cannot be
reserved (an address-space limit such as ``ulimit -v``, or strict
overcommit accounting), Lace falls back to that conservative size. An
explicit ``stacksize`` is used as given, with a minimum of 16 MB.

Pages that a deep computation touched stay resident until ``lace_stop``.
``lace_release_memory`` gives them back while Lace keeps running: every
worker, at its next idle moment, releases its stack below the idle loop,
its deque past the first page, and its scratch arena past the first band
(on Windows only the scratch arena). On Linux this is ``madvise`` with
``MADV_DONTNEED``, which keeps the mapping and any hwloc binding; on other
systems ``madvise`` only marks pages for lazy reclaim (macOS keeps them
resident), so a fresh anonymous mapping replaces the range. Lace does not
do this by itself when workers go to sleep (it does trim the scratch arena
then): a program that runs one deep computation after another would only
fault the same pages in again.


Scratch arena
-------------

Each worker owns a private bump-allocated arena for task-local temporary
storage (see :doc:`tasks` for the user-facing API). The arena exists
because the three obvious alternatives all have problems for the
fork-join workloads Lace targets:

* ``alloca`` / C99 VLAs run on the thread stack and overflow without
  warning past the guard page; MSVC does not support VLAs at all.
* ``malloc``/``free`` introduces allocator contention on workloads with
  many fine-grained tasks, dominating the cost of the task itself.
* A shared lock-free allocator would still bounce cache lines between
  workers on every allocation.

A per-worker arena avoids all three: the allocations are stack-like
bump operations on private state, no cache lines are shared, and the
backing memory is bounded by the worker's recursion depth rather than
by a fixed stack size.


Virtual memory layout
~~~~~~~~~~~~~~~~~~~~~

Each worker reserves a large virtual address range at startup (1 GiB
on 64-bit, 16 MiB on 32-bit, configurable). The reservation is purely
address space; on 64-bit systems address space is essentially free and
typical totals fit comfortably below any practical limit. On 32-bit
systems user-mode address space is limited (typically 2–3 GiB) so the
default is sized to fit a moderate number of workers without exhausting
the address space; an application that needs more should call
``lace_set_scratch_size`` before ``lace_start``. ``lace_start`` reserves
the arenas of all workers before they start. If the 64-bit default does
not fit (an address-space limit such as ``ulimit -v``), it falls back to
16 MiB per worker, as it does for the stacks; a size set with
``lace_set_scratch_size`` is used as given.

The arena exposes four pointers per worker:

* ``scratch_base`` — base of the reservation
* ``scratch_top`` — current allocation pointer (hot, written on every
  ``lace_scratch_alloc``)
* ``scratch_committed_end`` — one past the highest currently-committed
  byte (hot, read on every allocation as a bound check)
* ``scratch_reserve_end`` — one past the end of the reservation
  (cold, only consulted on growth)

The two hot fields live adjacent to the deque pointers in the worker
struct, in the cache line that every SPAWN/SYNC already touches.
``lace_scratch_alloc`` is then a load + add + compare + conditional
out-of-line branch + store — about four instructions on the fast path,
with branch prediction trivially correct (the slow path is taken at
most once per band's worth of allocations).


Reserve / commit / decommit
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Allocations bump ``scratch_top``. When the bump crosses
``scratch_committed_end``, ``lace_scratch_grow`` commits additional
pages by calling ``mprotect(PROT_READ|PROT_WRITE)`` on POSIX or
``VirtualAlloc(MEM_COMMIT, PAGE_READWRITE)`` on Windows. To avoid
syscalling on every small push/pop oscillation near the top, the grow
path commits a *band* of pages above the immediate request (1 MiB by
default, configurable). The fast path stays in the committed band
without further system calls until the band is exhausted.

The reserve uses ``PROT_NONE`` / ``PAGE_NOACCESS`` rather than
``PROT_READ|PROT_WRITE`` with ``MAP_NORESERVE``. This is more work
on the commit path but provides guard semantics: a runaway recursion
that overshoots the band hits a segfault rather than silently
faulting in zero pages. Reservation exhaustion is caught explicitly
in ``lace_scratch_grow`` with a diagnostic message before aborting.

The decommit primitive uses ``madvise(MADV_DONTNEED)`` followed by
``mprotect(PROT_NONE)`` on POSIX, and ``VirtualFree(MEM_DECOMMIT)``
on Windows. The ``MADV_DONTNEED`` returns the physical pages to the
OS immediately; the ``PROT_NONE`` ensures that any stray access past
the new top fails fast.


NUMA placement
~~~~~~~~~~~~~~

The reservation is created in ``lace_init_worker`` on the worker's own
thread, after CPU pinning. The reservation itself touches no pages;
all physical allocations happen later, on first access from the same
worker thread, so first-touch NUMA policy places the pages on the
worker's local node. No explicit NUMA binding is needed.


Trim on deep idle
~~~~~~~~~~~~~~~~~

When a worker exhausts its steal attempts and transitions into the
futex-wait stage of the idle progression (see *Worker idle strategy*),
Lace decommits arena pages above ``scratch_top + band``. The trim
fires exactly once per backoff sequence: ``idle_count`` resets on any
successful steal, so the next dry spell will trim again from a new
high-water mark. The trim is invoked from the same cold code path
that is already about to make a ``futex_wait`` syscall, so its cost
is invisible.

By construction the worker is between tasks when it idles, so
``scratch_top`` equals ``scratch_base`` and the trim leaves only the
band committed — enough to absorb the first wave of allocations after
wake without faulting in pages.


Leak detection
~~~~~~~~~~~~~~

The arena's correctness depends on every ``lace_scratch_mark`` being
paired with a matching ``lace_scratch_reset`` on every return path.
A missing reset would not corrupt anything — the next outer reset
will eventually restore — but it can cause the arena to grow without
bound until the reservation is exhausted.

The same deep-idle transition that triggers the trim also performs a
leak check: if ``scratch_top != scratch_base`` at the point when all
task frames have unwound, Lace prints a one-line warning identifying
the worker and the leaked byte count, then resets ``scratch_top`` to
``scratch_base``. The warning is emitted once per worker per program
lifetime to avoid flooding logs.

This combination — warn loudly, recover quietly — keeps a leak from
killing a long-running program while still making the bug visible
during development.
