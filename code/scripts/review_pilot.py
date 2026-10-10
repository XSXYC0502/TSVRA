#!/usr/bin/env python3
"""Audit pilot suitability and estimate unchanged main-study cost from its cells.

This is a descriptive operational review, not a statistical power calculation.
Runtime estimates interpolate measured pilot cell means by configured rate at
fixed layer/scenario. Bounds use the fastest/slowest pilot run in that profile.
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path

from run_experiments import build_tasks, project_path, write_csv


def scenario(parameters):
    if parameters["failure_model"] == "uniform":
        return "U"
    centers = parameters.get("hotspot_centers", [])
    points = {(p["x"], p["y"], p["z"]) if isinstance(p, dict) else tuple(p) for p in centers}
    return {(5, 5, 0): "H1", (10, 10, 0): "H2"}.get(next(iter(points)), "unknown") if len(points) == 1 else "H12"


def interpolate(points, rate):
    points = sorted(points)
    if rate <= points[0][0]:
        return points[0][1]
    for (left, a), (right, b) in zip(points, points[1:]):
        if rate <= right:
            return a + (b - a) * (rate - left) / (right - left)
    return points[-1][1]


def describe(values):
    values = [value for value in values if value is not None]
    return {"defined": len(values), "mean": statistics.fmean(values) if values else None,
            "sd": statistics.stdev(values) if len(values) > 1 else None,
            "minimum": min(values) if values else None, "maximum": max(values) if values else None}


def review(pilot_dir, main_config, jobs, calibration=None):
    results = json.loads((pilot_dir / "results.json").read_text())
    rows = results["runs"]
    manifest = json.loads((pilot_dir / "manifest.json").read_text())
    tasks = manifest["tasks"]
    rebuilt = build_tasks(json.loads((pilot_dir / "batch.json").read_text()))
    approved = build_tasks(json.loads(project_path("code/config/experiments.pilot.json").read_text()))
    def signatures(planned_tasks):
        return sorted(json.dumps({"parameters": task.parameters, "seed": task.seed}, sort_keys=True)
                      for task in planned_tasks)
    if len(rebuilt) != 156 or signatures(rebuilt) != signatures(approved):
        raise ValueError("Pilot does not match the approved 156-run settings and seed budget")
    declarations = {task["run_id"]: task for task in tasks}
    if len(declarations) != len(tasks) or set(declarations) != {task.run_id for task in rebuilt}:
        raise ValueError("Pilot manifest contains duplicate, missing or unexpected tasks")
    for task in rebuilt:
        declared = declarations[task.run_id]
        if declared["seed"] != task.seed or declared["parameters"] != task.parameters:
            raise ValueError("Pilot manifest settings/seed differ from the frozen batch")
    planned = {task["run_id"] for task in tasks}
    seen = [row["run_id"] for row in rows]
    errors = [row["run_id"] for row in rows if row["status"] != "ok"]
    if len(seen) != len(set(seen)) or set(seen) != planned or errors:
        raise ValueError("Pilot has duplicate, missing, unexpected, or failed runs; preserve failures and resolve them before main")
    cells = defaultdict(list)
    for row in rows:
        p = row["parameters"]
        declared = declarations[row["run_id"]]
        if p != declared["parameters"] or row["requested_seed"] != declared["seed"] or row["actual_seed"] != declared["seed"]:
            raise ValueError("Pilot result settings/seed differ from the manifest")
        if row["in_flight_at_end"] != 0 or row["total_requests"] != row["completed_requests"] + row["failed_requests"]:
            raise ValueError(f"Pilot not drained/accounted: {row['run_id']}")
        cells[p["num_layers"], scenario(p), p["initial_failure_rate"]].append(row)
    cell_rows = []
    runtime_profiles = defaultdict(list)
    spread_profiles = defaultdict(list)
    for (layers, fault_scenario, rate), group in sorted(cells.items()):
        record = {"num_layers": layers, "scenario": fault_scenario, "initial_failure_rate": rate, "valid_runs": len(group)}
        for key in ("wall_seconds", "peak_rss_kib", "total_requests", "completed_requests", "elapsed_cycles",
                    "average_latency_cycles", "success_rate_pct", "total_mw"):
            for statistic, value in describe([r.get(key) for r in group]).items():
                record[f"{key}_{statistic}"] = value
        drains = [r["elapsed_cycles"] - r["parameters"]["simulation_cycles"] for r in group]
        record.update({f"drain_cycles_{k}": v for k, v in describe(drains).items()})
        record["undefined_latency_runs"] = sum(r.get("average_latency_cycles") is None for r in group)
        cell_rows.append(record)
        runtime_profiles[layers, fault_scenario].append((rate, record["wall_seconds_mean"]))
        spread_profiles[layers, fault_scenario].extend(r["wall_seconds"] for r in group)
    main_spec = json.loads(main_config.read_text())
    main_tasks = build_tasks(main_spec)
    approved_main = build_tasks(json.loads(project_path("code/config/experiments.main.json").read_text()))
    if len(main_tasks) != 3960 or signatures(main_tasks) != signatures(approved_main):
        raise ValueError("Main does not match the approved 3,960-run settings and seed budget")
    def fixed(parameters):
        return {k: v for k, v in parameters.items() if k not in {"initial_failure_rate", "failure_rate"}}
    for task in main_tasks:
        profile = task.parameters["num_layers"], scenario(task.parameters)
        reference = next(group[0]["parameters"] for (layers, name, _), group in cells.items()
                         if (layers, name) == profile)
        if fixed(task.parameters) != fixed(reference):
            raise ValueError("Main workload/fault profile differs from pilot; design review required")
    estimates = []
    for task in main_tasks:
        p = task.parameters
        profile = p["num_layers"], scenario(p)
        estimates.append({"run_id": task.run_id, "expected_wall_seconds": interpolate(runtime_profiles[profile], p["initial_failure_rate"]),
                          "minimum_profile_wall_seconds": min(spread_profiles[profile]),
                          "maximum_profile_wall_seconds": max(spread_profiles[profile])})
    useful_concurrency = 1.0
    selected_jobs = 1
    if calibration:
        if calibration["parallel_jobs"] == jobs and calibration["all_successful"] and calibration["speedup"] > 1:
            useful_concurrency = min(float(jobs), calibration["speedup"])
            selected_jobs = jobs
    maximum_wall = max(r["wall_seconds"] for r in rows)
    timeout = max(30, math.ceil(20 * maximum_wall))
    bundle_bytes = sum(path.stat().st_size for path in pilot_dir.rglob("*") if path.is_file())
    native_seconds = sum(estimate["expected_wall_seconds"] for estimate in estimates)
    return {
        "schema_version": 1, "pilot_dir": str(pilot_dir), "main_config": str(main_config),
        "planned_pilot_runs": len(tasks), "accepted_pilot_runs": len(rows), "failed_runs": errors, "missing_runs": [],
        "pilot_cells": len(cells), "main_runs": len(main_tasks), "pilot_bundle_bytes": bundle_bytes,
        "projected_main_bundle_bytes": bundle_bytes * len(main_tasks) / len(rows),
        "pilot_wall_seconds": describe([r["wall_seconds"] for r in rows]),
        "peak_rss_kib": describe([r.get("peak_rss_kib") for r in rows]),
        "generated": describe([r["total_requests"] for r in rows]),
        "completed": describe([r["completed_requests"] for r in rows]),
        "drain_cycles": describe([r["elapsed_cycles"] - r["parameters"]["simulation_cycles"] for r in rows]),
        "undefined_latency_runs": sum(r.get("average_latency_cycles") is None for r in rows),
        "selected_jobs": selected_jobs, "measured_useful_concurrency": useful_concurrency,
        "selected_timeout_seconds": timeout, "concurrency_calibration": calibration,
        "estimated_sum_run_seconds": native_seconds,
        "estimated_main_seconds": native_seconds / useful_concurrency,
        "estimated_main_seconds_profile_range": [sum(e["minimum_profile_wall_seconds"] for e in estimates) / useful_concurrency,
                                               sum(e["maximum_profile_wall_seconds"] for e in estimates) / useful_concurrency],
        "estimation_limitations": "Interpolation of short measured native processes includes output validation but excludes final report/analysis costs. Bounds are observed profile extrema, not prediction intervals; resource contention can change wall time. Three pilot seeds are insufficient for a power guarantee.",
        "decision": "Proceed with unchanged authorized protocol; all pilot runs drained and accounted. No scientific settings or seed budget changed.",
        "pilot_manifest": manifest, "cells": cell_rows, "run_cost_estimates": estimates,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pilot-dir", type=Path, required=True)
    parser.add_argument("--main-config", type=Path, default=Path("code/config/experiments.main.json"))
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--concurrency-measurement", type=Path)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 4:
        parser.error("jobs must be 1..4 under the approved plan")
    pilot_dir = project_path(args.pilot_dir)
    calibration = json.loads(project_path(args.concurrency_measurement).read_text()) if args.concurrency_measurement else None
    result = review(pilot_dir, project_path(args.main_config), args.jobs, calibration)
    path = pilot_dir / "pilot-review.json"
    path.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n")
    write_csv(pilot_dir / "pilot-review-cells.csv", result["cells"])
    print(json.dumps({key: value for key, value in result.items() if key not in {"cells", "run_cost_estimates", "pilot_manifest"}}, indent=2))
    print(f"Review retained at {path}")


if __name__ == "__main__":
    main()
