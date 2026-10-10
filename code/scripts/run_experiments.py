#!/usr/bin/env python3
"""One editable, concurrent TSVRA batch runner; Python standard library only.

Edit code/config/experiments.example.json, then run python3 code/scripts/run_experiments.py. Each run keeps
its simulator CSVs, power trace, exact configuration, and log. Combined reports
are refreshed as runs finish. Relative paths are relative to the repository.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import itertools
import json
import math
import os
import platform
import re
import shutil
import signal
import statistics
import subprocess
import sys
import threading
import time
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[2]
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
    "In Flight At End": "in_flight_at_end", "Router Count": "router_count",
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
    "total_requests", "completed_requests", "failed_requests", "in_flight_at_horizon", "in_flight_at_end", "router_count",
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
                if process.returncode is None:
                    stop_process(process)


def stop_process(process, force=False):
    """Terminate the full simulator process group, including resource wrappers."""
    try:
        if os.name == "posix":
            os.killpg(process.pid, signal.SIGKILL if force else signal.SIGTERM)
        elif force:
            process.kill()
        else:
            process.terminate()
    except ProcessLookupError:
        pass


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_hash(value) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                     allow_nan=False).encode()).hexdigest()


def write_json(path: Path, value) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def project_path(value) -> Path:
    path = Path(value)
    return path.resolve() if path.is_absolute() else (PROJECT / path).resolve()


def default_binary() -> Path:
    executable = "tsvra.exe" if os.name == "nt" else "tsvra"
    candidates = [PROJECT / "code" / "build" / "bin" / executable]
    if os.name == "nt":
        candidates.append(PROJECT / "code" / "build" / "windows-msvc" / "bin" / executable)
        candidates.append(PROJECT / "code" / "build" / "mingw" / "bin" / executable)
    return next((path for path in candidates if path.is_file()), candidates[0])


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


def validate_parameters(parameters: dict) -> None:
    for key in ("num_layers", "grid_factor", "simulation_cycles", "vertical_delay", "horizontal_delay", "hotspot_count"):
        if key in parameters:
            minimum = 2 if key == "num_layers" else 1
            value = parameters[key]
            if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
                raise ValueError(f"{key} must be an integer >= {minimum}")
    for key in ("router_delay", "max_path_length", "max_horizontal_distance"):
        if key in parameters and (not isinstance(parameters[key], int) or parameters[key] < 0):
            raise ValueError(f"{key} must be a nonnegative integer")
    for key in ("failure_rate", "initial_failure_rate", "runtime_failure_rate", "request_probability", "reliability_min"):
        if key in parameters and (not isinstance(parameters[key], (int, float)) or not 0 <= parameters[key] <= 1):
            raise ValueError(f"{key} must be in [0,1]")
    for key in ("lambda1", "lambda2", "hotspot_radius", "hotspot_multiplier"):
        if key in parameters:
            minimum = 1 if key == "hotspot_multiplier" else 0
            if not isinstance(parameters[key], (int, float)) or parameters[key] < minimum or (
                    key == "hotspot_radius" and parameters[key] == 0):
                raise ValueError(f"Invalid {key}")
    for key, choices in (("failure_mode", {"a", "b", "c"}), ("failure_model", {"uniform", "hotspot"}),
                         ("redundancy", {"shared", "corner4", "none"})):
        if key in parameters and parameters[key] not in choices:
            raise ValueError(f"Invalid {key}")
    if parameters.get("redundancy") == "shared" and parameters.get("grid_factor", 4) < 3:
        raise ValueError("shared redundancy requires grid_factor >= 3")
    centers = parameters.get("hotspot_centers", [])
    if isinstance(centers, list):
        size, layers = 4 * parameters.get("grid_factor", 4), parameters.get("num_layers", 4)
        if any(not (0 <= x < size and 0 <= y < size and 0 <= z < layers - 1) for x, y, z in centers):
            raise ValueError("hotspot_centers must be within the grid and lower interfaces")


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
    task_settings = set()
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
                isinstance(seed, bool) or not isinstance(seed, int) or not 1 <= seed <= 0xFFFFFFFF for seed in seeds):
            raise ValueError("seeds must be a nonempty array of nonzero uint32 integers")
        if len(seeds) != len(set(seeds)):
            raise ValueError(f"{name} seeds must be unique within each cell")
        repeats = positive_integer(configuration.get("repetitions", spec.get("repetitions", len(seeds))), "repetitions")
        if repeats > len(seeds):
            raise ValueError(f"{name} needs {repeats} seeds; only {len(seeds)} supplied")
        slug = re.sub(r"[^a-zA-Z0-9_-]+", "_", name).strip("_")[:50] or "configuration"
        keys = list(matrix)
        for variant_index, values in enumerate(itertools.product(*(matrix[key] for key in keys)), 1):
            selected = {**parameters, **dict(zip(keys, values))}
            validate_parameters(selected)
            variant = f"c{index:03d}_{slug}_v{variant_index:03d}"
            for repeat, seed in enumerate(seeds[:repeats], 1):
                settings = canonical_hash({"parameters": selected, "seed": seed})
                if settings in task_settings:
                    raise ValueError(f"Duplicate settings/seed cell in {name}")
                task_settings.add(settings)
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


def resolved_parameters(task: Task, prefix: Path) -> dict:
    result = {**task.parameters, "random_seed": task.seed, "output_prefix": str(prefix)}
    if "power_config" in result:
        result["power_config"] = str(project_path(result["power_config"]))
    return result


def capture(command: list[str]) -> str:
    try:
        result = subprocess.run(command, cwd=PROJECT, capture_output=True, text=True, timeout=10)
        return result.stdout.strip() if result.returncode == 0 else f"Unavailable: {result.stderr.strip()}"
    except (OSError, subprocess.SubprocessError) as error:
        return f"Unavailable: {error}"


def provenance(binary: Path, tasks: list[Task]) -> dict:
    source_paths = ["code/backend", "code/scripts", "code/config", "code/frontend"]
    tracked = capture(["git", "ls-files", "--", *source_paths]).splitlines()
    untracked = capture(["git", "ls-files", "--others", "--exclude-standard", "--", *source_paths]).splitlines()
    hashes = {name: file_hash(PROJECT / name) for name in sorted(set(tracked + untracked))
              if (PROJECT / name).is_file()}
    diff = capture(["git", "diff", "--binary", "HEAD", "--", *source_paths])
    power_files = sorted({str(project_path(task.parameters["power_config"])) for task in tasks
                          if "power_config" in task.parameters})
    memory = {}
    cpu_model = platform.processor()
    try:
        for line in Path("/proc/meminfo").read_text().splitlines():
            if line.startswith(("MemTotal:", "MemAvailable:")):
                key, value, _ = line.split()
                memory[key.rstrip(":").lower() + "_kb"] = int(value)
    except OSError:
        pass
    try:
        cpu_model = next(line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines()
                         if line.startswith("model name"))
    except (OSError, StopIteration):
        pass
    compiler = "c++"
    cache = binary.parents[1] / "CMakeCache.txt"
    build_settings = {}
    if cache.is_file():
        for line in cache.read_text(errors="replace").splitlines():
            if line.startswith(("CMAKE_CXX_COMPILER:", "CMAKE_BUILD_TYPE:", "CMAKE_CXX_FLAGS:")):
                key, value = line.split("=", 1)
                build_settings[key.split(":")[0]] = value
        compiler = build_settings.get("CMAKE_CXX_COMPILER", compiler)
    return {"captured_at": utc_now(), "git_revision": capture(["git", "rev-parse", "HEAD"]),
            "git_status": capture(["git", "status", "--porcelain=v1"]),
            "git_diff_sha256": hashlib.sha256(diff.encode()).hexdigest(), "git_diff": diff,
            "git_source_sha256": canonical_hash(hashes), "source_file_sha256": hashes,
            "untracked_source_files": [name for name in untracked if name in hashes],
            "binary": str(binary), "binary_sha256": file_hash(binary) if binary.is_file() else None,
            "power_files_sha256": {name: file_hash(Path(name)) for name in power_files},
            "toolchain": {"python": sys.version, "compiler": capture([compiler, "--version"]),
                          "cmake": capture(["cmake", "--version"]), "node": capture(["node", "--version"]),
                          "npm": capture(["npm", "--version"]),
                          "build_settings": build_settings},
            "machine": {"hostname": platform.node(), "platform": platform.platform(),
                        "uname": list(platform.uname()), "cpu_model": cpu_model, "cpu_count": os.cpu_count(), **memory}}


def make_manifest(spec: dict, tasks: list[Task], binary: Path, output: Path, jobs: int, timeout) -> dict:
    planned = []
    for task in tasks:
        prefix = output / "runs" / task.run_id
        command = command_for(binary, prefix, task)
        resolved = resolved_parameters(task, prefix)
        planned.append({"run_id": task.run_id, "configuration": task.configuration, "variant": task.variant,
                        "repeat": task.repeat, "seed": task.seed, "parameters": task.parameters,
                        "resolved_parameters": resolved, "output_prefix": str(prefix), "command": command,
                        "configuration_sha256": canonical_hash(resolved), "command_sha256": canonical_hash(command)})
    return {"schema_version": 1, "batch_configuration_sha256": canonical_hash(spec), "batch": spec,
            "started_at": utc_now(), "finished_at": None, "status": "planned", "jobs": jobs,
            "timeout_seconds": timeout, "planned_runs": len(tasks), "output_dir": str(output),
            "tasks": planned, "provenance": provenance(binary, tasks)}


def reserve_output(output: Path) -> None:
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"Output directory is not empty: {output}; choose a fresh output_dir")
    output.mkdir(parents=True, exist_ok=True)
    try:
        with (output / ".batch.lock").open("x", encoding="utf-8") as handle:
            handle.write(f"pid={os.getpid()} started_at={utc_now()}\n")
    except FileExistsError:
        raise ValueError(f"Another batch already reserved output directory: {output}") from None
    (output / "runs").mkdir()


def wait_process(process, timeout, row: dict) -> bool:
    """Wait once per child, preserving exact per-job resource usage on POSIX."""
    if not hasattr(os, "wait4"):
        row["resource_measurement"] = "unavailable"
        try:
            row["return_code"] = process.wait(timeout=timeout)
            return False
        except subprocess.TimeoutExpired:
            stop_process(process, force=True)
            row["return_code"] = process.wait()
            return True
    deadline = time.monotonic() + timeout if timeout is not None else None
    timed_out = False
    while True:
        pid, status, usage = os.wait4(process.pid, os.WNOHANG)
        if pid:
            process.returncode = os.waitstatus_to_exitcode(status)
            row.update(return_code=process.returncode, resource_measurement="wait4_per_child",
                       peak_rss_kib=int(usage.ru_maxrss / 1024 if sys.platform == "darwin" else usage.ru_maxrss),
                       cpu_user_seconds=usage.ru_utime, cpu_system_seconds=usage.ru_stime)
            return timed_out
        if deadline is not None and time.monotonic() >= deadline and not timed_out:
            stop_process(process, force=True)
            timed_out = True
        time.sleep(.005)


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
    required = {"actual_seed", "total_requests", "completed_requests", "failed_requests", "in_flight_at_end",
                "in_flight_at_horizon", "router_count", "elapsed_cycles",
                "elapsed_seconds", "router_events", "horizontal_hops", "vertical_hops", "redundant_vertical_hops",
                "router_static_mw", "router_dynamic_mw", "horizontal_mw", "vertical_mw", "total_mw", "total_energy_pj",
                "router_energy_pj", "horizontal_energy_pj", "vertical_energy_pj"}
    if any(result.get(key) is None for key in required):
        raise ValueError(f"Missing required simulator/power metrics in {path}; build the current simulator")
    return result


def close(actual, expected, label):
    if not math.isfinite(actual) or not math.isclose(actual, expected, rel_tol=1e-8, abs_tol=1e-8):
        raise ValueError(f"Inconsistent {label}: {actual} != {expected}")


def check_outputs(prefix: Path, task: Task | None = None) -> dict:
    summary = parse_summary(Path(f"{prefix}_summary.csv"))
    configuration = json.loads(Path(f"{prefix}_config.json").read_text(encoding="utf-8"))
    if configuration["actual_seed"] != summary["actual_seed"]:
        raise ValueError("Actual seed differs between summary and exported configuration")
    simulation = configuration["simulation"]
    if task is not None:
        if summary["actual_seed"] != task.seed or simulation["random_seed"] != task.seed:
            raise ValueError("Actual/exported seed differs from requested seed")
        for key, value in task.parameters.items():
            actual = simulation[key]
            if key == "power_config":
                value = str(project_path(value))
            elif key == "hotspot_centers":
                if isinstance(value, list):
                    actual = [[point["x"], point["y"], point["z"]] for point in actual]
                else:
                    actual = ";".join(",".join(str(point[axis]) for axis in ("x", "y", "z")) for point in actual)
            if actual != value:
                raise ValueError(f"Exported setting differs from requested {key}: {actual} != {value}")
    if summary["in_flight_at_end"] != 0:
        raise ValueError("Partial run: In Flight At End must equal zero")
    if summary["total_requests"] != summary["completed_requests"] + summary["failed_requests"]:
        raise ValueError("Generated requests do not equal completed plus failed requests")
    horizon = simulation["simulation_cycles"]
    if summary["elapsed_cycles"] < horizon:
        raise ValueError("Partial run: elapsed cycles precede generation horizon")
    last = None
    with Path(f"{prefix}_power.csv").open(encoding="utf-8", newline="") as handle:
        for last in csv.DictReader(handle):
            pass
    if last is None:
        raise ValueError("Power trace is empty")
    for key in ("elapsed_cycles", "elapsed_seconds", "router_events", "horizontal_hops", "vertical_hops",
                "redundant_vertical_hops", "router_static_mw", "router_dynamic_mw", "horizontal_mw",
                "vertical_mw", "total_mw", "total_energy_pj"):
        expected = summary[key]
        actual = int(last[key]) if key in INTEGER_METRICS else float(last[key])
        if not math.isfinite(actual) or not math.isclose(actual, expected, rel_tol=1e-8, abs_tol=1e-8):
            raise ValueError(f"Final power trace disagrees with summary: {key}")
    counts = {"Completed": 0, "Failed": 0}
    latencies = []
    completed_horizontal = 0
    total_horizontal = 0
    request_ids = set()
    with Path(f"{prefix}_requests.csv").open(encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        expected_header = ["Request ID", "Start X", "Start Y", "Start Z", "End X", "End Y", "End Z",
                           "Generate Time", "Complete Time", "Latency", "Horizontal Distance", "Status"]
        if reader.fieldnames != expected_header:
            raise ValueError("Unexpected request CSV header")
        for request in reader:
            identifier = int(request["Request ID"])
            if identifier < 0 or identifier in request_ids:
                raise ValueError("Duplicate or invalid terminal request ID")
            request_ids.add(identifier)
            generated_at = int(request["Generate Time"])
            horizontal = int(request["Horizontal Distance"])
            if not 0 <= generated_at < horizon or horizontal < 0:
                raise ValueError("Invalid request generation time or horizontal distance")
            size, layers = simulation["grid_size"], simulation["num_layers"]
            for endpoint in ("Start", "End"):
                x, y, z = (int(request[f"{endpoint} {axis}"]) for axis in ("X", "Y", "Z"))
                if not (0 <= x < size and 0 <= y < size and 0 <= z < layers):
                    raise ValueError("Request endpoint lies outside grid")
            total_horizontal += horizontal
            status = request["Status"]
            if status not in counts:
                raise ValueError(f"Unexpected terminal request status: {status}")
            counts[status] += 1
            if status == "Completed":
                completed_at, latency = int(request["Complete Time"]), int(request["Latency"])
                if not generated_at <= completed_at <= summary["elapsed_cycles"] or latency != completed_at - generated_at:
                    raise ValueError("Request latency disagrees with completion/generation timestamps")
                latencies.append(latency)
                completed_horizontal += horizontal
            elif request["Complete Time"] not in ("", "N/A") or request["Latency"] not in ("", "N/A"):
                raise ValueError("Failed request completion/latency must be undefined")
    completed = summary["completed_requests"]
    if counts["Completed"] != completed or counts["Failed"] != summary["failed_requests"]:
        raise ValueError("Request CSV counts disagree with summary")
    if total_horizontal != summary["total_horizontal_distance"] or total_horizontal != summary["horizontal_hops"]:
        raise ValueError("Request horizontal distances disagree with summary/launch counters")
    total_latency = sum(latencies)
    if completed:
        close(summary["average_latency_cycles"], total_latency / completed, "completed latency")
        if summary["min_latency_cycles"] != min(latencies) or summary["max_latency_cycles"] != max(latencies):
            raise ValueError("Summary minimum/maximum latency disagrees with requests")
    elif any(summary.get(key) is not None for key in ("average_latency_cycles", "min_latency_cycles", "max_latency_cycles")):
        raise ValueError("No-completion latency must be undefined")
    summary["total_completed_latency_cycles"] = total_latency
    summary["average_completed_horizontal_distance"] = completed_horizontal / completed if completed else None
    summary["average_latency_cycles"] = total_latency / completed if completed else None
    summary["median_latency_cycles"] = statistics.median(latencies) if latencies else None
    summary["p95_latency_cycles"] = sorted(latencies)[math.ceil(.95 * completed) - 1] if completed else None
    summary["drain_cycles"] = summary["elapsed_cycles"] - horizon
    generated = summary["total_requests"]
    terminal = completed + summary["failed_requests"]
    for key, expected_ratio in (("success_rate_pct", 100 * completed / generated if generated else None),
                                ("terminal_delivery_rate_pct", 100 * completed / terminal if terminal else None),
                                ("route_failure_rate_pct", 100 * summary["failed_requests"] / generated if generated else None),
                                ("energy_per_completed_flit_pj", summary["total_energy_pj"] / completed if completed else None)):
        if expected_ratio is not None:
            close(summary[key], expected_ratio, key)
        elif summary.get(key) is not None:
            raise ValueError(f"Undefined ratio exported as a value: {key}")
    summary["success_rate_pct"] = 100 * completed / generated if generated else None
    summary["terminal_delivery_rate_pct"] = 100 * completed / terminal if terminal else None
    summary["route_failure_rate_pct"] = 100 * summary["failed_requests"] / generated if generated else None
    summary["energy_per_completed_flit_pj"] = summary["total_energy_pj"] / completed if completed else None
    if summary["router_events"] != summary["horizontal_hops"] + summary["vertical_hops"]:
        raise ValueError("Power router/link event counters are inconsistent")
    if summary["redundant_vertical_hops"] > summary["vertical_hops"]:
        raise ValueError("Redundant hop subset exceeds total vertical hops")
    power = configuration["power_parameters"]
    router_count = simulation["grid_size"] ** 2 * simulation["num_layers"]
    if summary["router_count"] != router_count:
        raise ValueError("Router population disagrees with geometry")
    channel_count = simulation["grid_size"] ** 2 * (simulation["num_layers"] - 1)
    if not 0 <= summary["failed_redundant_tsvs"] <= summary["failed_tsvs"] <= channel_count:
        raise ValueError("Failed-channel populations violate subset/geometry bounds")
    seconds = summary["elapsed_cycles"] / power["frequency_hz"]
    close(summary["elapsed_seconds"], seconds, "elapsed simulated seconds")
    router_event = sum(power[key] for key in ("router_buffer_event_pj", "router_crossbar_event_pj", "router_control_event_pj"))
    # fF * V^2 gives femtojoules; convert to pJ independently of exported derived energies.
    link_factor = .5 * power["voltage_v"] ** 2 * power["total_toggle_probability"] * .001
    horizontal_event = power["horizontal_link_length_um"] * power["horizontal_capacitance_ff_per_um"] * power["flit_bits"] * link_factor
    vertical_event = power["tsv_capacitance_ff"] * power["physical_signal_tsvs"] * link_factor
    idle_mw = router_count * power["router_idle_mw"]
    static_energy = idle_mw * seconds * 1e9
    dynamic_energy = summary["router_events"] * router_event
    horizontal_energy = summary["horizontal_hops"] * horizontal_event
    vertical_energy = (summary["vertical_hops"] - summary["redundant_vertical_hops"]) * vertical_event + summary["redundant_vertical_hops"] * vertical_event * power["redundant_vertical_capacitance_multiplier"]
    expected = {"router_energy_pj": static_energy + dynamic_energy, "horizontal_energy_pj": horizontal_energy,
                "vertical_energy_pj": vertical_energy, "total_energy_pj": static_energy + dynamic_energy + horizontal_energy + vertical_energy,
                "router_static_mw": idle_mw, "router_dynamic_mw": dynamic_energy / (seconds * 1e9),
                "horizontal_mw": horizontal_energy / (seconds * 1e9), "vertical_mw": vertical_energy / (seconds * 1e9)}
    expected["total_mw"] = expected["total_energy_pj"] / (seconds * 1e9)
    for key, value in expected.items():
        close(summary[key], value, f"independently recomputed {key}")
    summary["router_static_energy_pj"] = static_energy
    summary["router_dynamic_energy_pj"] = dynamic_energy
    summary["dynamic_energy_pj"] = dynamic_energy + horizontal_energy + vertical_energy
    summary["channel_count"] = channel_count
    summary["observed_failure_fraction"] = summary["failed_tsvs"] / channel_count
    row = {**summary, **{f"parameter.{key}": value for key, value in simulation.items()}}
    row["actual_hotspot_centers"] = configuration.get("actual_hotspot_centers", [])
    row["exported_configuration_sha256"] = file_hash(Path(f"{prefix}_config.json"))
    row["effective_parameters"] = simulation
    row["power_parameters"] = power
    for key in ("parameter_version", "calibration_status", "technology", "calibration_source_revision", "router_source"):
        row[f"power_{key}"] = power[key]
    return row


def run_task(binary: Path, runs_dir: Path, task: Task, timeout, control: RunControl) -> dict:
    prefix = runs_dir / task.run_id
    row = {"configuration": task.configuration, "variant": task.variant, "run_id": task.run_id,
           "repeat": task.repeat, "seed": task.seed, "requested_seed": task.seed, "actual_seed": None,
           "parameters": task.parameters, "output_prefix": str(prefix), "started_at": utc_now(),
           "status": "failed", "error": "", **{f"parameter.{key}": value for key, value in task.parameters.items()}}
    started = time.monotonic()
    try:
        if control.cancelled.is_set():
            row["status"] = "cancelled"
            raise RuntimeError("Batch cancelled before this run started")
        if list(runs_dir.glob(task.run_id + "*")):
            row["output_collision"] = True
            raise RuntimeError("Existing run files would be replaced; choose a fresh output_dir")
        command = command_for(binary, prefix, task)
        row["command"] = command
        row["command_sha256"] = canonical_hash(command)
        row["configuration_sha256"] = canonical_hash(resolved_parameters(task, prefix))
        if os.name == "posix" and shutil.which("stdbuf"):
            command = ["stdbuf", "-oL", "-eL", *command]
        with Path(f"{prefix}.log").open("x", encoding="utf-8") as log:
            log.write("Command: " + json.dumps(command) + "\n")
            log.flush()
            row["execution_command"] = command
            process = subprocess.Popen(command, cwd=PROJECT, stdout=log, stderr=subprocess.STDOUT,
                                       start_new_session=os.name == "posix")
            with control.lock:
                control.processes[task.run_id] = process
                if control.cancelled.is_set():
                    stop_process(process)
            try:
                timed_out = wait_process(process, timeout, row)
            finally:
                with control.lock:
                    control.processes.pop(task.run_id, None)
        if timed_out:
            row["status"] = "timeout"
            raise RuntimeError(f"Simulator exceeded timeout_seconds={timeout}")
        if control.cancelled.is_set():
            row["status"] = "cancelled"
            raise RuntimeError("Batch cancelled while simulator was running")
        if row["return_code"] != 0:
            raise RuntimeError(f"Simulator exited with code {row['return_code']}; see {prefix}.log")
        row.update(check_outputs(prefix, task))
        row["status"] = "ok"
    except (OSError, ValueError, KeyError, TypeError, RuntimeError) as error:
        row["error"] = str(error)
    row["wall_seconds"] = time.monotonic() - started
    row["finished_at"] = utc_now()
    row["output_bytes"] = sum(path.stat().st_size for path in runs_dir.glob(task.run_id + "*") if path.is_file())
    # A run-level record is durable even if the supervisor stops between report refreshes.
    if not row.get("output_collision"):
        write_json(Path(f"{prefix}_result.json"), row)
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
                  "missing_runs": len(expected) - len(all_runs),
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
    write_json(output / "results.json", {"schema_version": 1, "runs": rows})
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
    parser.add_argument("--config", type=Path, default=PROJECT / "code" / "config" / "experiments.example.json")
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--jobs", type=int)
    parser.add_argument("--timeout-seconds", type=float, help="Override the timeout using measured pilot cost")
    parser.add_argument("--manifest", type=Path, help="Save the resolved manifest during a dry run")
    parser.add_argument("--dry-run", action="store_true", help="Validate settings and print simulator argument arrays")
    args = parser.parse_args()
    try:
        spec = json.loads(project_path(args.config).read_text(encoding="utf-8"))
        tasks = build_tasks(spec)
        if args.binary is not None:
            binary = project_path(args.binary)
        elif "binary" in spec:
            binary = project_path(spec["binary"])
        else:
            binary = default_binary()
        output = project_path(args.output_dir or spec.get("output_dir", "code/output/experiments"))
        jobs = positive_integer(args.jobs if args.jobs is not None else spec.get("jobs", min(4, os.cpu_count() or 1)), "jobs")
        timeout = args.timeout_seconds if args.timeout_seconds is not None else spec.get("timeout_seconds")
        if timeout is not None and (isinstance(timeout, bool) or not isinstance(timeout, (int, float))
                                    or not math.isfinite(timeout) or timeout <= 0):
            raise ValueError("timeout_seconds must be positive and finite, or null")
        runs_dir = output / "runs"
        if args.dry_run:
            if args.manifest is not None:
                path = project_path(args.manifest)
                if path.exists():
                    raise ValueError(f"Dry-run manifest already exists: {path}")
                path.parent.mkdir(parents=True, exist_ok=True)
                write_json(path, make_manifest(spec, tasks, binary, output, jobs, timeout))
            for task in tasks:
                print(json.dumps(command_for(binary, runs_dir / task.run_id, task)))
            print(f"{len(tasks)} runs; jobs={jobs}; reports={output}")
            return 0
        if not binary.is_file():
            raise ValueError(f"Simulator binary does not exist: {binary}")
        manifest = make_manifest(spec, tasks, binary, output, jobs, timeout)
        reserve_output(output)
        write_json(output / "batch.json", spec)
        manifest["status"] = "running"
        write_json(output / "manifest.json", manifest)
        (output / "source.diff").write_text(manifest["provenance"]["git_diff"], encoding="utf-8")
        write_json(output / "results.json", {"schema_version": 1, "runs": []})
        control = RunControl()
        rows = []
        interrupted = False
        with (output / "results.jsonl").open("x", encoding="utf-8") as journal, ThreadPoolExecutor(max_workers=jobs) as executor:
            futures = {executor.submit(run_task, binary, runs_dir, task, timeout, control): task for task in tasks}
            pending = set(futures)
            next_progress = 0.0
            next_reports = time.monotonic() + 5.0
            def record(row):
                rows.append(row)
                journal.write(json.dumps(row, allow_nan=False, separators=(",", ":")) + "\n")
                journal.flush()
                print(f"[{len(rows)}/{len(tasks)}] {row['run_id']}: {row['status']}", flush=True)
                if row["error"]:
                    print(row["error"], file=sys.stderr, flush=True)
            try:
                while pending:
                    done, pending = wait(pending, timeout=1.0, return_when=FIRST_COMPLETED)
                    for future in done:
                        record(future.result())
                    if done and time.monotonic() >= next_reports:
                        write_reports(output, tasks, rows)
                        next_reports = time.monotonic() + 5.0
                    if time.monotonic() >= next_progress:
                        progress(control, len(rows), len(tasks), runs_dir)
                        next_progress = time.monotonic() + 5.0
            except KeyboardInterrupt:
                control.cancel()
                interrupted = True
                recorded_ids = {row["run_id"] for row in rows}
                for future, task in futures.items():
                    if task.run_id not in recorded_ids:
                        record(future.result())
                print("Batch cancelled; completed reports and individual run files retained.", file=sys.stderr)
        write_reports(output, tasks, rows)
        manifest["finished_at"] = utc_now()
        manifest["status"] = "cancelled" if interrupted else "complete" if all(row["status"] == "ok" for row in rows) else "failed"
        manifest["run_status_counts"] = {status: sum(row["status"] == status for row in rows)
                                         for status in ("ok", "failed", "timeout", "cancelled")}
        manifest["accepted_runs"] = manifest["run_status_counts"]["ok"]
        manifest["missing_runs"] = len(tasks) - len(rows)
        manifest["output_bytes"] = sum(path.stat().st_size for path in output.rglob("*") if path.is_file())
        write_json(output / "manifest.json", manifest)
        print(f"Reports: {output / 'runs.csv'}, {output / 'summary.csv'}, {output / 'summary.md'}")
        if interrupted:
            return 130
        return 0 if all(row["status"] == "ok" for row in rows) else 1
    except (OSError, ValueError, TypeError, KeyError, subprocess.SubprocessError) as error:
        print(f"Batch error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
