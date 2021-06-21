Lace [![CI testing](https://github.com/trolando/lace/actions/workflows/ci-build.yml/badge.svg)](https://github.com/trolando/lace/actions/workflows/ci-build.yml)
======
Lace is a work-stealing framework for multi-core fork-join parallelism.
The framework is written in C and implements work-stealing in a style similar to frameworks like Cilk and Wool.

The novelty of Lace is that it uses a **scalable** double-ended queue for its
implementation of work-stealing and that it offers a feature where workers
cooperatively interrupt their current tasks to execute a new task frame.
Furthermore, Lace threads can be suspended when the framework is not needed temporarily.

Lace was originally developed by the [Formal Methods and Tools](https://fmt.ewi.utwente.nl/)
group at the University of Twente as part of the MaDriD project, which
was funded by NWO, and by the [Formal Methods and Verification](http://fmv.jku.at/)
group at the Johannes Kepler University Linz as part of the RiSE project.
Currently, Lace is maintained by the main author who is again part of the [Formal Methods and Tools](https://fmt.ewi.utwente.nl/) group at the University of Twente.

Lace is licensed with the Apache 2.0 license.

The main author of Lace is Tom van Dijk who can be reached via <tom@tvandijk.nl>.
Please let us know if you use Lace in your projects and if you need
features that are currently not implemented in Lace.

The main repository of Lace is https://github.com/trolando/lace.

Dependencies
------------
Lace requires **CMake** for compiling.
Optionally use **hwloc** (`libhwloc-dev`) to pin workers and allocate memory on the correct CPUs/memory domains.

Ideally, Lace is used on a system that supports the `mmap` functionality to allocate a large amount of **virtual** memory. Typically this memory is not actually used, but we delegate actual allocation of memory to the OS. This is done automatically when memory is accessed, thus in most use cases Lace has a low memory overhead. If `mmap` is not available, `posix_memalign` will be used instead.
In that case, it is recommended to choose a more conservative size of the Lace worker deque, however be aware that Lace will halt when a deque is full.

Building
--------
It is recommended to build Lace in a separate build directory:
```bash
mkdir build
cd build
cmake ..
make && make test && make install
```

It is recommended to use `ccmake` to configure the build settings of Lace.

The build process creates `lace.h` in the `build` directory. This file together with `lace.c` form the library. Lace supports up to 6 parameters for a task.
If you need more parameters, use `lace14.h` and `lace14.c` which lets you define tasks with up to 14 parameters.
The reason for this distinction is the size of each task in memory, namely either 64 or 128 bytes.

Usage
-----
Start the Lace framework using the `lace_start(unsigned int n_workers, size_t dqsize)` and `lace_stop` methods. 
This starts Lace with the given number of workers, but if you call `lace_start` with 0 workers, then Lace automatically detects the maximum number of workers for your system.
This is only recommended if your application is the only application on the system.
For the `dqsize` parameter of `lace_start`, you can use 0 for the default size, which is currently 100000 tasks.

Lace workers will greedily wait for tasks to execute, increasing your CPU load to 100%.
Use `lace_suspend` and `lace_resume` from non-Lace threads to temporarily stop the work-stealing framework.
Calls to `lace_start`, `lace_suspend` and `lace_resume` do not incur much overhead.
Typically suspending and resuming requires 1-2 ms if you use the maximum number of workers and much less if you do not use the maximum number of workers.

Calls to Lace tasks block the calling thread until Lace is done.
If your application is single-threaded and spends most of its time running Lace tasks, it is recommended to use the maximum number of workers, obtained via `lace_get_pu_count()` or by setting the number of workers in `lace_start` to 0.
If your application is single-threaded but only occasionally uses Lace, you can still use the maximum number of workers, but use `lace_suspend` and `lace_resume` to suspend Lace while no Lace tasks are used.
If your application is multi-threaded and you do not use Lace for your other threads, then it is recommended to use fewer workers, since the Lace workers use 100% CPU time while trying to acquire work.

For examples, see the `benchmarks` directory.
In essence, you define Lace tasks using e.g. `TASK_1(int, fib, int, n) { method body }` to create a Lace task with an int return value and one parameter of type int and variable name n.
You can further separate declaration and implementation using `TASK_DECL_1(int, fib, int)` and `TASK_IMPL_1(int, fib, int, n)` macros.
To declare tasks with no return value, use the `VOID_TASK_n` and so forth macros.
If you are inside a Lace task, use `SPAWN` and `SYNC` to create subtasks and wait for their completion.
Use `CALL` to directly execute a subtask.
If you are not inside a Lace task, use `RUN` to run a task.
You can use `RUN` from any thread, including from Lace tasks.
The method halts until the task has been run by the work-stealing framework.
Example: `int result = RUN(fib, 40)` will run the Lace task `fib` with parameter `40`.

If you use C++, you can parallelize class methods via friend functions, but not directly.
Proper task scheduling for C++ classes is currently not implemented.
It is certainly technically possible, so if you are interested in working on this, please reach out.

Benchmarking
------------
Lace comes with a number of example multi-threaded programs, which can be used to test the performance of Lace.
After building Lace with `LACE_BUILD_BENCHMARKS` set to `ON`, you can enter the `benchmarks` subdirectory of the build directory, and run the Python script `bench.py`.
This script runs the benchmarks `fib 50`, `uts t2l`, `uts t3l`, `queens 15`, `matmul 4096` in random order.
Each benchmark is run with 1 worker, with the maximum number of workers, and sequentially.
Workloads such as `matmul` and `queens` are easy to load balance.
The `fib` workload has a very high number of nearly empty tasks and is therefore a stress test on the overhead of the framework, but is not very representative for real world workloads.
The `uts t3l` is a more challenging workload as it offers a unpredictable tree search.
See for further details the academic publications on Lace mentioned below.

Publications
------------
The following two academic publications are directly related to Lace.

T. van Dijk (2016) [Sylvan: Multi-core Decision Diagrams](http://dx.doi.org/10.3990/1.9789036541602). PhD Thesis.

T. van Dijk and J.C. van de Pol (2014) [Lace: Non-blocking Split Deque for Work-Stealing](http://dx.doi.org/10.1007/978-3-319-14313-2_18). In: Euro-Par 2014: Parallel Processing Workshops. LNCS 8806, Springer.
