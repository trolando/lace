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
       GIT_TAG        |tag|
       GIT_SHALLOW    TRUE
   )
   FetchContent_MakeAvailable(lace)
   target_link_libraries(my_app PRIVATE lace::lace)

**With find_package (after installing):**

.. code-block:: cmake

   find_package(lace |version| REQUIRED CONFIG)
   target_link_libraries(my_app PRIVATE lace::lace)

A robust pattern that handles all three cases:

.. code-block:: cmake

   if(NOT TARGET lace::lace)
       find_package(lace |version| CONFIG QUIET)
       if(NOT lace_FOUND)
           include(FetchContent)
           FetchContent_Declare(lace
               GIT_REPOSITORY https://github.com/trolando/lace.git
               GIT_TAG        |tag|
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
   * - ``LACE_DEFAULT_PIN``
     - The pinning mode (Linux, FreeBSD, Windows) when neither ``lace_set_pinning()`` nor the
       ``LACE_PIN`` environment variable chooses one: ``AUTO``, ``OFF``,
       ``CPU``, ``CORE``, ``L2``, ``L3``, ``NUMA`` or ``ALL``, within the
       affinity mask the program is given; see `Worker pinning`_.
     - AUTO
   * - ``LACE_USE_HWLOC``
     - Use the ``hwloc`` library to pin worker threads to CPU cores, instead
       of the built-in pinning (macOS does not let programs pin threads).
     - OFF
   * - ``LACE_BACKOFF``
     - Workers sleep with exponential backoff when no work is available,
       reducing CPU usage without affecting throughput.
     - ON
   * - ``LACE_NATIVE_OPT``
     - Optimise for the host CPU architecture (``-march=native``). Improves
       performance on the build machine but produces binaries that may not
       run on other CPUs.
     - ON as the top-level project, OFF as a subproject
   * - ``LACE_ENABLE_PIC``
     - Compile with position-independent code (``-fPIC``). Required when
       embedding Lace inside a shared library.
     - OFF
   * - ``BUILD_SHARED_LIBS``
     - Build shared libraries instead of static. Automatically enables
       ``LACE_ENABLE_PIC``.
     - OFF
   * - ``LACE_PIE_TIMES``
     - Record precise overhead times per worker (startup, steal overhead,
       idle search time).
     - OFF
   * - ``LACE_COUNT_TASKS``
     - Record the number of tasks executed per worker.
     - OFF
   * - ``LACE_COUNT_STEALS``
     - Record the number of steals and leaps per worker, and leap strays.
     - OFF
   * - ``LACE_COUNT_SPLITS``
     - Record the number of deque split-point adjustments per worker.
     - OFF
   * - ``LACE_HARDENING``
     - Keep the compiler's default stack protector and stack clash
       protection in Lace's own code. When OFF, the library and the
       benchmarks are built with ``-fno-stack-protector
       -fno-stack-clash-protection`` (``/GS-`` on MSVC).
     - OFF
   * - ``LACE_DONT_OVERCOMMIT``
     - Default worker stacks to the calling thread's stack limit, clamped
       to 16-64 MB, instead of reserving 1 GB of address space per worker.
       For systems with strict overcommit accounting
       (``vm.overcommit_memory=2``). An explicit ``stacksize`` passed to
       ``lace_start`` is used either way.
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

**Keep LACE_DEFAULT_PIN at AUTO.** Pinned workers stay on their core and keep
their caches warm; in benchmarks on Intel and AMD machines with one, two and
four sockets, unpinned workers were 2-13% slower. The default mode leaves cores
to other programs when Lace does not use all of them (see `Worker pinning`_).

**Use LACE_NATIVE_OPT for local benchmarking** but not for portable or
distributed builds, since ``-march=native`` produces binaries tied to the
build machine's CPU.

**Compare timings only between builds with the same statistics options.**
The ``LACE_PIE_TIMES``, ``LACE_COUNT_TASKS``, ``LACE_COUNT_STEALS``, and
``LACE_COUNT_SPLITS`` options cost about 1% or less in the benchmarks (see
:doc:`scheduling-experiments`), but they change the code layout, which can
move a single program by several percent.

**Hardening defaults cost time in task code.** Many distributions configure
GCC to add a stack protector and stack clash protection by default. These
guard against attacks through untrusted input, which a work-stealing
scheduler does not handle, so Lace turns both off in its own targets
(``LACE_HARDENING``). Your program keeps its own flags, also for the code
that the task macros put in your functions; to turn them off there too, add
for example:

.. code-block:: cmake

   target_compile_options(myprog PRIVATE -fno-stack-protector -fno-stack-clash-protection)

``-fcf-protection`` stays on: mixing objects with and without it drops the
CET marking of the whole program.

**Sanitiser options are mutually exclusive.** AddressSanitizer and
ThreadSanitizer cannot be combined. Use them individually during development.


Worker pinning
--------------

On Linux, FreeBSD and Windows, Lace pins its workers within the affinity mask
of the thread that calls ``lace_start()``, as ``taskset``, cgroups or a job
scheduler such as SLURM set it; on Windows, within the processor group of that
thread. Lace never pins to CPUs outside that mask. The mode says what
each worker may run on:

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Mode
     - Each worker may run on
   * - ``LACE_PIN_AUTO``
     - The default: ``LACE_PIN_CPU`` if the workers fill the cores of the
       mask (as with the default number of workers, or ``taskset`` with as
       many cores as workers), otherwise ``LACE_PIN_ALL``. A program that
       leaves cores free may share the machine with another, and two
       programs that pin worker *i* to the same CPU would share their cores.
   * - ``LACE_PIN_CPU``
     - One hardware thread, taking one of every physical core before any
       second hardware thread of a core.
   * - ``LACE_PIN_CORE``
     - One physical core (its hardware threads).
   * - ``LACE_PIN_L2``, ``LACE_PIN_L3``, ``LACE_PIN_NUMA``
     - The cores that share an L2 cache, an L3 cache (a chiplet, or a
       socket) or a NUMA node. The workers are spread over these groups in
       proportion to their number of cores.
   * - ``LACE_PIN_ALL``
     - Any physical core of the mask, one hardware thread per core.
   * - ``LACE_PIN_OFF``
     - Any CPU of the mask (no pinning).

With more workers than places (hardware threads for ``LACE_PIN_CPU``, cores
for the other modes), Lace does not pin.

A program chooses the mode with ``lace_set_pinning()``, or pins worker *i* to
``cpus[i % n]`` with ``lace_set_pin_cpus(cpus, n)``, before ``lace_start()``:

.. code-block:: c

   lace_set_pinning(LACE_PIN_L3);
   lace_start(0, 0, 0);

If the program does not choose, the environment variable ``LACE_PIN`` does:
``off``, ``auto``, ``cpu``, ``core``, ``l2``, ``l3``, ``numa``, ``all``, or a
CPU list such as ``list:0,2,4-7``. ``LACE_PIN_PRINT=1`` prints the CPUs of every
worker. On other systems these calls have no effect; with ``LACE_USE_HWLOC``,
hwloc pins every worker to a core unless the mode is ``off``.

With ``lace_start_external`` (:doc:`threads`), Lace pins the program's threads
only when the program chooses a mode or ``LACE_PIN`` is set; the default mode
is then ``off``, whatever ``LACE_DEFAULT_PIN`` says.


Installing
----------

.. code-block:: bash

   cmake -B build -DCMAKE_INSTALL_PREFIX=/usr/local
   cmake --build build
   cmake --install build

This installs libraries, headers, CMake config files, and pkg-config
``.pc`` files. After installation, other projects can use ``find_package(lace)``
or ``pkg-config --cflags --libs lace``.
