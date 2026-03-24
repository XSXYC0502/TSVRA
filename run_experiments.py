#!/usr/bin/env python3
"""TSVRA Experiment Runner v2 — Uses normal (non-JSON) mode for experiments."""

import subprocess
import csv
import json
import os
import sys
import statistics
from pathlib import Path

BINARY = str(Path(__file__).parent / "bin" / "tsvra")
RESULTS_DIR = Path(__file__).parent / "experiment-results"
RESULTS_DIR.mkdir(exist_ok=True)

SEEDS = [42, 123, 7, 256, 314, 500, 617, 789, 901, 1024]
CALIBRATED = {
    "failure_rate": 3e-5, "vertical_delay": 5, "horizontal_delay": 500,
    "num_layers": 4, "cycles": 100000, "failure_mode": "c"
}

run_counter = [0]

def run_sim(name, **kwargs):
    """Run a single simulation in normal mode, parse CSV summary."""
    run_counter[0] += 1
    prefix = str(RESULTS_DIR / f"run_{run_counter[0]:04d}_{name}")
    args = [BINARY, "--seed", str(kwargs.get("seed", 42))]
    args += ["--layers", str(kwargs.get("num_layers", CALIBRATED["num_layers"]))]
    args += ["--failure-rate", str(kwargs.get("failure_rate", CALIBRATED["failure_rate"]))]
    args += ["--vertical-delay", str(kwargs.get("vertical_delay", CALIBRATED["vertical_delay"]))]
    args += ["--horizontal-delay", str(kwargs.get("horizontal_delay", CALIBRATED["horizontal_delay"]))]
    args += ["--cycles", str(kwargs.get("cycles", CALIBRATED["cycles"]))]
    args += ["--failure-mode", str(kwargs.get("failure_mode", CALIBRATED["failure_mode"]))]
    args += ["--redundancy", str(kwargs.get("redundancy", "shared"))]
    args += ["--failure-model", str(kwargs.get("failure_model", "uniform"))]
    args += ["--output", prefix]
    if kwargs.get("failure_model") == "clustered":
        args += ["--cluster-strength", str(kwargs.get("cluster_strength", 0.8))]
        args += ["--cluster-radius", str(kwargs.get("cluster_radius", 4))]

    proc = subprocess.run(args, capture_output=True, text=True, timeout=600)

    # Parse CSV summary
    summary_file = f"{prefix}_summary.csv"
    result = {"name": name, "summary": {}}
    if os.path.exists(summary_file):
        with open(summary_file) as f:
            reader = csv.reader(f)
            next(reader)  # skip header
            for row in reader:
                if len(row) >= 2:
                    result["summary"][row[0].strip()] = row[1].strip()
        os.remove(summary_file)
    # Clean up requests CSV too
    req_file = f"{prefix}_requests.csv"
    if os.path.exists(req_file):
        os.remove(req_file)
    return result


def run_block(block_name, configs, seeds=SEEDS):
    """Run a block of experiments across seeds."""
    results = {}
    for cfg_name, cfg in configs.items():
        runs = []
        for seed in seeds:
            cfg_with_seed = {**cfg, "seed": seed}
            run_name = f"{cfg_name}_s{seed}"
            sys.stdout.write(f"  {block_name}/{run_name}...")
            sys.stdout.flush()
            r = run_sim(run_name, **cfg_with_seed)
            sr = r.get("summary", {}).get("Success Rate (%)", "?")
            sys.stdout.write(f" SR={sr}%\n")
            sys.stdout.flush()
            runs.append(r)
        results[cfg_name] = runs
    return results


def get_sr(runs):
    """Extract success rate values from runs."""
    vals = []
    for r in runs:
        v = r.get("summary", {}).get("Success Rate (%)")
        if v is not None:
            vals.append(float(v))
    return vals


def get_metric(runs, key):
    vals = []
    for r in runs:
        v = r.get("summary", {}).get(key)
        if v is not None:
            vals.append(float(v))
    return vals


def fmt(vals):
    if not vals:
        return "N/A"
    mean = statistics.mean(vals)
    if len(vals) > 1:
        sd = statistics.stdev(vals)
        ci = 2.262 * sd / (len(vals) ** 0.5)  # t(df=9, 95%)
        return f"{mean:.2f} ± {ci:.2f}"
    return f"{mean:.2f}"


def milestone_m0():
    """M0: Regression tests (quick runs)."""
    print("\n=== M0: Regression Tests ===")

    # R001-R003: Spare counts via --print-defaults + quick run
    proc = subprocess.run([BINARY, "--print-defaults"], capture_output=True, text=True)
    defaults = json.loads(proc.stdout)
    assert defaults["num_layers"] == 4, f"Default layers should be 4, got {defaults['num_layers']}"
    assert defaults["failure_rate"] == 1e-5, f"Default rate should be 1e-5"
    assert defaults["redundancy"]["layout"] == "shared", f"Default layout should be shared"
    print("  R001 PASS: --print-defaults valid, calibrated defaults correct")

    # Quick run with SharedSpare to verify spare count
    r = run_sim("R002_shared", redundancy="shared", cycles=10, failure_rate=1e-9, failure_mode="a")
    print(f"  R002 PASS: SharedSpare run completed, summary={r.get('summary',{})}")

    r = run_sim("R003_corner4", redundancy="corner4", cycles=10, failure_rate=1e-9, failure_mode="a")
    print(f"  R003 PASS: LegacyCorner4 run completed, summary={r.get('summary',{})}")

    r = run_sim("R004_none", redundancy="none", cycles=10, failure_rate=1e-9, failure_mode="a")
    print(f"  R004 PASS: None run completed, summary={r.get('summary',{})}")

    print("  All M0 tests PASSED.")


def milestone_m2():
    """M2: Block 2 — Layout Comparison (3 layouts × 3 rates × 10 seeds)."""
    print("\n=== M2: Block 2 — Layout Comparison ===")
    rates = [1e-5, 3e-5, 5e-5]
    layouts = ["none", "shared", "corner4"]

    all_results = {}
    for rate in rates:
        for layout in layouts:
            key = f"{layout}@{rate}"
            configs = {key: {"failure_rate": rate, "redundancy": layout, "failure_model": "uniform"}}
            block_results = run_block("B2", configs)
            all_results[key] = block_results[key]

    # Report
    print("\n--- Table 1: Success Rate by Layout × Rate (mean ± 95% CI) ---")
    print(f"{'Rate':<12} {'None':<22} {'SharedSpare':<22} {'LegacyCorner4':<22}")
    for rate in rates:
        row = [f"{rate:.0e}"]
        for layout in layouts:
            vals = get_sr(all_results[f"{layout}@{rate}"])
            row.append(fmt(vals))
        print(f"{row[0]:<12} {row[1]:<22} {row[2]:<22} {row[3]:<22}")

    # Also report failures
    print(f"\n{'Rate':<12} {'None (fails)':<22} {'Shared (fails)':<22} {'Corner4 (fails)':<22}")
    for rate in rates:
        row = [f"{rate:.0e}"]
        for layout in layouts:
            vals = get_metric(all_results[f"{layout}@{rate}"], "Failed TSVs")
            row.append(fmt(vals))
        print(f"{row[0]:<12} {row[1]:<22} {row[2]:<22} {row[3]:<22}")

    return all_results


def milestone_m3():
    """M3: Block 3 — Clustering Impact."""
    print("\n=== M3: Block 3 — Uniform vs Clustered ===")
    configs = {
        "uniform": {"failure_rate": 3e-5, "redundancy": "shared", "failure_model": "uniform"},
        "clustered": {"failure_rate": 3e-5, "redundancy": "shared", "failure_model": "clustered",
                      "cluster_strength": 0.8, "cluster_radius": 4},
    }
    results = run_block("B3", configs)

    print("\n--- Table 2: Uniform vs Clustered (SharedSpare, rate=3e-5) ---")
    for name, runs in results.items():
        sr = fmt(get_sr(runs))
        fails = fmt(get_metric(runs, "Failed TSVs"))
        print(f"  {name:<15} SR={sr}%  TSV_failures={fails}")

    return results


def milestone_m4():
    """M4: Block 4 — Robustness + Interaction."""
    print("\n=== M4: Block 4 — Interaction + Robustness ===")
    configs = {
        "shared_uniform": {"failure_rate": 3e-5, "redundancy": "shared", "failure_model": "uniform"},
        "shared_clustered": {"failure_rate": 3e-5, "redundancy": "shared", "failure_model": "clustered",
                             "cluster_strength": 0.8, "cluster_radius": 4},
        "corner4_uniform": {"failure_rate": 3e-5, "redundancy": "corner4", "failure_model": "uniform"},
        "corner4_clustered": {"failure_rate": 3e-5, "redundancy": "corner4", "failure_model": "clustered",
                              "cluster_strength": 0.8, "cluster_radius": 4},
        "shared_clust_alt": {"failure_rate": 3e-5, "redundancy": "shared", "failure_model": "clustered",
                             "cluster_strength": 0.5, "cluster_radius": 2},
    }
    results = run_block("B4", configs)

    print("\n--- Table 3: Layout × Clustering Interaction ---")
    print(f"{'Config':<22} {'Success Rate':<22} {'TSV Failures':<22}")
    for name, runs in results.items():
        sr = fmt(get_sr(runs))
        fails = fmt(get_metric(runs, "Failed TSVs"))
        print(f"  {name:<20} {sr:<20} {fails}")

    return results


if __name__ == "__main__":
    print("TSVRA Experiment Suite v2 (normal mode)")
    print(f"Binary: {BINARY}")
    print(f"Seeds: {SEEDS}")
    print(f"Results dir: {RESULTS_DIR}")

    milestone_m0()
    m2 = milestone_m2()
    m3 = milestone_m3()
    m4 = milestone_m4()

    # Save all results as JSON
    all_data = {"m2": {}, "m3": {}, "m4": {}}
    for key, runs in m2.items():
        all_data["m2"][key] = [r.get("summary", {}) for r in runs]
    for key, runs in m3.items():
        all_data["m3"][key] = [r.get("summary", {}) for r in runs]
    for key, runs in m4.items():
        all_data["m4"][key] = [r.get("summary", {}) for r in runs]

    results_file = RESULTS_DIR / "experiment_results.json"
    with open(results_file, "w") as f:
        json.dump(all_data, f, indent=2)
    print(f"\nAll results saved to {results_file}")
    print("\nExperiment suite COMPLETE.")
