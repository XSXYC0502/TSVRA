#!/usr/bin/env python3
"""One editable, concurrent TSVRA batch runner; Python standard library only.

Edit experiments/example.json, then run python3 experiments.py. Each run keeps
its simulator CSVs, power trace, exact configuration, and log. Combined reports
are refreshed as runs finish. Relative paths are relative to the repository.
"""

from __future__ import annotations

import argparse
import csv
import itertools
import json
import math
import os
import re
import shutil
import statistics
import subprocess
import sys
import threading
import time
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
from dataclasses import dataclass
from pathlib import Path

PROJECT = Path(__file__).resolve().parent
OPTIONS = {
    "num_layers": "--layers", "grid_factor": "--grid-factor",
    "failure_mode": "--failure-mode", "failure_rate": "--failure-rate",
    "initial_failure_rate": "--initial-failure-rate", "runtime_failure_rate": "--runtime-failure-rate",
    "failure_model": "--failure-model", "redundancy": "--redundancy",
    "vertical_delay": "--vertical-delay", "horizontal_delay": "--horizontal-delay",
    "router_delay": "--router-delay", "simulation_cycles": "--cycles",
    "request_probability": "--request-probability", "power_config": "--power-config",
    "lambda1": "--lambda1", "lambda2": "--lambda2",
    "max_path_length": "--max-path-length", "max_horizontal_distance": "--max-horizontal-distance",
    "reliability_min": "--reliability-min", "hotspot_count": "--hotspot-count",
    "hotspot_radius": "--hotspot-radius", "hotspot_multiplier": "--hotspot-multiplier",
    "hotspot_centers": "--hotspot-centers",
}
METRICS = {
    "Total Requests": "total_requests", "Completed Requests": "completed_requests",
    "Failed Requests": "failed_requests", "In Flight At Horizon": "in_flight_at_horizon",
    "Success Rate (%)": "success_rate_pct", "Terminal Delivery Rate (%)": "terminal_delivery_rate_pct",
    "Route Failure Rate (%)": "route_failure_rate_pct", "Average Latency": "average_latency_cycles",
    "Min Latency": "min_latency_cycles", "Max Latency": "max_latency_cycles",
    "Average Horizontal Distance": "average_horizontal_distance",
    "Total Horizontal Distance": "total_horizontal_distance",
    "Failed TSVs": "failed_tsvs", "Failed Redundant TSVs": "failed_redundant_tsvs",
    "Redundant TSV Usages": "redundant_tsv_usages", "Failed Vertical Encounters": "failed_vertical_encounters",
    "Spare Found": "spare_found", "Spare Unavailable": "spare_unavailable", "Actual Seed": "actual_seed",
    "Elapsed Cycles": "elapsed_cycles", "Elapsed Seconds": "elapsed_seconds",
    "Router Events": "router_events", "Horizontal Hops": "horizontal_hops",
    "Vertical Hops": "vertical_hops", "Redundant Vertical Hops": "redundant_vertical_hops",
    "Router Static Power (mW)": "router_static_mw", "Router Dynamic Power (mW)": "router_dynamic_mw",
    "Horizontal Power (mW)": "horizontal_mw", "Vertical Power (mW)": "vertical_mw",
    "Total Power (mW)": "total_mw", "Total Energy (pJ)": "total_energy_pj",
    "Router Energy (pJ)": "router_energy_pj", "Horizontal Energy (pJ)": "horizontal_energy_pj",
    "Vertical Energy (pJ)": "vertical_energy_pj", "Energy Per Completed Flit (pJ)": "energy_per_completed_flit_pj",
}
INTEGER_METRICS = {
    "total_requests", "completed_requests", "failed_requests", "in_flight_at_horizon",
    "min_latency_cycles", "max_latency_cycles", "total_horizontal_distance", "failed_tsvs",
    "failed_redundant_tsvs", "redundant_tsv_usages", "failed_vertical_encounters", "spare_found",
    "spare_unavailable", "actual_seed", "elapsed_cycles", "router_events", "horizontal_hops",
    "vertical_hops", "redundant_vertical_hops",
}


@dataclass(frozen=True)
class Task:
    configuration: str
    variant: str
    repeat: int
    seed: int
    parameters: dict
    run_id: str


class RunControl:
    def __init__(self):
        self.lock = threading.Lock()
        self.processes = {}
        self.cancelled = threading.Event()

    def cancel(self):
        self.cancelled.set()
        with self.lock:
            for process in self.processes.values():
                if process.poll() is None:
                    process.terminate()


def project_path(value) -> Path:
    path = Path(value)
    return path.resolve() if path.is_absolute() else (PROJECT / path).resolve()


def positive_integer(value, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise ValueError(f"{name} must be a positive integer")
    return value


def parameter_map(value, name: str) -> dict:
    if not isinstance(value, dict):
        raise ValueError(f"{name} must be an object")
    unknown = set(value) - set(OPTIONS)
    if unknown:
        raise ValueError(f"Unknown simulation parameters in {name}: {', '.join(sorted(unknown))}")
    for key, parameter in value.items():
        if key == "hotspot_centers" and isinstance(parameter, list):
            if any(not isinstance(point, list) or len(point) != 3
                   or any(isinstance(c, bool) or not isinstance(c, int) for c in point) for point in parameter):
                raise ValueError("hotspot_centers must contain [x,y,lower_interface_z] integer triples")
        elif isinstance(parameter, bool) or not isinstance(parameter, (str, int, float)):
            raise ValueError(f"{key} requires a scalar value")
        if isinstance(parameter, float) and not math.isfinite(parameter):
            raise ValueError(f"{key} must be finite")
    return value


def build_tasks(spec: dict) -> list[Task]:
    if not isinstance(spec, dict):
        raise ValueError("Batch configuration must be an object")
    known = {"binary", "output_dir", "jobs", "repetitions", "seeds", "timeout_seconds", "defaults", "configurations"}
    if set(spec) - known:
        raise ValueError(f"Unknown batch settings: {', '.join(sorted(set(spec) - known))}")
    defaults = parameter_map(spec.get("defaults", {}), "defaults")
    configurations = spec.get("configurations")
    if not isinstance(configurations, list) or not configurations:
        raise ValueError("configurations must be a nonempty array")
    tasks = []
    names = set()
    for index, configuration in enumerate(configurations, 1):
        if not isinstance(configuration, dict):
            raise ValueError("Each configuration must be an object")
        unknown = set(configuration) - {"name", "parameters", "matrix", "repetitions", "seeds"}
        if unknown:
            raise ValueError(f"Unknown configuration settings: {', '.join(sorted(unknown))}")
        name = configuration.get("name")
        if not isinstance(name, str) or not name.strip() or name in names:
            raise ValueError("Every configuration needs a unique nonempty name")
        names.add(name)
        parameters = {**defaults, **parameter_map(configuration.get("parameters", {}), name)}
        matrix = configuration.get("matrix", {})
        if not isinstance(matrix, dict) or set(matrix) - set(OPTIONS):
            raise ValueError(f"{name}.matrix keys must be supported simulation parameters")
        for key, values in matrix.items():
            if not isinstance(values, list) or not values:
                raise ValueError(f"{name}.matrix.{key} must be a nonempty array")
            for value in values:
                parameter_map({key: value}, f"{name}.matrix")
        seeds = configuration.get("seeds", spec.get("seeds", [42]))
        if not isinstance(seeds, list) or not seeds or any(
                isinstance(seed, bool) or not isinstance(seed, int) or not 0 <= seed <= 0xFFFFFFFF for seed in seeds):
            raise ValueError("seeds must be a nonempty array of uint32 integers; 0 requests a random actual seed")
        repeats = positive_integer(configuration.get("repetitions", spec.get("repetitions", len(seeds))), "repetitions")
        if repeats > len(seeds):
            raise ValueError(f"{name} needs {repeats} seeds; only {len(seeds)} supplied")
        slug = re.sub(r"[^a-zA-Z0-9_-]+", "_", name).strip("_")[:50] or "configuration"
        keys = list(matrix)
        for variant_index, values in enumerate(itertools.product(*(matrix[key] for key in keys)), 1):
            selected = {**parameters, **dict(zip(keys, values))}
            variant = f"c{index:03d}_{slug}_v{variant_index:03d}"
            for repeat, seed in enumerate(seeds[:repeats], 1):
                tasks.append(Task(name, variant, repeat, seed, selected, f"{variant}_r{repeat:03d}_s{seed}"))
    return tasks


def command_for(binary: Path, prefix: Path, task: Task) -> list[str]:
    command = [str(binary)]
    # The shorthand sets both phases; emit it before explicit phase overrides,
    # independently of JSON member order and whether it came from a matrix.
    keys = (["failure_rate"] if "failure_rate" in task.parameters else []) + [
        key for key in task.parameters if key != "failure_rate"]
    for key in keys:
        value = task.parameters[key]
        if key == "power_config":
            value = str(project_path(value))
        elif key == "hotspot_centers" and isinstance(value, list):
            if not value:
                continue
            value = ";".join(",".join(str(c) for c in point) for point in value)
        command.extend([OPTIONS[key], str(value)])
    command.extend(["--seed", str(task.seed), "--output", str(prefix)])
    return command


def parse_summary(path: Path) -> dict:
    result = {}
    with path.open(encoding="utf-8", newline="") as handle:
        reader = csv.reader(handle)
        if next(reader, None) != ["Metric", "Value"]:
            raise ValueError(f"Unexpected summary CSV header: {path}")
        for entry in reader:
            if len(entry) != 2:
                raise ValueError(f"Invalid summary CSV row in {path}")
            label, text = entry
            if label not in METRICS:
                continue
            key = METRICS[label]
            if key in result:
                raise ValueError(f"Duplicate summary metric: {label}")
            value = None if not text else int(text) if key in INTEGER_METRICS else float(text)
            if value is not None and (not math.isfinite(value) or value < 0):
                raise ValueError(f"Invalid summary metric: {label}")
            result[key] = value
    required = {"actual_seed", "total_requests", "completed_requests", "failed_requests", "elapsed_cycles",
                "elapsed_seconds", "router_events", "horizontal_hops", "vertical_hops", "redundant_vertical_hops",
                "router_static_mw", "router_dynamic_mw", "horizontal_mw", "vertical_mw", "total_mw", "total_energy_pj"}
    if any(result.get(key) is None for key in required):
        raise ValueError(f"Missing required simulator/power metrics in {path}; build the current simulator")
    return result


def check_outputs(prefix: Path) -> dict:
    summary = parse_summary(Path(f"{prefix}_summary.csv"))
    configuration = json.loads(Path(f"{prefix}_config.json").read_text(encoding="utf-8"))
    if configuration["actual_seed"] != summary["actual_seed"]:
        raise ValueError("Actual seed differs between summary and exported configuration")
    last = None
    with Path(f"{prefix}_power.csv").open(encoding="utf-8", newline="") as handle:
        for last in csv.DictReader(handle):
            pass
    if last is None:
        raise ValueError("Power trace is empty")
    for key in ("elapsed_cycles", "elapsed_seconds", "router_events", "horizontal_hops", "vertical_hops",
                "redundant_vertical_hops", "total_mw", "total_energy_pj"):
        expected = summary[key]
        actual = int(last[key]) if key in INTEGER_METRICS else float(last[key])
        if not math.isfinite(actual) or not math.isclose(actual, expected, rel_tol=1e-8, abs_tol=1e-8):
            raise ValueError(f"Final power trace disagrees with summary: {key}")
    counts = {"Completed": 0, "Failed": 0}
    total_latency = 0
    completed_horizontal = 0
    with Path(f"{prefix}_requests.csv").open(encoding="utf-8", newline="") as handle:
        for request in csv.DictReader(handle):
            status = request["Status"]
            if status not in counts:
                raise ValueError(f"Unexpected terminal request status: {status}")
            counts[status] += 1
            if status == "Completed":
                total_latency += int(request["Latency"])
                completed_horizontal += int(request["Horizontal Distance"])
    completed = summary["completed_requests"]
    if counts["Completed"] != completed or counts["Failed"] != summary["failed_requests"]:
        raise ValueError("Request CSV counts disagree with summary")
    summary["total_completed_latency_cycles"] = total_latency
    summary["average_completed_horizontal_distance"] = completed_horizontal / completed if completed else None
    summary["average_latency_cycles"] = total_latency / completed if completed else None
    generated = summary["total_requests"]
    terminal = completed + summary["failed_requests"]
    summary["success_rate_pct"] = 100 * completed / generated if generated else None
    summary["terminal_delivery_rate_pct"] = 100 * completed / terminal if terminal else None
    summary["route_failure_rate_pct"] = 100 * summary["failed_requests"] / generated if generated else None
    summary["energy_per_completed_flit_pj"] = summary["total_energy_pj"] / completed if completed else None
    if summary["router_events"] != summary["horizontal_hops"] + summary["vertical_hops"]:
        raise ValueError("Power router/link event counters are inconsistent")
    if summary["redundant_vertical_hops"] > summary["vertical_hops"]:
        raise ValueError("Redundant hop subset exceeds total vertical hops")
    row = {**summary, **{f"parameter.{key}": value for key, value in configuration["simulation"].items()}}
    row["actual_hotspot_centers"] = configuration.get("actual_hotspot_centers", [])
    power = configuration["power_parameters"]
    for key in ("parameter_version", "calibration_status", "technology", "calibration_source_revision", "router_source"):
        row[f"power_{key}"] = power[key]
    return row


def run_task(binary: Path, runs_dir: Path, task: Task, timeout, control: RunControl) -> dict:
    prefix = runs_dir / task.run_id
    row = {"configuration": task.configuration, "variant": task.variant, "run_id": task.run_id,
           "repeat": task.repeat, "requested_seed": task.seed, "output_prefix": str(prefix),
           "status": "failed", "error": "", **{f"parameter.{key}": value for key, value in task.parameters.items()}}
    started = time.monotonic()
    try:
        if control.cancelled.is_set():
            raise RuntimeError("Batch cancelled before this run started")
        command = command_for(binary, prefix, task)
        if os.name == "posix" and shutil.which("stdbuf"):
            command = ["stdbuf", "-oL", "-eL", *command]
        with Path(f"{prefix}.log").open("w", encoding="utf-8") as log:
            log.write("Command: " + json.dumps(command) + "\n")
            log.flush()
            process = subprocess.Popen(command, cwd=PROJECT, stdout=log, stderr=subprocess.STDOUT)
            with control.lock:
                control.processes[task.run_id] = process
                if control.cancelled.is_set():
                    process.terminate()
            try:
                row["return_code"] = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                raise RuntimeError(f"Simulator exceeded timeout_seconds={timeout}") from None
            finally:
                with control.lock:
                    control.processes.pop(task.run_id, None)
        if row["return_code"] != 0:
            raise RuntimeError(f"Simulator exited with code {row['return_code']}; see {prefix}.log")
        row.update(check_outputs(prefix))
        row["status"] = "ok"
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        row["error"] = str(error)
    row["wall_seconds"] = time.monotonic() - started
    return row


def csv_value(value):
    return json.dumps(value, separators=(",", ":")) if isinstance(value, (dict, list)) else value


def write_csv(path: Path, rows: list[dict]) -> None:
    fields = list(dict.fromkeys(key for row in rows for key in row))
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows({key: csv_value(value) for key, value in row.items()} for row in rows)
    temporary.replace(path)


def aggregate(tasks: list[Task], rows: list[dict]) -> list[dict]:
    results = []
    for variant in dict.fromkeys(task.variant for task in tasks):
        expected = [task for task in tasks if task.variant == variant]
        all_runs = [row for row in rows if row["variant"] == variant]
        good = [row for row in all_runs if row["status"] == "ok"]
        first = expected[0]
        effective = {key: value for key, value in good[0].items() if key.startswith("parameter.")} if good else {
            f"parameter.{key}": value for key, value in first.parameters.items()}
        # Seed and output path differ across repetitions, so they belong to runs.csv.
        effective.pop("parameter.random_seed", None)
        effective.pop("parameter.output_prefix", None)
        result = {"configuration": first.configuration, "variant": variant, "expected_runs": len(expected),
                  "completed_runs": len(good), "failed_runs": len(all_runs) - len(good),
                  **effective}
        for key in METRICS.values():
            if key == "actual_seed":
                continue
            values = [row[key] for row in good if row.get(key) is not None]
            result[f"mean_{key}"] = statistics.fmean(values) if values else None
            if key in ("success_rate_pct", "total_mw"):
                result[f"stddev_{key}"] = statistics.stdev(values) if len(values) > 1 else None
        completed = sum(row["completed_requests"] for row in good)
        generated = sum(row["total_requests"] for row in good)
        energy = sum(row["total_energy_pj"] for row in good)
        seconds = sum(row["elapsed_seconds"] for row in good)
        result["pooled_success_rate_pct"] = 100 * completed / generated if generated else None
        result["pooled_energy_per_completed_flit_pj"] = energy / completed if completed else None
        result["pooled_total_mw"] = energy / (seconds * 1e9) if seconds else None
        result["actual_seeds"] = [row["actual_seed"] for row in good]
        result["power_calibration_status"] = sorted({row["power_calibration_status"] for row in good})
        results.append(result)
    return results


def markdown_table(rows: list[dict], columns: list[tuple[str, str]]) -> str:
    def display(value):
        if value is None:
            return "—"
        if isinstance(value, float):
            return f"{value:.6g}"
        return str(csv_value(value)).replace("|", "\\|").replace("\n", " ")
    lines = ["| " + " | ".join(title for _, title in columns) + " |",
             "| " + " | ".join("---" for _ in columns) + " |"]
    lines.extend("| " + " | ".join(display(row.get(key)) for key, _ in columns) + " |" for row in rows)
    return "\n".join(lines)


def write_reports(output: Path, tasks: list[Task], rows: list[dict]) -> None:
    order = {task.run_id: index for index, task in enumerate(tasks)}
    rows = sorted(rows, key=lambda row: order[row["run_id"]])
    groups = aggregate(tasks, rows)
    write_csv(output / "runs.csv", rows)
    write_csv(output / "summary.csv", groups)
    good = sum(row["status"] == "ok" for row in rows)
    text = ["# TSVRA experiment results", "", f"Validated runs: {good}/{len(tasks)}; failed runs: {len(rows)-good}.", "",
            "Each request is one 64-bit flit. CSV blanks and — mean undefined, including energy per completed flit when no flit completes.", "",
            "Run means weight repetitions equally. Pooled energy divides all run energy (including idle, failed and in-flight traffic) by all completed flits. Power averages include drain time.", "",
            "## Delivery and failures", "",
            "Failure timing: a = initialization only; b = runtime only; c = initialization and runtime. Probabilities for inactive phases are retained for reproduction.", "",
            markdown_table(groups, [("variant", "Variant"), ("completed_runs", "Runs"),
                ("parameter.num_layers", "Layers"), ("parameter.redundancy", "Layout"),
                ("parameter.failure_model", "Failure model"), ("parameter.failure_mode", "Failure timing"),
                ("parameter.initial_failure_rate", "Initial probability"),
                ("parameter.runtime_failure_rate", "Runtime probability/cycle"),
                ("mean_average_horizontal_distance", "Horizontal distance"), ("mean_success_rate_pct", "Success (%)"),
                ("mean_route_failure_rate_pct", "Route failure (%)"), ("mean_failed_tsvs", "Failed TSVs"),
                ("mean_average_latency_cycles", "Latency (cycles)")]), "", "## Activity and power", "",
            markdown_table(groups, [("variant", "Variant"), ("mean_router_events", "Router events"),
                ("mean_horizontal_hops", "H hops"), ("mean_vertical_hops", "V hops"),
                ("mean_redundant_vertical_hops", "Spare V hops"), ("mean_router_static_mw", "Router idle (mW)"),
                ("mean_total_mw", "Power mean (mW)"), ("stddev_total_mw", "Power SD (mW)"),
                ("pooled_energy_per_completed_flit_pj", "Energy/completed flit (pJ)"),
                ("power_calibration_status", "Calibration")]), "", "## Hotspot settings", "",
            markdown_table(groups, [("variant", "Variant"), ("parameter.hotspot_count", "Count"),
                ("parameter.hotspot_radius", "Radius"), ("parameter.hotspot_multiplier", "Multiplier"),
                ("parameter.hotspot_centers", "Declared centers"), ("actual_seeds", "Actual seeds")]), "",
            "Exact effective parameters and actual hotspot centers are retained in each runs/*_config.json and runs.csv.", ""]
    failures = [row for row in rows if row["status"] != "ok"]
    if failures:
        text.extend(["## Run errors", "", markdown_table(failures, [("run_id", "Run"), ("error", "Error")]), ""])
    temporary = output / "summary.md.tmp"
    temporary.write_text("\n".join(text), encoding="utf-8")
    temporary.replace(output / "summary.md")


def progress(control: RunControl, finished: int, total: int, runs_dir: Path):
    with control.lock:
        running = list(control.processes)
    detail = ""
    if running:
        name = running[0]
        path = runs_dir / f"{name}.log"
        try:
            with path.open("rb") as handle:
                handle.seek(max(0, path.stat().st_size - 8192))
                lines = handle.read().decode("utf-8", errors="replace")
            cycles = re.findall(r"\((\d+)/(\d+)\)", lines)
            detail = f"; {name} cycles={cycles[-1][0]}/{cycles[-1][1]}" if cycles else f"; {name}"
        except OSError:
            pass
    print(f"Batch {finished}/{total}; active={len(running)}{detail}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=PROJECT / "experiments" / "example.json")
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--jobs", type=int)
    parser.add_argument("--dry-run", action="store_true", help="Validate settings and print simulator argument arrays")
    args = parser.parse_args()
    try:
        spec = json.loads(args.config.read_text(encoding="utf-8"))
        tasks = build_tasks(spec)
        binary = project_path(args.binary or spec.get("binary", "bin/tsvra"))
        output = project_path(args.output_dir or spec.get("output_dir", "output/experiments"))
        jobs = positive_integer(args.jobs if args.jobs is not None else spec.get("jobs", min(4, os.cpu_count() or 1)), "jobs")
        timeout = spec.get("timeout_seconds")
        if timeout is not None and (isinstance(timeout, bool) or not isinstance(timeout, (int, float))
                                    or not math.isfinite(timeout) or timeout <= 0):
            raise ValueError("timeout_seconds must be positive and finite, or null")
        runs_dir = output / "runs"
        if args.dry_run:
            for task in tasks:
                print(json.dumps(command_for(binary, runs_dir / task.run_id, task)))
            print(f"{len(tasks)} runs; jobs={jobs}; reports={output}")
            return 0
        if not binary.is_file():
            raise ValueError(f"Simulator binary does not exist: {binary}")
        collisions = [task.run_id for task in tasks if list(runs_dir.glob(task.run_id + "*"))]
        if collisions:
            raise ValueError(f"Existing run files would be replaced({collisions[0]}); choose a fresh output_dir")
        runs_dir.mkdir(parents=True, exist_ok=True)
        (output / "batch.json").write_text(json.dumps(spec, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        control = RunControl()
        rows = []
        with ThreadPoolExecutor(max_workers=jobs) as executor:
            futures = {executor.submit(run_task, binary, runs_dir, task, timeout, control): task for task in tasks}
            pending = set(futures)
            next_progress = 0.0
            try:
                while pending:
                    done, pending = wait(pending, timeout=1.0, return_when=FIRST_COMPLETED)
                    for future in done:
                        row = future.result()
                        rows.append(row)
                        print(f"[{len(rows)}/{len(tasks)}] {row['run_id']}: {row['status']}", flush=True)
                        if row["error"]:
                            print(row["error"], file=sys.stderr, flush=True)
                    if done:
                        write_reports(output, tasks, rows)
                    if time.monotonic() >= next_progress:
                        progress(control, len(rows), len(tasks), runs_dir)
                        next_progress = time.monotonic() + 5.0
            except KeyboardInterrupt:
                control.cancel()
                for future in pending:
                    future.cancel()
                print("Batch cancelled; completed reports and individual run files retained.", file=sys.stderr)
                return 130
        write_reports(output, tasks, rows)
        print(f"Reports: {output / 'runs.csv'}, {output / 'summary.csv'}, {output / 'summary.md'}")
        return 0 if all(row["status"] == "ok" for row in rows) else 1
    except (OSError, ValueError, TypeError, KeyError, subprocess.SubprocessError) as error:
        print(f"Batch error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
