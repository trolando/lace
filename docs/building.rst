Building
========

Requirements
------------

* A C11-capable compiler (GCC, Clang, or MSVC)
* CMake 3.15 or newer
* POSIX threads (pthreads) on Linux/macOS, or Windows threads on Windows

Optional dependencies:

* `hwloc <https://www.open-mpi.org/projects/hwloc/>`_ — for NUMA-aware thread pinning


Building from source
--------------------

.. code-block:: bash

   cmake -B build
   cmake --build build

For a Release build (the default when Lace is the top-level project):

.. code-block:: bash

   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build


Using Lace in your project
--------------------------

**As a subdirectory or submodule:**

.. code-block:: cmake

   add_subdirectory(external/lace)
   target_link_libraries(my_app PRIVATE lace::lace)

**With FetchContent:**

.. code-block:: cmake

   include(FetchContent)
   FetchContent_Declare(lace
       GIT_REPOSITORY https://github.com/trolando/lace.git
       GIT_TAG        v2.2.2
       GIT_SHALLOW    TRUE
   )
   FetchContent_MakeAvailable(lace)
   target_link_libraries(my_app PRIVATE lace::lace)

**With find_package (after installing):**

.. code-block:: cmake

   find_package(lace 2.2.2 REQUIRED CONFIG)
   target_link_libraries(my_app PRIVATE lace::lace)

A robust pattern that handles all three cases:

.. code-block:: cmake

   if(NOT TARGET lace::lace)
       find_package(lace 2.2.2 CONFIG QUIET)
       if(NOT lace_FOUND)
           include(FetchContent)
           FetchContent_Declare(lace
               GIT_REPOSITORY https://github.com/trolando/lace.git
               GIT_TAG        v2.2.2
               GIT_SHALLOW    TRUE
           )
           FetchContent_MakeAvailable(lace)
       endif()
   endif()

This first checks if Lace is already a target (e.g. added as a submodule by
a parent project), then tries to find an installed version, and finally
fetches it from GitHub as a last resort.


CMake options
-------------

.. list-table::
   :header-rows: 1
   :widths: 28 62 10
   :class: cmake-opts-table

   * - Option
     - Description
     - Default
   * - ``LACE_USE_MMAP``
     - Use ``mmap`` (or ``VirtualAlloc`` on Windows) to allocate task deques
       instead of ``aligned_alloc``. Physical pages are lazily allocated by the
       OS, which reduces startup memory usage.
     - ON
   * - ``LACE_USE_HWLOC``
     - Use the ``hwloc`` library to pin worker threads to CPU cores.
       Important for NUMA systems where memory locality affects performance.
     - OFF
   * - ``LACE_BACKOFF``
     - Workers sleep with exponential backoff when no work is available,
       reducing CPU usage without affecting throughput.
     - ON
   * - ``LACE_NATIVE_OPT``
     - Optimise for the host CPU architecture (``-march=native``). Improves
       performance on the build machine but produces binaries that may not
       run on other CPUs.
     - ON
   * - ``LACE_ENABLE_PIC``
     - Compile with position-independent code (``-fPIC``). Required when
       embedding Lace inside a shared library.
     - OFF
   * - ``LACE_PIE_TIMES``
     - Record precise overhead times per worker (startup, steal overhead,
       idle search time).
     - OFF
   * - ``LACE_COUNT_TASKS``
     - Record the number of tasks executed per worker.
     - OFF
   * - ``LACE_COUNT_STEALS``
     - Record the number of successful steals per worker.
     - OFF
   * - ``LACE_COUNT_SPLITS``
     - Record the number of deque split-point adjustments per worker.
     - OFF

The following options are only available when Lace is the top-level project:

.. list-table::
   :header-rows: 1
   :widths: 28 62 10
   :class: cmake-opts-table

   * - Option
     - Description
     - Default
   * - ``LACE_BUILD_TESTS``
     - Build the test suite (disabled when used as a subproject)
     - ON
   * - ``LACE_BUILD_BENCHMARKS``
     - Build the benchmark programs (disabled when used as a subproject)
     - ON
   * - ``LACE_BUILD_DOCS``
     - Build the documentation (disabled when used as a subproject)
     - OFF
   * - ``LACE_SANITIZE_ADDRESS``
     - Build with AddressSanitizer to detect memory errors.
       For development and testing only.
     - OFF
   * - ``LACE_SANITIZE_THREAD``
     - Build with ThreadSanitizer to detect data races.
       For development and testing only.
     - OFF
   * - ``LACE_SANITIZE_UB``
     - Build with UndefinedBehaviorSanitizer to detect undefined behaviour.
       For development and testing only.
     - OFF


Configuration recommendations
------------------------------

**Keep LACE_BACKOFF on.** Benchmarks show that backoff does not affect
throughput, but it prevents idle workers from consuming 100% CPU when there
is no work. There is no reason to turn this off unless you are doing very
precise micro-benchmarking of steal overhead.

**Use LACE_USE_MMAP.** When enabled, deques are allocated as virtual memory.
Physical pages are committed lazily by the OS, so a large ``dqsize`` has no
upfront memory cost. This means you can be generous with the deque size
without worrying about wasting RAM.

**Use LACE_USE_HWLOC for NUMA systems.** On multi-socket machines, enabling
hwloc ensures that worker threads are pinned to cores and that memory is
allocated close to the core that uses it. On single-socket desktop machines
the benefit is smaller but still measurable for memory-intensive workloads.

**Use LACE_NATIVE_OPT for local benchmarking** but not for portable or
distributed builds, since ``-march=native`` produces binaries tied to the
build machine's CPU.

**Statistics options ruin timing measurements.** The ``LACE_PIE_TIMES``,
``LACE_COUNT_TASKS``, ``LACE_COUNT_STEALS``, and ``LACE_COUNT_SPLITS``
options add instrumentation overhead. Enable them for profiling and debugging,
but never for performance benchmarking.

**Sanitiser options are mutually exclusive.** AddressSanitizer and
ThreadSanitizer cannot be combined. Use them individually during development.


Installing
----------

.. code-block:: bash

   cmake -B build -DCMAKE_INSTALL_PREFIX=/usr/local
   cmake --build build
   cmake --install build

This installs static libraries, headers, CMake config files, and pkg-config
``.pc`` files. After installation, other projects can use ``find_package(lace)``
or ``pkg-config --cflags --libs lace``.
