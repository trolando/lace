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


External task submission
------------------------

Non-Lace threads submit tasks via ``lace_run_task``. The task is
placed into one of 64 atomic slots (a fixed-size array). Workers
check an atomic counter ``external_task_count`` on each steal loop
iteration; when non-zero, they scan the array and claim a task with
``atomic_exchange``. The exchange atomically reads and clears the
slot, preventing double-execution.

The submitting thread blocks on a per-task semaphore until the worker
signals completion. The ``ext_lace_task`` struct lives on the
submitting thread's stack, so no heap allocation is needed.

Multiple external threads can submit concurrently without contention
on a single slot. The atomic counter keeps the fast path (no external
tasks) to a single relaxed load.


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