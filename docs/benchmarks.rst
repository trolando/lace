Benchmarks
==========

Lace includes a set of benchmark programs in the ``benchmarks/`` directory.
These are primarily intended for evaluating Lace's work-stealing performance
and comparing it against other frameworks.

Many of these benchmarks originate from well-known benchmark suites, such
as the Cilk-5 benchmark suite. The implementations here are mostly adapted
from Nowa (Schmaus et al, IPDPS 2021). UTS is from the Unbalanced Tree Search
project (Olivier et al., LCPC 2006). The benchmarks cover a range of
parallelism patterns:

``fib``
   Recursive Fibonacci. Micro-benchmark for task creation overhead: tasks
   are near-empty, so performance is dominated by spawn/sync cost.

``uts``
   Unbalanced Tree Search. Highly irregular, unpredictable workload;
   a standard stress-test for work-stealing schedulers.

``nqueens``
   N-Queens solver. Spawns one task per candidate position at each row,
   producing a regular N-ary task tree. Moderate task granularity.

``pi``
   Numerical integration for computing pi. Embarrassingly parallel with
   uniform task sizes.

``integrate``
   Adaptive numerical integration. Recursion depth varies with the
   function being integrated.

``cilksort``
   Recursive merge-sort (from Cilk-5, MIT/Frigo). Uniform binary task
   tree with a cutoff for switching to sequential sort.

``quicksort``
   Parallel quicksort. Irregular partitioning due to pivot selection.

``dfs``
   Parallel depth-first search. Regular task tree structure.

``matmul``
   Recursive matrix multiplication. Splits along the largest dimension,
   with a 64-element base case.

``rectmul``
   Rectangular matrix multiplication (MIT/Prokop, 1997). Variant of
   matmul for non-square matrices where ``n < m`` and dimensions are
   multiples of 16.

``strassen``
   Strassen's fast matrix multiplication (MIT, 1996). Seven-way
   recursive split with coarser granularity than naïve matmul.

``lu``
   LU decomposition (MIT/Blumofe, 1996). Block-recursive Gaussian
   elimination with data dependencies between subproblems.

``cholesky``
   Sparse Cholesky factorization (MIT/Frigo, 2000). Uses small blocks at the
   leaves of a quad-tree decomposition.

``heat``
   Heat diffusion using Jacobi-type iteration (MIT/Strumpen, 1996).
   Cache-oblivious stencil computation with spatial and temporal tiling.

``fft``
   Fast Fourier Transform (MIT/Frigo, 2000). Cooley-Tukey divide-and-conquer
   with a binary recursive task tree.

``knapsack``
   0/1 knapsack via branch-and-bound (MIT/Frigo, 2000). Irregular pruning
   makes the task tree unpredictable.

Running benchmarks
------------------

Build Lace as the top-level project with benchmarks enabled (the default):

.. code-block:: bash

   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build

Then run individual benchmarks from the build directory:

.. code-block:: bash

   ./build/benchmarks/fib-lace -w 0 42        # fibonacci(42) using all cores
   ./build/benchmarks/nqueens-lace -w 0 14    # 14 queens
   ./build/benchmarks/uts-lace -w 0 -t 0 -b 2000 -q 0.200014 -m 5 -r 7  # UTS T3L

Most benchmarks accept a ``-w N`` flag to set the number of worker threads.
Run without arguments to see usage information.
In addition, there is a ``bench.py`` program in the ``build/benchmarks`` directory
that can be used to automatically run the benchmarks.

Profiling with statistics
-------------------------

To collect steal counts, task counts, split counts, and timing data, rebuild with the
appropriate options:

.. code-block:: bash

   cmake -B build -DLACE_COUNT_SPLITS=ON -DLACE_COUNT_STEALS=ON -DLACE_COUNT_TASKS=ON -DLACE_PIE_TIMES=ON
   cmake --build build

Then call ``lace_count_report`` at the end of your program.
Note that enabling statistics adds instrumentation overhead, so the reported wall-clock times will not
be representative of production performance.