Your own threads
================

``lace_start`` creates the worker threads. Some programs need their own
threads as the workers instead: a runtime that creates its threads itself and
runs the same code in all of them (such as the HRE runtime of LTSmin), threads
that carry the program's own thread-local state, or a program that places its
threads itself. ``lace_start_external`` starts Lace without creating threads,
and the program's threads join as workers.

Starting and stopping
---------------------

Start Lace once, with the number of threads that will join. The threads may
already run, but each enters only after ``lace_start_external`` has returned,
for example after a barrier of the program's runtime, or by creating the
threads afterwards. Each thread enters with its own worker id, from 0 to
``n - 1``, and ``lace_worker_enter`` returns once all ``n`` threads have
entered: the workers start together. A thread that only works steals until
Lace stops; one thread can instead generate the tasks itself:

.. code-block:: c

   lace_start_external(n, 0);       // before any thread enters

   // in each of the n threads, with its own id
   void thread_main(unsigned int id)
   {
       lace_worker_enter(id);
       if (id == 0) {
           root(...);               // runs as worker 0; the others steal its tasks
           lace_worker_leave();
           lace_stop();             // the others' loops return; waits until all have left
       } else {
           lace_worker_loop();      // steal and run tasks until lace_stop
           lace_worker_leave();
       }
   }

   // afterwards the program joins its threads

Worker 0 in this example is a worker like the others: other workers steal its
tasks, and when it waits for a stolen task at a ``SYNC``, it steals from the
thief (leapfrogging), but it does not look for work elsewhere.

All ``n`` threads can also just steal. Then another thread, which is not a
worker, calls the tasks as usual (``fib(42)``) and finally ``lace_stop()``.

``lace_stop`` makes every ``lace_worker_loop`` return and waits until all
``n`` workers have left; then it frees what Lace allocated. After that, Lace
can be started again, with ``lace_start`` or ``lace_start_external``, and any
thread can take any worker id.

Rules
-----

* Each worker id is entered by one thread per run, and a thread is at most one
  worker at a time. Every id must be entered: ``lace_worker_enter`` waits for
  all ``n`` threads, and so does ``lace_stop``.
* ``lace_worker_leave`` and ``lace_stop`` are never called from inside a task.
  ``lace_stop`` is called by a thread that is not a worker, or that has left.
* A worker that does not steal, such as worker 0 above, still takes part in
  ``TOGETHER`` and ``NEWFRAME``, which wait for all workers: between tasks it
  should not do long work outside Lace. While it is a worker, the idle workers
  stay ready to steal its tasks, so it should leave when it is done.

Lace reports a broken rule that it can detect, such as a second thread with
the same id, a ``lace_worker_leave`` from inside a task or a ``lace_stop`` by a
worker, and exits.

Stacks and pinning
------------------

The threads run on their own stacks. Lace's own threads reserve 1 GB of stack
each, because a waiting worker can hold several paths of a deep task tree on
its stack (see :doc:`design`); threads that join Lace need large stacks for
deep task trees too.

Lace pins the threads only when the program asks for it, with
``lace_set_pinning``, ``lace_set_pin_cpus`` or the ``LACE_PIN`` environment
variable, before ``lace_start_external`` (see *Worker pinning* in
:doc:`building`). The CMake option ``LACE_DEFAULT_PIN`` does not apply: the
program may place its threads itself. A thread that leaves gets its affinity
back (on Linux also its timer slack, which Lace lowers for idle workers).

.. code-block:: c

   lace_set_pinning(LACE_PIN_CORE); // optional: let Lace pin the threads
   lace_start_external(n, 0);

Each worker allocates its task deque when it enters, so the memory is local to
where the thread runs.
