Choosing a Variant
==================

Lace ships three variants that trade task struct size against available
parameter space:

.. list-table::
   :header-rows: 1
   :widths: 15 20 15 25

   * - Header
     - CMake target
     - Task size
     - Space for parameters + result
   * - ``lace32.h``
     - ``lace::lace32``
     - 32 bytes
     - 16 bytes
   * - ``lace.h``
     - ``lace::lace``
     - 64 bytes
     - 48 bytes
   * - ``lace128.h``
     - ``lace::lace128``
     - 128 bytes
     - 112 bytes

The 16-byte overhead is fixed (function pointer and thief status field). A
``static_assert`` in the generated code catches it at compile time if a task's
parameters and return type exceed the available space.

The standard ``lace.h`` variant supports up to 10 parameters. The ``lace128.h``
variant supports up to 14 parameters. The ``lace32.h`` variant is primarily
intended for architectures with 32-byte cache lines.
