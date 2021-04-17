Lace
======
Lace is a work-stealing framework for multi-core fork-join parallelism.
The framework is written in C and implements work-stealing in a style similar
to frameworks like Cilk and Wool.

The novelty of Lace is that it uses a novel scalable double-ended queue for its
implementation of work-stealing and that it offers a feature where workers
cooperatively interrupt their current tasks to execute a new task frame.

Lace is developed (&copy; 2011-2016) by the [Formal Methods and Tools](http://fmt.ewi.utwente.nl/)
group at the University of Twente as part of the MaDriD project, which
was funded by NWO, and (&copy; 2016-2017) by the [Formal Methods and Verification](http://fmv.jku.at/)
group at the Johannes Kepler University Linz as part of the RiSE project.
Lace is licensed with the Apache 2.0 license.

The main author of Lace is Tom van Dijk who can be reached via <tom@tvandijk.nl>.
Please let us know if you use Lace in your projects and if you need
features that are currently not implemented in Lace.

The main repository of Lace is https://github.com/trolando/lace.

Dependencies
------------
Lace requires **CMake** for compiling.
Optionally use **hwloc** (`libhwloc-dev`) to pin workers and allocate memory on the correct CPUs/memory domains.

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

The build process creates `lace.h` in the `build` directory. This file together with `lace.c` form the library. Lace supports up to 6 parameters for a task. If you need more, use `lace14.h` and `lace14.c` which lets you define tasks with up to 14 parameters.

Usage
-----
Start Lace workers using `lace_start` and `lace_stop` methods. 
This starts 0 (autodetect) or a given number of threads.
These threads will greedily wait for tasks to execute, increasing your CPU load to 100%. Use `lace_suspend` and `lace_resume` to temporarily stop the work-stealing framework.

See the `benchmarks` directory for examples of using Lace.
Essentially, define Lace tasks using e.g. `TASK_1(int, fib, int, n) { method body }` to create a Lace task with an int result value and one parameter of type int and variable name n.
If you are inside a Lace task, use `SPAWN` and `SYNC` to create subtasks and wait for their completion. Use `CALL` to directly execute a subtask.
If you are not inside a Lace task, use `RUN` to run a task. You can use `RUN` from any thread, including from Lace tasks. The method halts until the task has been run by the work-stealing framework. Example: `int result = RUN(fib, 40)` will run the Lace task `fib` with parameter `40`.

Publications
------------
T. van Dijk (2016) [Sylvan: Multi-core Decision Diagrams](http://dx.doi.org/10.3990/1.9789036541602). PhD Thesis.

T. van Dijk and J.C. van de Pol (2014) [Lace: Non-blocking Split Deque for Work-Stealing](http://dx.doi.org/10.1007/978-3-319-14313-2_18). In: Euro-Par 2014: Parallel Processing Workshops. LNCS 8806, Springer.
