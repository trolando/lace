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
attempts to steal work from the thief's deque, recursively.
The owner reads ``t->thief`` in a loop until it observes
``THIEF_COMPLETED``, at which point an ``acquire`` fence ensures
all stores made by the thief (including the task result) are
visible.

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


xternal task submission
------------------------
 
Non-Lace threads submit tasks via ``lace_run_task``. The task is
placed into one of 64 atomic slots (a fixed-size array). Workers
check an atomic counter ``external_task_count`` periodically in the
steal loop; when non-zero, they scan the array and claim a task.
The scan uses a two-step approach: a relaxed ``atomic_load`` to
skip empty slots (read-only, no cache line invalidation), followed
by ``atomic_exchange`` only on non-NULL slots. This minimises
write traffic on the shared slot array when most slots are empty.
 
The submitting thread blocks on a per-task ``atomic_int done``
field, using a futex wait. Before falling back to the futex, the
submitter briefly spins (256 PAUSE iterations) to avoid a kernel
transition when the task completes quickly (typical for fine-grained
BDD operations). The worker signals completion with an atomic store
and ``futex_wake``. The ``ext_lace_task`` struct lives on the
submitting thread's stack, so no heap allocation is needed and no
initialisation or destruction is required for the futex word.
 
When an external task is published, the submitter wakes one sleeping
worker via ``futex_wake``. This ensures that even when all workers
are in deep futex sleep, external tasks are picked up promptly.
 
Multiple external threads can submit concurrently without contention
on a single slot. The atomic counter keeps the fast path (no external
tasks) to a single relaxed load.


Worker idle strategy
--------------------
 
When a worker fails to find work, it progresses through idle stages
of increasing aggressiveness to balance responsiveness against CPU
and cache-bus overhead.
 
**Stage 1: Yield (0–256 failed attempts).** The worker calls
``sched_yield()`` (or ``SwitchToThread()`` on Windows) on each
iteration. This gives the OS scheduler an opportunity to run other
threads while remaining responsive to new work. No PAUSE stage
precedes this: on modern out-of-order CPUs, PAUSE instructions
provide insufficient throttling and the core continues to drive
cache-coherence traffic at near-full rate.
 
**Stage 2: Futex sleep with ramping timeout (256+ failed attempts).**
The worker increments an ``n_sleeping`` counter, then enters
``futex_wait`` on a shared ``wake_futex`` word. The timeout starts
at 100 µs and increases by 100 µs per futex cycle, capping at
1 ms. This ramp avoids two failure modes:
 
- Too-short timeouts cause excessive futex syscall overhead
  (each ``futex_wait``/return is ~3–5 µs of kernel entry/exit).
- Too-long timeouts cause barrier-synchronized workloads
  (cholesky, LU) to miss the start of a new parallel phase.
 
The 1 ms cap was determined empirically: 10 ms caused measurable
regressions in LU decomposition; 100 ms caused severe regressions
in multiple structured benchmarks.
 
**Wake signals.** Workers can be woken from futex sleep by:
 
1. ``lace_run_task``: always wakes one worker when an external task
   is submitted. This is the primary mechanism for external task
   responsiveness.
 
2. Successful steal with remaining work (``LACE_STOLEN``, not
   ``LACE_STOLEN_LAST``): the thief knows the victim still has
   stealable tasks, so it wakes one sleeping worker to help.
   This creates a cascade: workers wake proportionally to
   available parallelism. The ``LACE_STOLEN_LAST`` distinction
   prevents spurious wakes when the stolen task was the last one.
 
3. ``lace_stop``: wakes all sleeping workers so they observe the
   quit flag and exit promptly.
 
The ``wake_futex`` word is incremented (``fetch_add``) before each
``futex_wake`` call. This ensures the futex value has changed,
preventing the lost-wake race where a worker enters ``futex_wait``
between the value change and the wake signal.
 
**Steal result signalling.** ``lace_steal`` returns one of:
 
- ``LACE_STOLEN`` — task stolen, victim still has more work
  (``tail < split`` after CAS). Triggers a wake signal.
- ``LACE_STOLEN_LAST`` — task stolen, but it was the last
  stealable task. No wake signal.
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
on this flag with ``acquire`` loads. A ``leaving`` counter tracks how
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
the spinning threads acquire.


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
``lace_set_scratch_size`` before ``lace_start``.

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
