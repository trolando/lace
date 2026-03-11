Lace Documentation
===================

Lace is a C framework for fine-grained fork-join parallelism on multi-core
computers. It uses work-stealing deques to distribute tasks across worker
threads, providing scalable parallelism with low overhead.

Features
--------

* ⚡ Low-overhead, lock-free work-stealing
* 💤 Sleeps when idle (exponential backoff) to save CPU time
* 📌 Optional thread pinning with ``hwloc``
* 📈 Low-overhead statistics per worker
* ⛔ Interrupt support for coordinating all workers to a safe point (e.g. stop-the-world GC)

Lace uses a **scalable** double-ended queue for work-stealing. The owner
thread pushes and pops tasks from its own deque in a **wait-free** manner.
Thief threads steal from the other end in a **lock-free** manner. The design
minimises cache line contention between workers.

Platform support
----------------

* 🐧 Linux (GCC, Clang)
* 🪟 Windows (MSVC, MSYS2/MinGW)
* 🍎 macOS (Apple Clang)

.. toctree::
   :maxdepth: 2
   :caption: User Guide

   getting-started
   building
   variants
   tasks
   benchmarks
   migrating

.. toctree::
   :maxdepth: 2
   :caption: API Reference

   api/lifecycle
   api/workers
   api/operations
   api/stats
   api/misc


Publications
------------

If you use Lace in your research, please cite the following publications:

* T. van Dijk and J.C. van de Pol (2014).
  **Lace: Non-blocking Split Deque for Work-Stealing.**
  In: *Euro-Par 2014: Parallel Processing Workshops.* LNCS 8806, Springer.
  `doi:10.1007/978-3-319-14313-2_18 <https://doi.org/10.1007/978-3-319-14313-2_18>`_

* T. van Dijk (2016).
  **Sylvan: Multi-core Decision Diagrams.**
  PhD Thesis, University of Twente.
  `doi:10.3990/1.9789036541602 <https://doi.org/10.3990/1.9789036541602>`_


License
-------

Lace is licensed under the `Apache License, Version 2.0 <https://opensource.org/licenses/Apache-2.0>`_.