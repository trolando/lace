// Scheduler plugin for Lace (https://github.com/trolando/lace)
//
// Install as parlay/internal/scheduler_plugins/lace.h, add the PARLAY_LACE
// branch to parlay/parallel.h (parallel.h.patch next to this file), compile
// with -DPARLAY_LACE and link against Lace.
//
// par_do is a Lace SPAWN of the right function, a call of the left one, and
// a SYNC. parallel_for splits the range in halves with par_do, like Parlay's
// own scheduler, and with granularity 0 picks the grain the same way.
//
// A thread that is not a Lace worker (such as main) enters Lace for every
// outermost par_do or parallel_for: the call runs as one task on a worker
// while the calling thread waits.
//
// Lace starts on first use, with PARLAY_NUM_THREADS workers (default: every
// core Lace may use), unless the program started Lace itself. Lace stops at
// exit when this plugin started it.
//
// Workers have ids 0..n-1 and every other thread has id n, so num_workers()
// is n + 1. Only one non-worker thread at a time may use worker-indexed
// state such as parlay::WorkerSpecific.
//
// The conservative flag is ignored. While it waits for a stolen task, a
// Lace worker runs tasks spawned by that task (leapfrogging), and rarely,
// when the thief has just finished it, another task. Holding a lock across
// a par_do can therefore deadlock, as with Parlay's own scheduler when
// conservative is false.
//
// Exceptions must not leave a task: an exception thrown between the SPAWN
// and the SYNC would leave the right function in the deque.

#ifndef PARLAY_INTERNAL_SCHEDULER_PLUGINS_LACE_H_
#define PARLAY_INTERNAL_SCHEDULER_PLUGINS_LACE_H_

#include <cstddef>
#include <cstdlib>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <lace.h>

namespace parlay {

// IWYU pragma: private, include "../../parallel.h"

namespace internal {

// Starts Lace on first use, unless the program already did, and stops it at
// exit if it started it.
class lace_session {
 public:
  lace_session() {
    if (!lace_is_running()) {
      unsigned int n = 0;
      if (const char* env = std::getenv("PARLAY_NUM_THREADS")) n = static_cast<unsigned int>(std::stoul(env));
      lace_start(n, 0, 0);
      started = true;
    }
    workers = lace_worker_count();
  }
  ~lace_session() {
    if (started) lace_stop();
  }
  lace_session(const lace_session&) = delete;
  lace_session& operator=(const lace_session&) = delete;

  size_t workers = 0;
  bool started = false;
};

inline lace_session& get_lace_session() {
  static lace_session session;
  return session;
}

// One Lace task type runs every closure: a function pointer that knows the
// closure's type, and the closure's address. The closure lives in the frame
// of par_do, which outlives the task.
typedef void (*lace_thunk_fn)(void*);

TASK(void, parlay_lace_thunk, lace_thunk_fn, fn, void*, arg)

inline void parlay_lace_thunk_CALL(lace_worker*, lace_thunk_fn fn, void* arg) { fn(arg); }

template <typename F>
void lace_invoke(void* p) {
  (*static_cast<F*>(p))();
}

template <typename F>
void* lace_closure(F& f) {
  return const_cast<void*>(static_cast<const void*>(std::addressof(f)));
}

// Runs f() as a task on a Lace worker; the calling thread is not a worker
template <typename F>
void lace_enter(F& f) {
  get_lace_session();
  parlay_lace_thunk(&lace_invoke<F>, lace_closure(f));
}

template <typename F>
void lace_parfor(lace_worker* lw, size_t start, size_t end, F& f, size_t granularity);

}  // namespace internal

inline size_t num_workers() { return internal::get_lace_session().workers + 1; }

inline size_t worker_id() {
  int id = lace_worker_id();
  return id >= 0 ? static_cast<size_t>(id) : internal::get_lace_session().workers;
}

template <typename Lf, typename Rf>
inline void par_do(Lf&& left, Rf&& right, bool) {
  static_assert(std::is_invocable_v<Lf&&>);
  static_assert(std::is_invocable_v<Rf&&>);

  lace_worker* lw = lace_get_worker();
  if (LACE_UNLIKELY(lw == nullptr)) {
    auto both = [&]() { par_do(std::forward<Lf>(left), std::forward<Rf>(right), false); };
    internal::lace_enter(both);
    return;
  }

  internal::parlay_lace_thunk_SPAWN(lw, &internal::lace_invoke<std::remove_reference_t<Rf>>,
                                    internal::lace_closure(right));
  std::forward<Lf>(left)();
  internal::parlay_lace_thunk_SYNC(lw);
}

namespace internal {

// Run f on a prefix of the range, doubling the prefix until a round takes a
// microsecond; return how many iterations ran. As in Parlay's scheduler.
template <typename F>
size_t lace_get_granularity(size_t start, size_t end, F& f) {
  size_t done = 0;
  size_t sz = 1;
  unsigned long long ticks = 0;
  do {
    sz = (std::min)(sz, end - (start + done));
    auto tstart = std::chrono::steady_clock::now();
    for (size_t i = 0; i < sz; i++) f(start + done + i);
    auto tstop = std::chrono::steady_clock::now();
    ticks = static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(tstop - tstart).count());
    done += sz;
    sz *= 2;
  } while (ticks < 1000 && done < (end - start));
  return done;
}

template <typename F>
void lace_parfor(lace_worker* lw, size_t start, size_t end, F& f, size_t granularity) {
  if ((end - start) <= granularity) {
    for (size_t i = start; i < end; i++) f(i);
  } else {
    size_t n = end - start;
    // Not in the middle, to avoid clashes on set-associative caches on powers of 2
    size_t mid = (start + (9 * (n + 1)) / 16);
    auto right = [&]() { lace_parfor(lace_get_worker(), mid, end, f, granularity); };
    parlay_lace_thunk_SPAWN(lw, &lace_invoke<decltype(right)>, lace_closure(right));
    lace_parfor(lw, start, mid, f, granularity);
    parlay_lace_thunk_SYNC(lw);
  }
}

}  // namespace internal

template <typename F>
inline void parallel_for(size_t start, size_t end, F&& f, long granularity, bool) {
  static_assert(std::is_invocable_v<F&, size_t>);

  if (end <= start) return;
  if (end == start + 1) {
    f(start);
    return;
  }
  if (granularity > 0 && (end - start) <= static_cast<size_t>(granularity)) {
    for (size_t i = start; i < end; i++) f(i);
    return;
  }

  lace_worker* lw = lace_get_worker();
  if (LACE_UNLIKELY(lw == nullptr)) {
    auto loop = [&]() { parallel_for(start, end, f, granularity, false); };
    internal::lace_enter(loop);
    return;
  }

  size_t grain = static_cast<size_t>(granularity);
  if (grain == 0) {
    size_t done = internal::lace_get_granularity(start, end, f);
    grain = (std::max)(done, (end - start) / (128 * internal::get_lace_session().workers));
    start += done;
  }
  internal::lace_parfor(lw, start, end, f, grain);
}

template <typename... Fs>
void execute_with_scheduler(Fs...) {
  struct Illegal {};
  static_assert((std::is_same_v<Illegal, Fs> && ...), "parlay::execute_with_scheduler is only available in the Parlay scheduler and is not compatible with Lace");
}

}  // namespace parlay

#endif  // PARLAY_INTERNAL_SCHEDULER_PLUGINS_LACE_H_
