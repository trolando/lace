Getting Started
===============

Prerequisites
-------------

Lace requires:

* A C11-capable compiler (GCC, Clang, or MSVC)
* CMake 3.15 or newer
* POSIX threads (pthreads) on Linux/macOS, or Windows threads on Windows

Optional:

* `hwloc <https://www.open-mpi.org/projects/hwloc/>`_ for NUMA-aware thread pinning

Quick example
-------------

.. code-block:: c

   #include <lace.h>
   #include <stdio.h>

   TASK_1(int, fibonacci, int, n)        // declare the task, create helper functions

   int fibonacci_CALL(lace_worker* lw, int n)
   {
       if (n < 2) return n;
       fibonacci_SPAWN(lw, n-1);         // fork: push task onto deque
       int a = fibonacci_CALL(lw, n-2);  // run another instance directly
       int b = fibonacci_SYNC(lw);       // join: retrieve result of spawned task
       return a + b;
   }

   int main(void)
   {
       int n_workers = 0;                // 0 workers = use all available cores
       int dqsize = 0;                   // use default task deque size
       int stacksize = 0;                // use default program stack size

       lace_start(n_workers, dqsize, stacksize);
       printf("fib(42) = %d\n", fibonacci(42));
       lace_stop();
   }

Building with CMake
-------------------

As a subdirectory:

.. code-block:: cmake

   add_subdirectory(external/lace)
   target_link_libraries(my_app PRIVATE lace::lace)

Or fetch automatically:

.. code-block:: cmake

   include(FetchContent)
   FetchContent_Declare(lace
       GIT_REPOSITORY https://github.com/trolando/lace.git
       GIT_TAG        v2.2.1
       GIT_SHALLOW    TRUE
   )
   FetchContent_MakeAvailable(lace)
   target_link_libraries(my_app PRIVATE lace::lace)
