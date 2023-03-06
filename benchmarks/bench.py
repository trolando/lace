#!/usr/bin/env python

from __future__ import print_function
import itertools # count
import math # sqrt
import multiprocessing # cpu_count
import os # mkdir
import random # shuffle
import re # compile
import subprocess # Popen, call
import sys # stdout, exit
import time # sleep

def extract(content, thing, letter):
    s = re.compile(thing+r':[\W]*([\d\.]+)').findall(content)
    return len(s) == 1 and {letter: s[0]} or {}

def proc_result(content):
    times = {}
    for a,b in [("Time","Ti"), 
                ("Steal work","Sw"), ("Leap work","Lw"), 
                ("Steal overhead","So"), ("Leap overhead","Lo"), 
                ("Steal search","Ss"), ("Leap search","Ls")]:
        times.update(extract(content, a, b))
    return times

def results_to_file(results, filename):
    with open(filename, "w") as out:
        out.write("Name; Workers; Key; Value\n")
        for name, workers, result in results:
            out.write("{};{};Ti;{}\n".format(name, workers, result))

def run_item_file(name, args, workers, filename, dry=False, fresh=False):
    if os.path.isfile(filename):
        # existing file
        with open(filename, "r") as out:
            times = proc_result(out.read())
        if times and 'Ti' in times:
            if fresh: return None
            # print("Retrieved {}-{} from previous run... {} seconds!".format(name, workers, times["Ti"]))
            return times
        else:
            print("Discarding previous run of {}-{}.".format(name, workers))
            os.unlink(filename)

    if dry: return None

    if not os.path.isfile(args[0]):
        print("Program {} does not exist!".format(args[0]))
        return None

    print("Performing {}-{}... ".format(name, workers), end='')
    sys.stdout.flush()
    try:
        with open(filename, "w+") as out:
            subprocess.call(args, stdout=out, stderr=out)
            out.seek(0)
            times = proc_result(out.read())
    except KeyboardInterrupt:
        os.unlink(filename)
        print("interrupted!")
        sys.exit()
    except OSError:
        os.unlink(filename)
        print("failure! (Program may not have been compiled)")
        sys.exit()
    else:
        if times and 'Ti' in times:
            print("done in %s seconds!" % times["Ti"])
        else:
            print("done, but no result!")
        time.sleep(2)
        return times

def online_variance(data):
    n = 0
    mean = 0
    M2 = 0
 
    for x in data:
        n = n + 1
        delta = x - mean
        mean = mean + delta/n
        M2 = M2 + delta*(x - mean)

    if n < 2: return n, mean, float('nan')
 
    variance = M2/(n - 1)
    return n, mean, variance

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

def report(results):
    names = set([name for name, workers, result in results])
    for exp in sorted(names):
        workers_in_data = set([workers for name, workers, result in results if name==exp])
        mean_1 = None
        for w in sorted(workers_in_data):
            data = [result for name, workers, result in results if name==exp and workers==w]
            n, mean, variance = online_variance(data)
            stdev = math.sqrt(variance)
            sem = math.sqrt(variance / n)
            if w == 1: mean_1 = mean
            if w != 1 and mean_1: speedup = "speedup={}".format(mean_1/mean)
            else: speedup = ""
            print("{0:<16}: {1:<8.2f} var={2:<6.2f} se={3:<6.2f} n={4:<5d} {5}".format(exp+"-"+str(w), mean, variance, sem, n, speedup))

if __name__ == "__main__":
    # Initialize experiments
    experiments = []

    # determine number of cores
    max_cores = multiprocessing.cpu_count()

    for w in (1,2,max_cores):
        if os.path.isfile('fib-lace'):
            experiments.append(("fib",("./fib-lace", "-w", str(w), "46"), w))
        if os.path.isfile('uts-lace'):
            experiments.append(("uts-t2l",["./uts-lace", "-w", str(w)] + globals()["T2L"].split(), w))
            experiments.append(("uts-t3l",["./uts-lace", "-w", str(w)] + globals()["T3L"].split(), w))
        if os.path.isfile('nqueens-lace'):
            experiments.append(("nqueens",("./nqueens-lace", "-w", str(w), "14"), w))
        if os.path.isfile('matmul-lace'):
            experiments.append(("matmul",("./matmul-lace", "-w", str(w), "2048"), w))
        if os.path.isfile('cholesky-lace'):
            experiments.append(("cholesky",("./cholesky-lace", "-w", str(w), "4000", "40000"), w))
        if os.path.isfile('integrate-lace'):
            experiments.append(("integrate",("./integrate-lace", "-w", str(w), "10000"), w))
        if os.path.isfile('heat-lace'):
            experiments.append(("heat",("./heat-lace", "-w", str(w), "1"), w))

    if os.path.isfile('fib-seq'):
        experiments.append(("fib-seq",("./fib-seq", "46"), 1))
    if os.path.isfile('uts-seq'):
        experiments.append(("uts-t2l-seq",["./uts-seq"] + globals()["T2L"].split(), 1))
        experiments.append(("uts-t3l-seq",["./uts-seq"] + globals()["T3L"].split(), 1))
    if os.path.isfile('nqueens-seq'):
        experiments.append(("nqueens-seq",("./nqueens-seq", "14"), 1))
    if os.path.isfile('matmul-seq'):
        experiments.append(("matmul-seq",("./matmul-seq", "2048"), 1))
    if os.path.isfile('cholesky-seq'):
        experiments.append(("cholesky-seq",("./cholesky-seq", "4000", "40000"), 1))
    if os.path.isfile('integrate-seq'):
        experiments.append(("integrate-seq",("./integrate-seq", "10000"), 1))
    if os.path.isfile('heat-seq'):
        experiments.append(("heat-seq",("./heat-seq", "1"), 1))

    outdir = 'exp-out'

    if not os.path.exists(outdir):
        os.makedirs(outdir)

    results = []

    # Get existing results
    for i in itertools.count():
        new_results = False
        random.shuffle(experiments)
        for name, call, workers in experiments:
            result = run_item_file(name, call, workers, "{}/{}-{}-{}".format(outdir, name, workers, i), dry=True)
            if result != None:
                results.append((name, workers, float(result['Ti'])))
                new_results = True
        if not new_results: break

    report(results)
    results_to_file(results, "results.csv")
    print()

    for i in itertools.count():
        random.shuffle(experiments)
        for name, call, workers in experiments:
            result = run_item_file(name, call, workers, "{}/{}-{}-{}".format(outdir, name, workers, i), fresh=True)
            if result != None: results.append((name, workers, float(result['Ti'])))
        print("\nResults after {} iterations:".format(i+1))
        report(results)
        print()
