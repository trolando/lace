// Tests for the Lace scheduler plugin for ParlayLib.
// Build: c++ -std=c++17 -O2 -DPARLAY_LACE -I<parlaylib>/include -I<lace build>
//        -I<lace>/src test_parlay.cpp <lace build>/liblace.a -pthread

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <parlay/parallel.h>
#include <parlay/primitives.h>
#include <parlay/random.h>
#include <parlay/sequence.h>

#ifndef PARLAY_LACE
#error "compile with -DPARLAY_LACE"
#endif

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                      \
    }                                                                  \
  } while (0)

static long fib(int n) {
  if (n < 2) return n;
  long a, b;
  parlay::par_do([&]() { a = fib(n - 1); }, [&]() { b = fib(n - 2); });
  return a + b;
}

int main() {
  // par_do from outside Lace, recursing inside
  CHECK(fib(25) == 75025);

  const size_t W = parlay::num_workers();
  CHECK(W >= 2);  // at least one worker plus the outside id
  CHECK(parlay::worker_id() == W - 1);

  // parallel_for with the plugin's own granularity, and with a fixed one
  for (long g : {0L, 1L, 100L}) {
    const size_t n = 1000003;
    std::vector<std::atomic<int>> seen(n);
    std::atomic<bool> bad_id{false};
    parlay::parallel_for(0, n, [&](size_t i) {
      seen[i].fetch_add(1, std::memory_order_relaxed);
      if (parlay::worker_id() >= W - 1) bad_id = true;  // inside Lace: a worker id
    }, g);
    size_t wrong = 0;
    for (size_t i = 0; i < n; i++) wrong += (seen[i].load() != 1);
    CHECK(wrong == 0);
    CHECK(!bad_id);
  }

  // empty and single-element ranges
  int calls = 0;
  parlay::parallel_for(5, 5, [&](size_t) { calls++; });
  parlay::parallel_for(5, 6, [&](size_t i) { calls += (i == 5); });
  CHECK(calls == 1);

  // nested: a parallel_for inside each side of a par_do
  std::atomic<long> sum{0};
  parlay::par_do(
      [&]() { parlay::parallel_for(0, 10000, [&](size_t i) { sum += (long)i; }); },
      [&]() { parlay::parallel_for(0, 10000, [&](size_t i) { sum += (long)i; }); });
  CHECK(sum.load() == 2 * (10000L * 9999 / 2));

  // the library on top: sort, reduce, scan, filter
  auto r = parlay::random_generator(42);
  std::uniform_int_distribution<long> dis(0, 1000000000);
  auto data = parlay::tabulate(2000000, [&](size_t i) {
    auto gen = r[i];
    return dis(gen);
  });
  auto sorted = parlay::sort(data);
  bool ordered = true;
  for (size_t i = 1; i < sorted.size(); i++) ordered &= sorted[i - 1] <= sorted[i];
  CHECK(ordered);
  CHECK(parlay::reduce(sorted) == parlay::reduce(data));

  auto ones = parlay::sequence<long>(1000000, 1);
  auto [prefix, total] = parlay::scan(ones);
  CHECK(total == 1000000);
  CHECK(prefix[999999] == 999999);

  auto evens = parlay::filter(parlay::iota<long>(1000000), [](long x) { return x % 2 == 0; });
  CHECK(evens.size() == 500000);

  // many short calls from outside Lace: each one enters Lace
  long acc = 0;
  for (int k = 0; k < 20000; k++) {
    std::atomic<long> s{0};
    parlay::parallel_for(0, 64, [&](size_t i) { s += (long)i; }, 1);
    acc += s.load();
  }
  CHECK(acc == 20000L * (64 * 63 / 2));

  if (failures) {
    std::printf("test_parlay: %d failures\n", failures);
    return 1;
  }
  std::printf("test_parlay: ok (%zu workers)\n", W - 1);
  return 0;
}
