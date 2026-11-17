Scheduling Experiments (2026)
=============================

This page reports the experiments behind the leapfrogging, idle and pinning
policies of Lace, and the reasons for each choice. Lace was compared with
OpenCilk 3.0 and with the scheduler of ParlayLib on several machines, and
every mechanism had to show a measured benefit to stay in.


Goals and constraints
---------------------

* **Fast:** at least as fast as OpenCilk and Parlay on fork-join programs.
* **Quiet when idle:** an idle Lace uses almost no CPU, so that a program
  that uses Lace now and then does not pay for it in between.
* **Responsive when needed:** a parallel region that starts after an idle
  period starts quickly.
* **Do no harm:** Lace is used from other programs and languages (Sylvan,
  bindings for Rust, Java and Python), and must not slow them down: no steal
  storms, no cores taken from other programs, nothing that breaks under
  interference.
* **The calling thread is not a worker.** This is by design: it keeps the API
  simple and makes Lace easy to call from other languages. OpenCilk and
  Parlay make the calling thread a worker; Lace instead makes the handoff to a
  worker fast.
* **Keep it simple.** A mechanism stays only if it shows a clear benefit on
  some platform and some measure (throughput, idle CPU, latency, storms,
  interference); a constant is preferred over an adaptive mechanism that
  gives nothing on the hardware tested.


Method
------

Platforms
~~~~~~~~~

All runs were on Linux, on shared nodes of a SLURM cluster (except on the
desktop processor), with 36 hardware threads (18 cores) per job and 16
workers unless stated otherwise.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Machine
     - Processor
   * - Intel, 2 sockets
     - 2x Xeon Silver 4314 (Ice Lake, 16 cores each). CPUs are numbered
       alternately over the sockets.
   * - AMD, chiplets
     - EPYC 9354P (Zen 4, 32 cores in 8 chiplets of 4 cores, each with its
       own L3).
   * - Intel, 4 sockets
     - 4x Xeon E7-8890 v4 (Broadwell, 24 cores each). Allocations spread over
       up to all four sockets; the noisiest machine.
   * - AMD, chiplets (older)
     - EPYC 7742 (Zen 2, 64 cores in 16 groups of 4 with their own L3); scaling
       to 64 cores and some PBBS runs.
   * - AMD desktop
     - Ryzen 9 7950X3D (16 cores in two chiplets, one with 96 MB of 3D
       V-Cache, up to 5.76 GHz, deepest idle state with a 350 µs exit
       latency), exclusive. Compiled with the system clang 14, since the
       OpenCilk 3.0 release needs a newer C library than this machine has.

Systems and benchmarks
~~~~~~~~~~~~~~~~~~~~~~

Every system was compiled with the same compiler (the clang 19.1.7 of
OpenCilk 3.0), so that only the scheduler differs. The references are
OpenCilk 3.0, the Parlay scheduler, and Lace v2.4.0.

* **The Lace benchmark suite** (18 programs: fib, uts T1L/T2L/T3L, nqueens,
  dfs, matmul, cholesky, integrate, heat, cilksort, fft, knapsack, lu, pi,
  quicksort, rectmul, strassen; uts T3XXL in some runs), and OpenCilk ports
  of the same programs. Nine of them print a checksum of their result, which
  agreed between all variants and worker counts. uts T3L is a stress test,
  not a target: its binomial tree gives every unexplored node the same
  expected work, so that stealing the oldest task gets no more than any
  other, as its authors intended ("It is indeed our goal to construct a
  benchmark that in its most challenging form challenges all load balancing
  strategies", Prins, UNC 2003).
* **PBBS v2** (up to 67 inputs of 21 benchmarks) through the Parlay
  scheduler plugin, against Parlay's own scheduler and OpenCilk.
* **Sylvan** (BDD and LDD operations on BEEM models), the main real user of
  Lace, as the do-no-harm test.
* **Micro benchmarks** for the idle and external-call behaviour: short calls
  from outside Lace, a tiny region 200 µs and 2 ms after the previous one, a
  region of 1000 tasks of 1 µs after a 5 ms sleep, regions of 10000 and 100000
  tasks of 100 ns, steal storms (one or four chains of steps that fork two
  empty or 1 µs tasks, so that at most two or eight workers have work), and
  the CPU an idle Lace uses in 2 s after work.
* **Interference:** busy-loop processes on 4 of the 16 cores of a run, and two
  copies of a benchmark at the same time on twice the cores.

Ablation
~~~~~~~~

The candidate design was tested by leave-one-out (remove one mechanism) and
add-one (add an alternative or change a value), in rounds, until no single
change made it better.

* **One binary for all configurations.** Code layout alone moves results by
  3-6% between builds, more than most single mechanisms. An ablation build
  (``LACE_TUNABLE``) compiles in every mechanism and reads its setting from
  the environment, so all configurations run the same code. Normal builds
  were checked to have the same machine code as without these switches, and
  the final code, from which the switches are removed, the same machine code
  as the experiment code with the final settings (bar one redundant check).
* **The same environment size for every configuration.** The size of the
  environment moves the initial stack, which can change results by itself
  (Mytkowicz et al., ASPLOS 2009). Every run pads its environment to the
  same size, with a different size per round.
* Configurations run interleaved, in a shuffled or rotating order, 3-5 rounds;
  results are geomeans over benchmarks of the median time per configuration.
* **The statistics options cost nothing measurable.** Builds with
  ``LACE_PIE_TIMES``, ``LACE_COUNT_TASKS``, ``LACE_COUNT_STEALS`` or
  ``LACE_COUNT_SPLITS``, alone or all together, ran the suite within 0.6% of
  the plain build at 16 workers, and fib, knapsack and uts T2L within 0.6% at
  one worker (``LACE_COUNT_TASKS``: knapsack 1.4%), and seven PBBS benchmarks
  within 0.3%. Only nqueens moved more, 5-10%, also between builds that differ
  only in an unrelated counter: code layout. PIE times can therefore explain
  a gap without causing one.

Pitfalls that cost time and are worth knowing:

* Runs under ``perf stat`` with many multiplexed events made Lace look 4-9%
  slower than OpenCilk; without perf the difference was 0-3%. Lace does tens
  of thousands of context switches, which perf makes expensive.
* Comparing separately compiled variants confused code layout with policy:
  an 11% "pinning gain" on PBBS came mostly from two different builds, and
  in a storm of empty tasks on Intel the final build took 12 ns per step,
  the same code in the ablation binary 16.
* Shared nodes have transient anomalies (once, unpinned fib ran 3-4 times
  slower in two rounds, never again in 240 runs), and heat is bimodal on some
  allocations (1.8 or 2.5 s, whatever the configuration).


Leapfrogging
------------

A worker that waits at a join for a stolen task *leapfrogs*: it steals from
the thief, since that work is part of the awaited task.

**Random leaps removed.** Since 2014, a leapfrogging worker also stole a
random task after 32 failed attempts. That takes the victim's oldest and
largest task, and the join stays blocked under it until that task is done; in
phased algorithms (cholesky, matmul, lu) the next phase waits behind that
join. Random steals also beget steals and move data between caches and
sockets (cholesky: 349k instead of 60k steals, 7x the remote cache traffic).

.. list-table:: Random leaps back on, time relative to the final design (16 workers)
   :header-rows: 1

   * - Machine
     - Suite geomean
     - cholesky
     - lu
     - matmul
   * - Intel, 2 sockets
     - 1.108
     - 2.29
     - 1.23
     - 1.42
   * - AMD, chiplets
     - 1.082
     - 2.04
     - 1.32
     - 1.20
   * - Intel, 4 sockets
     - 1.087
     - 2.56
     - 1.23
     - 1.18

Random leaps only while no worker is idle did not help (1.075-1.098).

**Transitive leaps.** Without random leaps, uts T3L (a tree with long
sequential chains) waited at joins 18% of the time. With transitive leaps, a
waiting worker whose thief has nothing to steal steals from the worker that
the thief waits for, down the chain. Each worker publishes whom it waits for
in a cache line that a steal attempt reads anyway, and only after a first
failed attempt (most waits end before that). Without transitive leaps uts T3L
takes 1.22-1.26 times as long on all three machines. How far the chain is
followed (1, 4 or 16 hops) and whether the target is published at once made
no difference; the design uses 4 hops and publishes lazily.


Steal storms
------------

When there are many idle workers and only small tasks, idle workers keep
succeeding at stealing worthless tasks, so they never back off, and every
stolen task turns the owner's join into a remote one. Lace v2.0.3 (before the
backoff) took 1296 ns per step for a chain of empty tasks at 16 workers, v2.4.0
72 ns, and the code before this work still kept all 16 cores busy.

**A steal of a tiny task counts as a failed steal** for the backoff: thieves
then back off and leave the work to its owner. The thief times only the stolen
task, not the steal.

.. list-table:: Storms at 16 workers: ns per step and cores busy
   :header-rows: 1

   * - Storm
     - Lace, no rule
     - Lace
     - OpenCilk
     - Parlay
   * - Empty tasks, AMD
     - 26.0 on 13.5
     - 7.6 on 2.9
     - 17.8 on 3.9
     - 451 on 9.4
   * - Empty tasks, Intel 4 sockets
     - 58.5 on 13.0
     - 12.4 on 1.8
     - 57.5 on 3.9
     - 235 on 14.5
   * - 1 µs tasks, AMD
     - 2405 on 15.0 (500 ns)
     - 2097 on 1.6
     - 2722 on 16
     - 1904 on 9.0

**The threshold adapts to the machine.** A stolen task counts as tiny when it
ran shorter than 16 times the thief's running average of its steal cost
(from the attempt until it holds the task). A fixed 500 ns let tasks of 1 µs
be stolen, which bought nothing on AMD and on 4 sockets and 15% on Intel at
nine times the CPU; with the factor 16 the threshold rose above 1 µs on all
three server machines, as a fixed 2000 ns did. On the desktop processor,
where steals are cheap, it stayed lower, and stealing the 1 µs tasks paid:
that storm ran in 1307 instead of 2060 ns per step (on 7.4 instead of 1.1
cores). A higher threshold costs 4-10% on regions of 10000 tasks of 100 ns,
and nothing in the suite.


Sleeping and waking
-------------------

**Sentinels.** While some worker runs a task, two idle workers stay awake as
*sentinels*: they look at the other workers' deques in rounds, with 20 µs
futex naps in between, and wake the others when they see work. A sentinel
never steals itself: when it sees work it gives up its slot and steals as a
normal thief, so that a sentinel always watches. Sentinels also take calls
from outside Lace (``lace_run_task``) and end their nap at once when one
arrives. A sentinel retires when no worker has run a task for the grace
period.

Because a sentinel watches, sleeping workers can back off exponentially to
100 ms instead of waking every millisecond. Without sentinels, idle Lace used
8-35 times the CPU (0.21 instead of 0.025 CPU-s per 2 s on AMD, 0.43 instead
of 0.025 on Intel, 0.52 instead of 0.015 on 4 sockets). On Intel sentinels
also make regions after short idle periods faster: without them a tiny region
after a 2 ms sleep took 20.7 instead of 9.5 µs, and a region of 1000 tasks 206
instead of 173 µs.

* Two sentinels and a 200 µs spin before a sentinel's first nap: with one
  sentinel and no spin, a tiny region 200 µs after the previous one took
  5.9 instead of 2.1 µs on Intel and 3.1 instead of 1.4-1.9 µs on the desktop
  processor, and larger regions 2-7% longer on Intel; on AMD and on 4
  sockets there was no difference.
* Whether a sentinel asks a worker without shared tasks to share some made no
  difference. The 20 µs naps and the 1 µs timer slack that keeps them short
  matter on Intel: with 100 µs naps the tiny region after a 2 ms sleep took
  16.7 instead of 9.5 µs, and without the slack 14.8 µs, with regions of 1000
  tasks 24% slower. On AMD they made no clear difference.
* On AMD, the suite ran 1.6-1.8% faster without sentinels (fft, lu, quicksort,
  strassen 5-9%), on Intel and on 4 sockets not. One sentinel, longer naps,
  not asking for sharing, the default timer slack, dropping the wake-up while
  no sentinel watches, and capping a sleeper's timeout at 1 ms while some
  worker is busy all left it in place. The cause is not known; the design
  accepts the cost for the idle CPU, and on Intel the latency, that sentinels
  buy.

**Shallow sleep.** For 20 ms after it ran a task, a worker sleeps in naps of
100 µs, so its core stays in a shallow idle state and the next region starts
fast. Against 5 ms, a region of 1000 tasks of 1 µs 5 ms after the previous
one takes 168 instead of 227 µs (AMD), at less CPU per region, while an idle
Lace uses 0.039 instead of 0.028 CPU-s per 2 s after work. The responsiveness
was judged worth that.

**Sentinel grace.** A sentinel retires after 10 ms without work. 50 ms made
tiny regions after short gaps faster while the helper wake-up still delayed
them (3.0 instead of 5.1 µs on AMD), but with the helper woken by the caller
(below) they are as fast with 10 ms, so the shorter grace stays.

**Wake-ups.**

* A steal that leaves work at the victim wakes as many sleepers as the victim
  has tasks left (one futex call), instead of a chain of single wake-ups:
  regions of 1000-10000 tasks after a sleep start 10-14% faster.
* A steal while no sentinel watches wakes a sleeper too (a region after a long
  idle, when nobody watches the work that the stolen root task spawns).
  Without it the AMD suite was 0.9% slower (fft, quicksort 7-10%).
* The wake-up when a steal leaves work behind is needed besides the one when
  no sentinel watches: without it, regions of 1000 and 10000 tasks after a
  sleep took 6-9% longer on AMD.
* A woken worker used to steal for 16 rounds before it slept again, first
  from the worker its waker named. With the sentinels and the wake-ups above,
  neither made a difference (one binary for all four combinations, on Intel
  and AMD; without the rounds, regions of 1000 tasks were 4-6% faster), so
  both were dropped.


Calls from outside Lace
-----------------------

The calling thread is not a worker: ``lace_run_task`` hands the task to a
worker and waits.

* **The caller spins 5 µs, then yields now and then, and sleeps after 20 µs.**
  Yielding helps when the caller shares a CPU with the worker that runs its
  task; yielding from the start cost short calls 20-28%.
* **A helper wake-up.** The task may spawn work while all other workers
  sleep, so a second worker is woken. Without it lu and fft were up to 14%
  slower. But when the worker that took the task made that futex call, every
  tiny region took 2 µs longer (4.5 instead of 2.5 µs). In the design the
  waiting caller, which has nothing else to do, wakes the helper once it sees
  its task taken (or the worker does if the caller sleeps already), and a task
  that is done before the caller looks wakes nobody. This kept the
  throughput (without a helper the suite was 0.4-1.3% slower, lu up to 11%)
  and made tiny regions 200 µs after the previous one take 2.1-2.9 µs on the
  server machines and 1.4-1.9 µs on the desktop processor, as fast as
  without a helper.
* Only the worker that ran a call stops backing off; the others keep backing
  off, so that a stream of calls does not keep every idle worker spinning.

Short calls take 2.8 µs on AMD, against 4.8 for OpenCilk and 6.1 for Parlay;
3.1 µs on Intel, against 3.4 and 3.9 (and 2.6 for v2.4.0, whose workers never
stop spinning); and 2.0 µs on the desktop processor (Parlay 3.3). A tiny
region after a 2 ms sleep takes 12.6 µs on AMD, against 2.3 for OpenCilk and
8.2 for Parlay, and 8.9 µs on Intel, against 5.0 and 4.5: the remaining cost
of the handoff to a sleeping worker, which the design accepts. On the desktop
processor it takes 3.8 µs, as with Parlay (3.6).


Pinning
-------

Lace now pins its workers by default, within the affinity mask of the thread
that calls ``lace_start`` (see :doc:`building` for the modes and the API).

* **Unpinned costs 2-13%** on every machine: up to 10% in the suite on 4
  sockets (cholesky 1.88), 13% on PBBS on Intel when the mask includes the
  second hardware thread of each core (an unpinned worker or sentinel lands
  next to a busy worker). On AMD PBBS pinning made no difference.
* **One CPU per worker is fastest when the workers fill the cores** of the
  mask, on every machine. Workers that may move lose their caches after a
  wake-up, and two of them sometimes share a core while another is idle.
  Groups (one L3 cache, one NUMA node) were 1-4% slower than one CPU per
  worker on Intel, where an L3 group is a socket, and as fast on AMD, where it
  is a 4-core chiplet.
* **One CPU per worker under interference.** With busy loops on 4 of 16 cores,
  one CPU per worker slowed down least (1.18 at 16 workers and 1.46 at 8,
  against 1.22 and 1.63 for v2.4.0): the workers on free cores keep running and
  take the work of those that share a core.
* **But not with spare cores.** Two programs that each pin worker *i* to the
  *i*-th core pile onto the same cores: two copies took 1.95 times as long as
  one, against 1.14 for v2.4.0. With fewer workers than cores, the default
  therefore lets every worker run on any core of the mask (1.14).

The default (``LACE_PIN_AUTO``) is the best or within 1.4% of the best mode in
every test.


Do no harm: Sylvan
------------------

Sylvan's BDD and LDD operations (9 benchmarks, 3 rounds) gave the same results
with every version and worker count.

.. list-table:: Sylvan with the final design, time relative to v2.4.0
   :header-rows: 1

   * - Machine
     - 1 worker
     - 4 workers
     - 8 workers
     - 16 workers
   * - Intel, 2 sockets
     - 0.961
     -
     - 0.850
     - 0.847
   * - AMD desktop
     - 0.972
     - 0.938
     - 0.896
     - 0.903

On Intel every benchmark gained at 16 workers, from 2% (lddmc anderson.8) to
35% (bddmc lifts.7), and the CPU time fell as much as the time. On the desktop
processor the collision models gained most (0.72-0.73 at 8 workers); two
models took 4-5% longer at 16 workers. Most models ran slower at 16 workers
than at 8 there, with both versions: Sylvan's shared tables span both
chiplets. Most of the gain comes from dropping random leaps (20-36% on the
largest models with an early version of this design).


Comparison with OpenCilk and Parlay
-----------------------------------

**Suite** (16 workers, 5 rounds, time relative to the final code; geomean
over 18 programs for v2.4.0 and 17 for OpenCilk):

.. list-table::
   :header-rows: 1

   * - Machine
     - Lace v2.4.0
     - OpenCilk
   * - Intel, 2 sockets
     - 1.116
     - 1.339
   * - AMD, chiplets
     - 1.107
     - 1.312
   * - Intel, 4 sockets
     - 1.206
     - 1.493
   * - AMD desktop
     - 1.071 (ablation build)
     - (not available)

At 8 workers v2.4.0 took 1.04-1.06 times as long; the gap grows with the
number of workers (see Scaling below). OpenCilk's large numbers come from
the fine-grained programs (fib, knapsack, dfs 2.7-3.9 times as long), where
its spawns cost more. On the coarse-grained ones the two are close: OpenCilk
is up to 13% faster on some (fft and quicksort on AMD), Lace up to 26% on
others (cholesky on 4 sockets).

OpenCilk's release runtime crashes on uts T3L: it caps fiber stacks at 8 MB,
and more than 1024 nested spawns write past its deque into the heap. With a
patched runtime (1 GiB stacks, a deque of 99999 frames) it runs, and took
1.02-1.06 times as long as Lace at 8 and 16 workers on AMD and Intel. The
patched runtime is as fast as the release on the other programs (Intel suite
within 2%).

**PBBS** (16 workers, 66-67 inputs of 21 benchmarks, time relative to
Parlay):

.. list-table::
   :header-rows: 1

   * - Machine
     - Lace
     - OpenCilk
   * - Intel, 2 sockets (final code)
     - 0.966
     - 0.978
   * - AMD, chiplets (final code)
     - 0.963
     - 0.917
   * - AMD, chiplets (older; an earlier round)
     - 0.958
     - 0.956

At 8 workers on AMD Lace took 0.996 and OpenCilk 0.957. The two machines
differ:

* **On Intel** Lace was faster than OpenCilk on 13 of the 21 benchmarks at 16
  workers (0.995 of its time; an earlier round with the previous helper rule,
  18 of 21). At 8 workers one run gave Lace 1.042 of Parlay's time; a second
  run did not reproduce that (Lace 0.980, OpenCilk 0.961), and pinning one
  CPU per worker (0.989) or per socket (0.984) instead of leaving the 8
  workers unpinned on the 18 cores made no difference. OpenCilk was faster
  than Lace on 12 of the 21 benchmarks at 8 workers (Lace 1.035 of its
  time); Lace's largest gaps to Parlay on Intel are delaunayRefine and
  delaunayTriangulation (1.08-1.21) and rangeQuery2d.
* **On AMD** OpenCilk was faster than Lace on 18 of the 21 benchmarks (Lace
  1.054 of its time, 1.041 without maximalMatching, where OpenCilk took 0.71
  of Parlay's time). Lace's lead over Parlay there comes from three
  benchmarks where Parlay's scheduler does poorly (classify, convexHull,
  rayCast: Lace and OpenCilk 0.75-0.88 of Parlay's time); without them Lace
  and Parlay are equal (0.993).

OpenCilk crashes on two BFS inputs in some runs.

A diagnosis on AMD looked at the seven benchmarks where Lace or OpenCilk
stood out (2 rounds; the geomeans below are over these seven, not the whole
set). It separated three causes:

* **Not pinning:** Lace unpinned, pinned per CPU, and on one hardware thread
  per core was within 1%.
* **OpenCilk's lead on maximalMatching (0.76 of Parlay's time) comes from
  ``cilk_for``, not from its scheduler:** ParlayLib's OpenCilk backend turns
  a loop into a ``cilk_for``, while Parlay and the Lace plugin pick a grain
  with a sequential prefix and split in halves. With ParlayLib's OpenCilk
  backend changed to split loops as Parlay does, OpenCilk took 1.02 of
  Parlay's time there (Lace 1.00), and the difference sits in the phase whose
  loops become a ``cilk_for``. Picking the grain as ``cilk_for`` does (at
  most 2048 iterations or 1/8P of the range) did not help Lace (1.054
  against 1.040 on the seven), so it is the compiled loop, which a library
  cannot offer. Hardware counters show no locality advantage: OpenCilk had
  as many fills from other chiplets as Parlay (1.06; on maximalMatching
  1.17) and Lace 0.96, and on maximalMatching it ran the same instructions
  in 0.90 of the cycles. (Parlay's scheduler did four times as many
  cross-chiplet fills on classify as Lace and OpenCilk, which is why it is
  slow there.)
* **Lace's own gap to Parlay on these benchmarks (1.04 on the seven, 4-9% on
  histogram, removeDuplicates and the graph benchmarks) comes from waiting at
  joins.** With random leaps back on it shrank to 1.014 (removeDuplicates
  1.066 to 1.010, maximalMatching 1.025 to 0.985): Parlay's scheduler, too,
  runs any task while it waits. Random leaps cost the suite 8-11% and Sylvan
  up to 36%, and random leaps only while no worker is idle gave the same 1.015
  here and did not help the suite; by the time a waiting worker knows that a
  random leap would have paid, the chance is gone. The design keeps
  leapfrogging only and accepts this gap. The PIE times show where it goes:
  without random leaps, workers spend 2-4% of their time searching for work at
  a join on these benchmarks (1% on sampleSort); with random leaps 0.5-2%, the
  work done while waiting rises from 8% to 30-45% of their time, and they make
  3-4 times fewer leap attempts.
* rangeQuery2d (1.07-1.09) did not change with random leaps; its build phase
  takes twice as long as with Parlay or OpenCilk, for a reason not yet known.

**Idle and latency** (16 workers, final code, 3 rounds; the idle CPU is
measured after a loop of Parlay calls):

.. list-table:: AMD EPYC 9354P
   :header-rows: 1

   * - Measure
     - Lace
     - Lace v2.4.0
     - OpenCilk
     - Parlay
   * - Short call from outside
     - 2.8 µs
     - 3.0 µs
     - 4.8 µs
     - 6.1 µs
   * - Tiny region 200 µs after the last
     - 2.8 µs
     - 4.1 µs
     - 9.6 µs
     - 5.3 µs
   * - Tiny region after a 2 ms sleep
     - 12.6 µs
     - 11.0 µs
     - 2.3 µs
     - 8.2 µs
   * - 1000 x 1 µs after a 5 ms sleep
     - 163 µs
     - 1053 µs
     - 258 µs
     - 103 µs
   * - Storm of empty tasks
     - 7.8 ns/step, 2.6 cores
     - 7.9 ns/step, 2.3 cores
     - 18.7 ns/step, 3.5 cores
     - 657 ns/step, 10.1 cores
   * - Storm of 1 µs tasks
     - 2092 ns/step, 1.7 cores
     - 2200 ns/step, 16 cores
     - 2735 ns/step, 16 cores
     - 1947 ns/step, 9.0 cores
   * - CPU in 2 s idle after work
     - 0.037 CPU-s
     - 0.224 CPU-s
     - 0.002 CPU-s
     - 0.054 CPU-s

.. list-table:: Intel Xeon Silver 4314, 2 sockets
   :header-rows: 1

   * - Measure
     - Lace
     - Lace v2.4.0
     - OpenCilk
     - Parlay
   * - Short call from outside
     - 3.1 µs
     - 2.6 µs
     - 3.4 µs
     - 3.9 µs
   * - Tiny region 200 µs after the last
     - 2.2 µs
     - 5.5 µs
     - 5.1 µs
     - 3.1 µs
   * - Tiny region after a 2 ms sleep
     - 8.9 µs
     - 20.5 µs
     - 5.0 µs
     - 4.5 µs
   * - 1000 x 1 µs after a 5 ms sleep
     - 173 µs
     - 1093 µs
     - 203 µs
     - 89 µs
   * - Storm of empty tasks
     - 11.3 ns/step, 2.3 cores
     - 46.1 ns/step, 14.7 cores
     - 25.8 ns/step, 3.2 cores
     - 386 ns/step, 10.8 cores
   * - Storm of 1 µs tasks
     - 2107 ns/step, 1.7 cores
     - 1670 ns/step, 16 cores
     - 2337 ns/step, 16 cores
     - 1568 ns/step, 12.1 cores
   * - CPU in 2 s idle after work
     - 0.047 CPU-s
     - 0.453 CPU-s
     - 0.013 CPU-s
     - 0.103 CPU-s

On the 4-socket machine the picture is the same (short calls 6.1 µs against
14.2 for v2.4.0, a region of 1000 tasks 178 against 1150 µs, idle 0.040
against 0.479 CPU-s), except that Parlay's short calls (3.8 µs) are faster
than Lace's there. OpenCilk idles at the lowest CPU, but it has no storm
rule: in the storm of 1 µs tasks it keeps all 16 cores busy, as v2.4.0 does.
On Intel, v2.4.0 and Parlay take 21-26% less time than Lace in that storm, on
7-9 times the CPU (see Steal storms).

.. list-table:: AMD Ryzen 9 7950X3D (desktop)
   :header-rows: 1

   * - Measure
     - Lace
     - Lace v2.4.0
     - Parlay
   * - Short call from outside
     - 2.0 µs
     - 2.2 µs
     - 3.3 µs
   * - Tiny region 200 µs after the last
     - 1.9 µs
     - 7.7 µs
     - 3.4 µs
   * - Tiny region after a 2 ms sleep
     - 3.8 µs
     - 6.9 µs
     - 3.6 µs
   * - 1000 x 1 µs after a 5 ms sleep
     - 113 µs
     - 1033 µs
     - 94 µs
   * - 100000 x 100 ns
     - 876 µs
     - 4184 µs
     - 965 µs
   * - Storm of empty tasks
     - 5.7 ns/step, 1.4 cores
     - 6.1 ns/step, 2.2 cores
     - 214 ns/step, 9.2 cores
   * - CPU in 2 s idle after work
     - 0.018 CPU-s
     - 0.092 CPU-s
     - 0.032 CPU-s

On the desktop processor, whose deepest idle state takes 350 µs to leave,
v2.4.0 took ten times as long for a region of 1000 tasks after a short pause;
the sentinels and the shallow sleep remove that.


Scaling
-------

Speedup over the serial elision of each program (the program without its
spawns), geomean over 17 programs (the elision of dfs is optimized away), on
36 cores of the 4-socket machine (a shared node), 3 rounds. OpenCilk ran with
the patched runtime, so that uts T3L runs too.

.. list-table:: Speedup, 4 sockets
   :header-rows: 1

   * - Workers
     - Lace
     - Lace v2.4.0
     - OpenCilk
   * - 8
     - 5.7
     - 5.4
     - 4.2
   * - 16
     - 9.7
     - 8.5
     - 7.1
   * - 24
     - 13.7
     - 10.6
     - 9.7
   * - 32
     - 16.9
     - 11.3
     - 11.8

v2.4.0 falls behind as workers are added: it takes 1.05 times as long as the
final design at 8 workers and 1.50 times at 32. The gap is in the programs
with blocked joins, where random leaps hurt: at 32 workers cholesky reaches a
speedup of 17.2 against 5.2 with v2.4.0, matmul 25.2 against 11.4, fft 19.5
against 9.9, lu 23.2 against 14.7, and uts T3L 19.6 against 12.0. OpenCilk
scales as well as Lace on the coarse-grained programs (cilksort, lu, rectmul,
strassen, uts; slightly better on uts T3L), and its spawn cost limits it on
the fine-grained ones (fib 2.9, knapsack 5.5, pi 4.0 at 32 workers). heat and
quicksort stop at 6-8 with all three.

.. list-table:: Speedup, AMD chiplets (30 of the 32 cores of a node)
   :header-rows: 1

   * - Workers
     - Lace
     - Lace v2.4.0
     - OpenCilk
   * - 8
     - 5.8
     - 5.6
     - 4.8
   * - 16
     - 10.5
     - 9.8
     - 8.6
   * - 24
     - 14.3
     - 12.8
     - 11.8
   * - 30
     - 16.7
     - 14.8
     - 13.4

On AMD the gap to v2.4.0 grows too, but less: 1.03 of the time at 8 workers,
1.13 at 30 (cholesky 15.4 against 5.8, matmul 29.6 against 21.4, lu 23.5
against 15.3). OpenCilk scales like Lace on the coarse-grained programs (fft
18.5 against 16.7).

**Which change gives the gain.** On the same machine at 27 workers (27
cores), time of the final design relative to each variant:

.. list-table::
   :header-rows: 1

   * - Variant
     - 16 workers
     - 27 workers
   * - Lace v2.4.0
     - 0.900
     - 0.844
   * - Random leaps back on
     - 0.922
     - 0.898
   * - Without transitive leaps
     - 0.983
     - 0.976
   * - Unpinned
     - 0.989
     - 0.951

Dropping random leaps gives most of it, more at more workers (cholesky 0.47,
lu 0.65, matmul 0.74 of the time with them at 27 workers); pinning adds 5% at
27 workers against 1% at 16 (fib, pi, nqueens, cilksort 7-10%); transitive
leaps keep uts T3L fast (0.66 of the time without them). In one run at 30
workers v2.4.0 reached a speedup of 25.2 on uts T3L against Lace's 21.8; at
27 workers the two were equal (0.518 against 0.511 s), and random leaps did
not help it (0.536 s).

**64 cores.** On all 64 cores of the EPYC 7742, 3 rounds, geomean over 16
programs: matmul is left out, since its time varied up to sevenfold between
rounds for all three systems on this machine.

.. list-table:: Speedup, AMD EPYC 7742 (64 cores)
   :header-rows: 1

   * - Workers
     - Lace
     - Lace v2.4.0
     - OpenCilk
   * - 8
     - 5.6
     - 5.4
     - 4.5
   * - 16
     - 10.3
     - 9.5
     - 8.1
   * - 32
     - 17.0
     - 15.0
     - 13.4
   * - 48
     - 20.9
     - 18.3
     - 16.5
   * - 64
     - 23.7
     - 20.5
     - 18.2

v2.4.0 takes 1.05 times as long as the final design at 8 workers and 1.16
times at 64, OpenCilk 1.27-1.31 times (at 64 workers cholesky 20.8 against
9.1 and 14.5, lu 32.5 against 16.5 and 27.1, fft 24.5 against 18.0 and
25.1). In one binary at 64 workers, the time of the final design relative to
random leaps back on is 0.905 (cholesky 0.57, lu 0.55, fft 0.75), relative to
no transitive leaps 0.988, and relative to unpinned workers 0.961. Two
programs scale worse than with v2.4.0. uts T3L reaches a speedup of 30
against 32 (OpenCilk 33), and random leaps make it 1.30 times as fast.
quicksort takes 1.24 against 0.99 s (OpenCilk 1.09); neither leap setting
changes that, and on the 32-core AMD node it took 4-9% longer than with
v2.4.0 too. With 64 workers waking the sleepers costs more: a region of 1000
tasks of 1 µs after a 5 ms sleep takes 172 µs (v2.4.0 1099, OpenCilk 68,
Parlay 48), and one of 100000 tasks of 100 ns 558 µs (4944, 319, 360). An
idle Lace uses 0.074 CPU-s in 2 s after work, v2.4.0 1.87.


Tried and dropped
-----------------

* Random leaps, and random leaps only while all workers are busy (they would
  close most of the gap to Parlay on PBBS, at a larger cost elsewhere).
* A ``cilk_for``-style grain in the Parlay plugin (no gain).
* A fixed tiny-steal threshold (2000 ns was as good on the server machines,
  but gave up the gain from cheap steals on the desktop processor).
* A helper wake-up by the worker that takes the task, also only when all
  other workers sleep (it did not remove the 2 µs).
* One sentinel without the spin, and a sentinel grace of 50 ms.
* A cap of 1 ms on a sleeper's timeout while some worker is busy (no effect
  on any machine).
* Pinning blocks of cores per worker when there are few workers (lost to all
  cores per worker with two programs).
* A 100 µs sentinel nap (slower on Intel), sentinels that do not ask for
  sharing (no difference).
* Extra steal rounds after a wake-up, and a hint which worker to steal from
  first (no difference once sentinels watch).


Open issues
-----------

* The handoff to a sleeping worker after a few milliseconds of idle (9-13 µs
  against OpenCilk's 2.3-5), the price of a calling thread that is not a
  worker.
* PBBS on AMD: OpenCilk is ahead of Lace on 18 of 21 benchmarks (4% without
  maximalMatching, whose gap is ``cilk_for``). With Parlay's loops OpenCilk
  is still 3-8% ahead on histogram, maximalIndependentSet and wordCounts: it
  never waits at a join (continuation stealing, out of reach for Lace with
  one stack per worker), and the hardware counters show no locality
  advantage. On a few benchmarks Lace is 4-9% behind Parlay, the price of
  leapfrogging only. The build phase of rangeQuery2d is slow on every
  machine.
* On AMD EPYC the suite runs 1.6-1.8% faster without sentinels; none of the
  sentinel variants tried removed that.
* At 64 cores (see Scaling): uts T3L, which random leaps would help at a
  larger cost elsewhere, and quicksort on AMD, whose cause is not known.
  Regions after a few milliseconds of idle start 2.5-3.6 times slower than
  with OpenCilk or Parlay.
* Pinning on FreeBSD and Windows (Linux only so far); timer granularity on
  Windows and macOS would change the idle behaviour, which was measured on
  Linux only.
* Scaling beyond 64 cores was not tested.
