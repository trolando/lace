Migrating from Lace v1
======================

Lace v2 changes how task bodies are written. In v1, the task body was placed
directly inside the ``TASK_N`` macro and ``SPAWN``/``CALL``/``SYNC`` were
macros that hid the worker pointer. In v2, the body is a regular C function
(``NAME_CALL``) and the worker pointer is explicit.

**v1:**

.. code-block:: c

   TASK_1(int, fibonacci, int, n)
   {
       if (n < 2) return n;
       int m, k;
       SPAWN(fibonacci, n-1);
       k = CALL(fibonacci, n-2);
       m = SYNC(fibonacci);
       return m + k;
   }

**v2:**

.. code-block:: c

   TASK_1(int, fibonacci, int, n)

   int fibonacci_CALL(lace_worker* lw, int n)
   {
       if (n < 2) return n;
       fibonacci_SPAWN(lw, n-1);
       int k = fibonacci_CALL(lw, n-2);
       int m = fibonacci_SYNC(lw);
       return m + k;
   }

Key differences
---------------

**Task body is a plain C function.** The ``TASK_N`` macro only generates
the task descriptor and wrapper code. You write the body as
``NAME_CALL(lace_worker* lw, ...)``. This means the body is visible to
debuggers, can be stepped through in GDB, and can call non-Lace helper
functions while still propagating the worker pointer.

**Worker pointer is explicit.** Instead of hidden thread-local lookups,
the ``lace_worker*`` is passed as the first argument. This makes the data
flow visible and avoids a TLS access on every spawn/sync.

**SPAWN/SYNC/CALL are name-prefixed.** Instead of ``SPAWN(fibonacci, n-1)``,
write ``fibonacci_SPAWN(lw, n-1)``. Instead of ``SYNC(fibonacci)``, write
``fibonacci_SYNC(lw)``.

**RUN is replaced by calling the task directly.** The old
``RUN(task, args...)`` macro is gone. Simply call the task by name as a
regular function: ``fibonacci(42)``. If called from a Lace worker, this
automatically falls back to ``fibonacci_CALL`` with no task submission
overhead.

**LACE_ME is gone.** In v1, ``LACE_ME`` was needed to obtain the worker
pointer inside a Lace thread that was not a Lace task. In v2, the worker
pointer is always passed explicitly.

**lace_startup is now lace_start.** The initialising thread is no longer
a Lace worker. Starting Lace with N workers simply starts N threads.

**lace_suspend/lace_resume are gone.** Use ``LACE_BACKOFF`` (enabled by
default) instead — idle workers sleep automatically with exponential backoff.