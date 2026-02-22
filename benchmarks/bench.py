#!/usr/bin/env python

from __future__ import print_function
import itertools # count
import math # sqrt
import multiprocessing # cpu_count
import os # mkdir
import random # shuffle
import re # compile
import statistics # median
import subprocess # Popen, call
import sys # stdout, exit
import time # sleep
from collections import defaultdict

# First some subroutines to handle output from the log files

def extract(content, thing, letter):
    """
    Extracts a numeric value associated with a given label from text output.

    Parameters:
        content (str): The full output text to search.
        thing (str): The label to search for (e.g., "Time").
        letter (str): Short key used in result dictionary (e.g., "Ti").

    Returns:
        dict: A dictionary {letter: value} if exactly one match is found, else {}.
    """
    pattern = re.compile(re.escape(thing) + r':[\W]*([\d\.]+)')
    matches = pattern.findall(content)
    return {letter: matches[0]} if len(matches) == 1 else {}


def process_result(content):
    """
    Extracts all known timing results from benchmark output.

    Parameters:
        content (str): The full output text from a benchmark run.

    Returns:
        dict: A dictionary of short labels to extracted string values.
    """
    times = {}
    metrics = [
        ("Time", "Ti"),
        ("Steal work", "Sw"),
        ("Leap work", "Lw"),
        ("Steal overhead", "So"),
        ("Leap overhead", "Lo"),
        ("Steal search", "Ss"),
        ("Leap search", "Ls")
    ]
    for label, short in metrics:
        times.update(extract(content, label, short))
    return times


def run_item_file(name, args, workers, filename, dry=False, fresh=False):
    """
    Runs a benchmark command and processes the result, with optional caching.

    Parameters:
        name (str): Logical name of the benchmark.
        args (list[str]): Command to execute, as argument list.
        workers (int): Number of workers used (for logging only).
        filename (str): File to store or read benchmark output.
        dry (bool): If True, skip execution.
        fresh (bool): If True, ignore previous results and rerun benchmark.

    Returns:
        dict or None: Extracted timing results if successful, or None.
    """
    if os.path.isfile(filename):
        with open(filename, "r") as out:
            content = out.read()
            times = process_result(content)
        if times and 'Ti' in times:
            if fresh:
                # only return if fresh!
                return None  # not a fresh result
            else:
                return times
        else:
            print(f"Discarding invalid previous run of {name}-{workers}.")
            os.unlink(filename)

    if dry:
        return None

    if not os.path.isfile(args[0]):
        print(f"Program {args[0]} does not exist (experiment {name})!")
        return None

    print(f"Performing {name}-{workers}... ", end='', flush=True)

    try:
        with open(filename, "w+") as out:
            subprocess.call(args, stdout=out, stderr=out)
            out.seek(0)
            times = process_result(out.read())
            time.sleep(2)
    except KeyboardInterrupt:
        os.unlink(filename)
        print("interrupted!")
        sys.exit(1)
    except OSError:
        os.unlink(filename)
        print("failure! (Program may not have been compiled)")
        sys.exit(1)
    else:
        if times and 'Ti' in times:
            print(f"done in {times['Ti']} seconds!")
        else:
            print("done, but no result!")
        return times


def compute_stats(data, B=1000, seed=None):
    """
    Bootstrap-based estimate of median stability.
    
    Parameters:
        data (list of float): Sample values.
        B (int): Number of bootstrap replications (default: 1000).
        seed (int or None): Random seed for reproducibility.

    Returns:
        (n, mean_median, rel_sem):
            n           = number of input samples
            mean_median = average of bootstrapped medians
            rel_sem     = relative standard error (None if n < 2)
    """
    n = len(data)

    if n == 0:
        return n, None, None

    if n == 1:
        return n, data[0], None

    if seed is not None:
        random.seed(seed)

    medians = []
    for _ in range(B):
        sample = random.choices(data, k=n)
        medians.append(statistics.median(sample))

    mean_median = statistics.mean(medians)
    if mean_median == 0:
        rel_sem = float('inf')
    else:
        stdev = statistics.stdev(medians)
        sem = stdev / math.sqrt(B)
        rel_sem = sem / mean_median

    return n, statistics.median(data), rel_sem


def report_results(results):
    """
    Print summary statistics for each (name, workers) pair in results.

    Parameters:
        results (dict): A mapping from (name, workers) to a list of float timings.
                        Example: {("exp1", 4): [0.32, 0.30, 0.31], ...}
    """
    names = {name for (name, _workers) in results}
    for exp in sorted(names):
        workers_in_data = {workers for (name, workers) in results if name == exp}
        mean_1 = None
        for w in sorted(workers_in_data):
            data = list(results.get((exp, w), {}).values())
            n, mean_m, rel_sem = compute_stats(data)

            rel_sem_str = f"{rel_sem * 100:.2f}%" if rel_sem is not None else "-"
            mean_str = f"{mean_m:.2f}" if mean_m is not None else "-"

            print(f"{exp + '-' + str(w):<16}: {mean_str:<8} ε={rel_sem_str:<7} n={n:<5}")


def get_done(results):
    """
    Determine which benchmark configurations have converged.

    A result is considered 'done' if:
      - It has at least 10 data points.
      - The relative standard error of the mean of the last 10 medians is < 1%.

    Parameters:
        results (dict): Mapping from (name, workers) to list of float timings.

    Returns:
        set of (name, workers): Configurations considered statistically stable.
    """
    done = set()
    for (name, workers), data in results.items():
        n, _, rel_sem = compute_stats(list(data.values()))
        if n >= 10 and rel_sem is not None and rel_sem < 0.001:
            done.add((name, workers))
    return done


def results_to_file(results, filename):
    """
    Write collected benchmark results to a CSV-like file.

    Format:
        Name; Workers; Key; Value

    Only the 'Ti' (Time) key is written, for each sample.

    Parameters:
        results (dict): Mapping from (name, workers) to list of float timings.
        filename (str): Output filename.
    """
    with open(filename, "w") as out:
        out.write("Name; Workers; Key; Value\n")
        for (name, workers), result_list in results.items():
            for result in result_list.values():
                out.write(f"{name};{workers};Ti;{result}\n")


# ====================================
# Small Workloads (~4 million nodes):
# ====================================

# (T1) Geometric [fixed] ------- Tree size = 4130071, tree depth = 10, num leaves = 3305118 (80.03%)
T1="-t 1 -a 3 -d 10 -b 4 -r 19"
# (T5) Geometric [linear dec.] - Tree size = 4147582, tree depth = 20, num leaves = 2181318 (52.59%)
T5="-t 1 -a 0 -d 20 -b 4 -r 34"
# (T2) Geometric [cyclic] ------ Tree size = 4117769, tree depth = 81, num leaves = 2342762 (56.89%)
T2="-t 1 -a 2 -d 16 -b 6 -r 502"
# (T3) Binomial ---------------- Tree size = 4112897, tree depth = 1572, num leaves = 3599034 (87.51%)
T3="-t 0 -b 2000 -q 0.124875 -m 8 -r 42"
# (T4) Hybrid ------------------ Tree size = 4132453, tree depth = 134, num leaves = 3108986 (75.23%)
T4="-t 2 -a 0 -d 16 -b 6 -r 1 -q 0.234375 -m 4 -r 1"

# ====================================
# Large Workloads (~100 million nodes):
# ====================================

# (T1L) Geometric [fixed] ------ Tree size = 102181082, tree depth = 13, num leaves = 81746377 (80.00%)
T1L="-t 1 -a 3 -d 13 -b 4 -r 29"
# (T2L) Geometric [cyclic] ----- Tree size = 96793510, tree depth = 67, num leaves = 53791152 (55.57%)
T2L="-t 1 -a 2 -d 23 -b 7 -r 220"
# (T3L) Binomial --------------- Tree size = 111345631, tree depth = 17844, num leaves = 89076904 (80.00%)
T3L="-t 0 -b 2000 -q 0.200014 -m 5 -r 7"

# ====================================
# Extra Large (XL) Workloads (~1.6 billion nodes):
# ====================================

# (T1XL) Geometric [fixed] ----- Tree size = 1635119272, tree depth = 15, num leaves = 1308100063 (80.00%)
T1XL="-t 1 -a 3 -d 15 -b 4 -r 29"

# ====================================
# Extra Extra Large (XXL) Workloads (~3-10 billion nodes):
# ====================================

# (T1XXL) Geometric [fixed] ---- Tree size = 4230646601, tree depth = 15 
T1XXL="-t 1 -a 3 -d 15 -b 4 -r 19"
# (T3XXL) Binomial ------------- Tree size = 2793220501 
T3XXL="-t 0 -b 2000 -q 0.499995 -m 2 -r 316"
# (T2XXL) Binomial ------------- Tree size = 10612052303, tree depth = 216370, num leaves = 5306027151 (50.00%) 
T2XXL="-t 0 -b 2000 -q 0.499999995 -m 2 -r 0"

# ====================================
# Wicked Large Workloads (~150-300 billion nodes):
# ====================================

# (T1WL) Geometric [fixed] ----- Tree size = 270751679750, tree depth = 18, num leaves = 216601257283 (80.00%)
T1WL="-t 1 -a 3 -d 18 -b 4 -r 19"
# (T2WL) Binomial -------------- Tree size = 295393891003, tree depth = 1021239, num leaves = 147696946501 (50.00%)
T2WL="-t 0 -b 2000 -q 0.4999999995 -m 2 -r 559"
# (T3WL) Binomial -------------- Tree size = 157063495159, tree depth = 758577, num leaves = 78531748579 (50.00%) 
T3WL="-t 0 -b 2000 -q 0.4999995 -m 2 -r 559"


def determine_worker_configs():
    """
    Determine which worker counts to benchmark, based on CPU count.
    Returns:
        tuple: List of worker counts, e.g., (1, 4, 16)
    """
    max_cores = multiprocessing.cpu_count()
    if max_cores > 4:
        return (1, 4, max_cores)
    elif max_cores > 2:
        return (1, 2, max_cores)
    elif max_cores > 1:
        return (1, max_cores)
    return (1,)


def discover_experiments(W):
    """
    Discover available benchmark programs and generate experiment definitions.

    Parameters:
        W (tuple): Tuple of worker counts to test (e.g., (1, 4, 16)).

    Returns:
        list of (name, args, workers): Experiments to run.
    """
    experiments = []

    for w in W:
        def lace(name, exe, *args):
            if os.path.isfile(exe):
                experiments.append((name, [f"./{exe}", "-w", str(w), *args], w))
            if os.path.isfile(exe+".exe"):
                experiments.append((name, [f"./{exe}.exe", "-w", str(w), *args], w))

        lace("fib", "fib-lace", "46")
        lace("uts-t2l", "uts-lace", *globals().get("T2L", "").split())
        lace("uts-t3l", "uts-lace", *globals().get("T3L", "").split())
        lace("nqueens", "nqueens-lace", "14")
        lace("matmul", "matmul-lace", "2048")
        lace("cholesky", "cholesky-lace", "4000", "40000")
        lace("integrate", "integrate-lace" , "10000")
        lace("heat", "heat-lace")
        lace("cilksort", "cilksort-lace")
        lace("fft", "fft-lace")
        lace("knapsack", "knapsack-lace")
        lace("lu", "lu-lace")
        lace("pi", "pi-lace")
        lace("quicksort", "quicksort-lace")
        lace("rectmul", "rectmul-lace")
        lace("strassen", "strassen-lace")

    def seq(name, exe, *args):
        if os.path.isfile(exe):
            experiments.append((f"{name}-seq", [f"./{exe}", *args], 1))
        if os.path.isfile(exe+".exe"):
            experiments.append((f"{name}-seq", [f"./{exe}.exe", *args], 1))

    seq("fib", "fib-seq", "46")
    seq("uts-t2l", "uts-seq", *globals().get("T2L", "").split())
    seq("uts-t3l", "uts-seq", *globals().get("T3L", "").split())
    seq("nqueens", "nqueens-seq", "14")
    seq("matmul", "matmul-seq", "2048")
    seq("cholesky", "cholesky-seq", "4000", "40000")
    seq("integrate", "integrate-seq" , "10000")
    seq("heat", "heat-seq")
    seq("cilksort", "cilksort-seq")
    seq("fft", "fft-seq")
    seq("knapsack", "knapsack-seq")
    seq("lu", "lu-seq")
    seq("pi", "pi-seq")
    seq("quicksort", "quicksort-seq")
    seq("rectmul", "rectmul-seq")
    seq("strassen", "strassen-seq")

    return experiments


def run_benchmark_loop(experiments, outdir):
    """
    Execute the main benchmark loop: load cached results, then run fresh experiments.

    Parameters:
        experiments (list): List of (name, args, workers) triples.
        outdir (str): Output directory to store benchmark result files.
    """
    if not os.path.exists(outdir):
        os.makedirs(outdir)

    results = defaultdict(dict)

    # Load cached results
    for i in itertools.count():
        new_results = False
        for name, call, workers in experiments:
            result = run_item_file(name, call, workers, f"{outdir}/{name}-{workers}-{i}", dry=True)
            if result is not None:
                results[(name, workers)][i] = float(result['Ti'])
                new_results = True
        if not new_results:
            break

    report_results(results)
    done = get_done(results)
    results_to_file(results, "results.csv")
    print()

    # Run fresh experiments
    for i in itertools.count():
        print(f"Running iteration {i}...")
        new_data = False
        random.shuffle(experiments)
        for name, call, workers in experiments:
            if (name, workers) not in done:
                result = run_item_file(name, call, workers, f"{outdir}/{name}-{workers}-{i}", fresh=True)
                if result is not None:
                    results[(name, workers)][i] = float(result['Ti'])
                    new_data = True
        if new_data:
            print(f"\nResults after {i+1} iterations:")
            report_results(results)
            done = get_done(results)
            print()
        if len(done) == len(experiments):
            break


if __name__ == "__main__":
    outdir = sys.argv[1] if len(sys.argv) > 1 else 'exp-out'
    W = determine_worker_configs()
    experiments = discover_experiments(W)
    run_benchmark_loop(experiments, outdir)

