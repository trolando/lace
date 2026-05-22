Defining Tasks
==============

Tasks are declared with the ``TASK`` macro.
Place the macro in a header or at the top of a source
file; it generates the task descriptor and all associated functions. Then
provide the task body as a regular C function named ``NAME_CALL``.

.. code-block:: c

   TASK(int, my_task)
   int my_task_CALL(lace_worker* lw) { ... }

   TASK(int, fibonacci, int, n)
   int fibonacci_CALL(lace_worker* lw, int n) { ... }

   TASK(void, my_void_task, int, n)
   void my_void_task_CALL(lace_worker* lw, int n) { ... }

   TASK(void, process, int*, data, int, size)
   void process_CALL(lace_worker* lw, int* data, int size) { ... }


Generated functions
-------------------

Each ``TASK(RTYPE, NAME, ...)`` macro generates the following:

.. list-table::
   :header-rows: 1
   :widths: 25 75
   :class: func-table

   * - Function
     - Description
   * - ``NAME_CALL(lw, ...)``
     - Your task body — implement this.
   * - ``NAME(...)``
     - Run the task, blocking until done. Works from inside or outside a Lace worker.
   * - ``NAME_SPAWN(lw, ...)``
     - Fork: push a task onto the deque so it can be stolen. Returns a pointer to the task.
   * - ``NAME_SYNC(lw)``
     - Join: retrieve the result of the last spawned task (LIFO order).
   * - ``NAME_DROP(lw)``
     - Drop: cancel the last spawned task if not yet stolen, or discard its result if already stolen.
   * - ``NAME_NEWFRAME(...)``
     - Interrupt all workers and run this task.
   * - ``NAME_TOGETHER(...)``
     - Interrupt all workers and run a copy on each worker.

The ``lace_worker*`` pointer passed to ``_CALL`` must not be modified. It is
required by ``SPAWN``, ``SYNC``, and other Lace operations.


SPAWN and SYNC
--------------

``SPAWN`` and ``SYNC`` must be matched and used in **LIFO order**: if you
spawn A then B, you must sync B before A. Syncing out of order is undefined
behaviour.

Each ``SPAWN`` pushes a task onto the deque where it can be stolen by another
worker. ``SYNC`` retrieves the result of the last spawned task, waiting for
it if stolen, or executing it directly if not.

.. code-block:: c

   int fibonacci_CALL(lace_worker* lw, int n)
   {
       if (n < 2) return n;
       fibonacci_SPAWN(lw, n-1);         // push onto deque (may be stolen)
       int a = fibonacci_CALL(lw, n-2);  // execute directly
       int b = fibonacci_SYNC(lw);       // retrieve spawned result
       return a + b;
   }


Calling NAME() from any context
--------------------------------

``NAME(...)`` can be called from both inside and outside a Lace worker thread.
If called from inside a worker, it detects this automatically and calls
``NAME_CALL`` directly, skipping task submission entirely. This means you can
write library code that calls ``NAME()`` without knowing whether currently running
inside a Lace worker.


Dropping a spawned task
-----------------------

Instead of ``SYNC``, use ``DROP`` to abandon the last spawned task.
If the task has not yet been stolen, it is cancelled and never executed. If it
has already been stolen, the thief will still complete it but the result is
discarded. Like ``SYNC``, ``DROP`` must follow LIFO order relative to other
``SPAWN``/``SYNC``/``DROP`` calls.

.. code-block:: c

   my_task_SPAWN(lw, arg);
   // ... decide we don't need the result
   my_task_DROP(lw);


Interrupting workers
--------------------

Two special run modes interrupt currently executing tasks at the next steal
point (i.e. at ``SYNC`` or when idle):

``NAME_NEWFRAME(...)``: halts all workers and runs the given task on the
worker pool. The current task frame is suspended and resumed after the new
task completes. Typical use: stop-the-world garbage collection.

``NAME_TOGETHER(...)``: halts all workers and runs a copy of the given task
on *every* worker simultaneously. All workers start together and all complete
together (barrier semantics). Typical use: per-worker initialisation of
thread-local state.

Long-running tasks should call ``lace_check_yield()`` periodically to
cooperate with interruptions.


Per-task scratch storage
------------------------

Some tasks need a small amount of temporary memory whose size depends on
the input — a typical example is a buffer holding the children of an MTBDD
node during a recursive operation. Three options are available:

* ``alloca`` or C99 variable-length arrays. Fast, but the size sits on the
  thread stack; a deep recursion with input-dependent ``alloca`` can blow
  past the guard page and crash. MSVC also does not support VLAs, so this
  is not portable.
* ``malloc``/``free``. Portable and safe, but each call goes through the
  global allocator. For fine-grained tasks this allocator contention is
  often the dominant cost.
* The Lace **scratch arena** described below. Per-worker, lock-free, and
  about as fast as ``alloca`` while remaining portable.

Each worker owns a private bump arena backed by a large virtual-memory
reservation. The default is 1 GiB per worker on 64-bit systems, where
address space is essentially free, and 16 MiB per worker on 32-bit
systems, where user-mode address space is limited. Physical memory is
committed lazily on first use; idle workers release pages back to the OS
during deep backoff. The arena is private to one worker — no
synchronisation, no atomics.

Tasks that need scratch use a save/alloc/restore pattern:

.. code-block:: c

   BDD my_op_CALL(lace_worker* lw, BDD a, BDD b)
   {
       void* mark = lace_scratch_mark(lw);
       BDD* children = (BDD*)lace_scratch_alloc(lw, n * sizeof(BDD));
       // ... use children, possibly SPAWN/CALL/SYNC sub-tasks ...
       BDD result = mtbdd_makenode(...);
       lace_scratch_reset(lw, mark);
       return result;
   }

The discipline is the same as ``alloca``: a scratch pointer is valid for
the duration of the allocating frame and any descendants it transitively
spawns or calls. It must not be stored anywhere that could outlive the
frame.

Multiple ``return`` paths each need a matching ``lace_scratch_reset``
before they return. If a task forgets to reset, Lace detects this when
the owning worker next enters deep idle: it prints a one-line warning
and resets the arena automatically, so the program does not accumulate
leaks over a long run.

Tasks that do not allocate scratch pay nothing for this feature: the
virtual address space reservation costs nothing until a page is touched,
and no instrumentation runs in the task entry or exit paths.

The reservation size and the committed band (the hysteresis margin
above the current top) can be configured before ``lace_start``:

.. code-block:: c

   lace_set_scratch_size(4ULL * 1024 * 1024 * 1024);  // 4 GiB per worker
   lace_set_scratch_band(4 * 1024 * 1024);            // 4 MiB band
   lace_start(0, 0, 0);

Pass ``0`` to ``lace_set_scratch_size`` to disable the arena entirely.

