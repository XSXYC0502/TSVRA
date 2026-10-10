#!/usr/bin/env python3
"""Audit raw TSVRA experiment bundles, join sheets, and export whole-seed analysis.

python3 code/scripts/analyze_experiments.py --results-dir PATH --output-dir PATH/analysis
Raw simulator files are authoritative. Any failed/missing/invalid job or mismatched
traffic is retained in the audit and yields exit status 1. CSV blanks/JSON null
denote undefined values. See analysis.json for versions, hashes, and definitions.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import csv
import hashlib
import json
import math
from pathlib import Path
import platform
import statistics
import sys
from typing import Any

import numpy as np

import run_experiments as runner

SCENARIOS = ("U", "H1", "H2", "H12")
BOOTSTRAP_RESAMPLES = 10_000
DEFAULT_RNG_SEED = 20261009
EXTRA_METRICS = {"In Flight At End": "in_flight_at_end", "Router Count": "router_count"}
INT_METRICS = runner.INTEGER_METRICS | set(EXTRA_METRICS.values())
METRICS = (
    "observed_failed_fraction", "success_rate_pct", "average_latency_cycles",
    "median_latency_cycles", "p95_latency_cycles", "total_mw", "router_static_mw",
    "router_dynamic_mw", "horizontal_mw", "vertical_mw", "dynamic_mw",
    "total_mw_per_layer", "total_energy_pj", "energy_per_completed_flit_pj",
    "horizontal_hops", "redundant_vertical_hops", "horizontal_hops_per_generated",
    "spare_hops_per_generated", "average_completed_horizontal_distance",
    "drain_cycles", "total_requests", "completed_requests", "in_flight_at_horizon",
)
POOLED = {
    "success_rate_pct": ("completed_requests", "total_requests", 100.0),
    "average_latency_cycles": ("total_completed_latency_cycles", "completed_requests", 1.0),
    "energy_per_completed_flit_pj": ("total_energy_pj", "completed_requests", 1.0),
}
REQUEST_FIELDS = ["run_id", "requested_seed", "actual_seed", "configured_failure_fraction", "observed_failed_fraction",
                  "fault_scenario", "fault_point_1", "fault_point_2", "fault_points_1_and_2", "hotspot_centers",
                  "num_layers", "redundancy", "request_id", "start_x", "start_y", "start_z", "end_x", "end_y", "end_z",
                  "generation_time", "completion_time", "latency_cycles", "horizontal_distance", "terminal_status"]


def json_safe(value: Any) -> Any:
    if isinstance(value, dict):
        return {str(k): json_safe(v) for k, v in value.items()}
    if isinstance(value, (tuple, list)):
        return [json_safe(v) for v in value]
    if isinstance(value, np.generic):
        return json_safe(value.item())
    if isinstance(value, float) and not math.isfinite(value):
        return None
    return value


def write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(json_safe(value), indent=2, allow_nan=False) + "\n", encoding="utf-8")


def write_csv(path: Path, rows: list[dict], fields: list[str] | None = None) -> None:
    fields = fields or list(dict.fromkeys(k for row in rows for k in row))
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            safe = json_safe(row)
            writer.writerow({k: json.dumps(v, separators=(",", ":")) if isinstance(v, (list, dict)) else v
                             for k, v in safe.items()})


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def assert_close(actual: float, expected: float, label: str) -> None:
    if not math.isfinite(actual) or not math.isclose(actual, expected, rel_tol=1e-8, abs_tol=1e-8):
        raise ValueError(f"{label}: {actual!r} != {expected!r}")


def centers(value: Any) -> list[list[int]]:
    if isinstance(value, str):
        value = json.loads(value) if value.startswith("[") else [list(map(int, p.split(","))) for p in value.split(";") if p]
    return [[int(p[axis]) for axis in ("x", "y", "z")] if isinstance(p, dict) else list(p) for p in (value or [])]


def scenario(parameters: dict) -> str:
    if str(parameters.get("failure_model", "uniform")).lower() == "uniform":
        return "U"
    points = sorted(centers(parameters.get("hotspot_centers", [])))
    for name, expected in (("H1", [[5, 5, 0]]), ("H2", [[10, 10, 0]]), ("H12", [[5, 5, 0], [10, 10, 0]])):
        if points == expected:
            return name
    raise ValueError(f"Unrecognized manual hotspot scenario: {points}")


def cell_key(parameters: dict) -> tuple:
    return (float(parameters["initial_failure_rate"]), int(parameters["num_layers"]),
            str(parameters["redundancy"]).lower(), scenario(parameters))


def cell_metadata(parameters: dict) -> dict:
    f, layers, layout, name = cell_key(parameters)
    points = centers(parameters.get("hotspot_centers", [])) if name != "U" else []
    return {"configured_failure_fraction": f, "num_layers": layers, "redundancy": layout,
            "fault_scenario": name, "fault_point_1": int(name == "H1"),
            "fault_point_2": int(name == "H2"), "fault_points_1_and_2": int(name == "H12"),
            "hotspot_centers": points}


def read_summary(path: Path) -> dict:
    labels = {**runner.METRICS, **EXTRA_METRICS}
    result = {}
    with path.open(encoding="utf-8", newline="") as handle:
        reader = csv.reader(handle)
        if next(reader, None) != ["Metric", "Value"]:
            raise ValueError("Unexpected summary header")
        seen = set()
        for row in reader:
            if len(row) != 2 or row[0] in seen:
                raise ValueError(f"Malformed/duplicate summary metric: {row}")
            seen.add(row[0])
            if row[0] not in labels:
                continue
            name = labels[row[0]]
            value = None if row[1] in ("", "N/A") else int(row[1]) if name in INT_METRICS else float(row[1])
            if value is not None and (not math.isfinite(value) or value < 0):
                raise ValueError(f"Nonfinite/negative summary metric: {name}")
            result[name] = value
    required = set(METRICS) & set(labels.values())
    required -= {"success_rate_pct", "average_latency_cycles", "energy_per_completed_flit_pj"}
    required |= {"failed_requests", "in_flight_at_end", "actual_seed", "router_count", "elapsed_cycles", "elapsed_seconds",
                 "router_events", "vertical_hops", "failed_tsvs", "failed_redundant_tsvs",
                 "router_energy_pj", "horizontal_energy_pj", "vertical_energy_pj",
                 "total_horizontal_distance", "average_horizontal_distance", "redundant_tsv_usages"}
    if any(result.get(k) is None for k in required):
        raise ValueError(f"Missing required raw metrics: {sorted(k for k in required if result.get(k) is None)}")
    return result


def verify_effective(planned: dict, effective: dict, requested_seed: int) -> None:
    for name, expected in planned.items():
        if name == "power_config":
            if runner.project_path(effective[name]) != runner.project_path(expected):
                raise ValueError("Effective power configuration resolved path differs")
        elif name == "hotspot_centers":
            if centers(expected) != centers(effective[name]):
                raise ValueError("Effective manual centers differ")
        elif name in ("failure_model", "failure_mode", "redundancy"):
            normalized = {"a": "initial_only", "b": "runtime_only", "c": "combined"}.get(str(expected).lower(), str(expected).lower())
            actual_normalized = {"a": "initial_only", "b": "runtime_only", "c": "combined"}.get(str(effective[name]).lower(), str(effective[name]).lower())
            if normalized != actual_normalized:
                raise ValueError(f"Effective setting differs: {name}")
        elif isinstance(expected, (float, int)):
            # Config settings are printed with 17 significant digits.
            if effective.get(name) != expected:
                raise ValueError(f"Effective setting differs: {name}")
        elif effective.get(name) != expected:
            raise ValueError(f"Effective setting differs: {name}")
    if effective["random_seed"] != requested_seed or requested_seed == 0:
        raise ValueError("Experiment requires the exact requested nonzero seed")
    if str(effective["failure_mode"]).lower() not in ("initial_only", "a") or effective["runtime_failure_rate"] != 0:
        raise ValueError("Experiment is restricted to initial-only faults")


def spare_population(effective: dict) -> int:
    n, g, layers = effective["grid_size"], effective["grid_factor"], effective["num_layers"]
    layout = effective["redundancy"].lower()
    per_interface = 0 if layout == "none" else 4 * (n // 4) ** 2 if layout == "corner4" else ((n - 2) // (g - 1) + 1) ** 2 if g > 1 else 0
    return per_interface * (layers - 1)


def power_formulas(power: dict, metrics: dict, routers: int) -> dict:
    bits, voltage, toggle = power["flit_bits"], power["voltage_v"], power["total_toggle_probability"]
    link_factor = 0.5 * 1e-3 * voltage ** 2 * toggle
    e_router = sum(power[k] for k in ("router_buffer_event_pj", "router_crossbar_event_pj", "router_control_event_pj"))
    e_h = link_factor * bits * power["horizontal_link_length_um"] * power["horizontal_capacitance_ff_per_um"]
    e_v = link_factor * power["physical_signal_tsvs"] * power["tsv_capacitance_ff"]
    e_spare = e_v * power["redundant_vertical_capacitance_multiplier"]
    for name, value in (("router_event_energy_pj", e_router), ("horizontal_hop_energy_pj", e_h),
                        ("vertical_hop_energy_pj", e_v), ("redundant_vertical_hop_energy_pj", e_spare)):
        assert_close(power[name], value, name)
    seconds = metrics["elapsed_cycles"] / power["frequency_hz"]
    denominator = seconds * 1e9
    idle = routers * power["router_idle_mw"]
    router_dynamic = metrics["router_events"] * e_router
    router_energy = idle * denominator + router_dynamic
    horizontal_energy = metrics["horizontal_hops"] * e_h
    vertical_energy = (metrics["vertical_hops"] - metrics["redundant_vertical_hops"]) * e_v + metrics["redundant_vertical_hops"] * e_spare
    total = router_energy + horizontal_energy + vertical_energy
    return {"elapsed_seconds": seconds, "router_energy_pj": router_energy,
            "horizontal_energy_pj": horizontal_energy, "vertical_energy_pj": vertical_energy,
            "total_energy_pj": total, "router_static_mw": idle if denominator else 0,
            "router_dynamic_mw": router_dynamic / denominator if denominator else 0,
            "horizontal_mw": horizontal_energy / denominator if denominator else 0,
            "vertical_mw": vertical_energy / denominator if denominator else 0,
            "total_mw": total / denominator if denominator else 0}


def audit_run(task: dict, row: dict, prefix: Path) -> tuple[dict, list[dict], dict]:
    configuration = json.loads(Path(f"{prefix}_config.json").read_text(encoding="utf-8"))
    effective, power = configuration["simulation"], configuration["power_parameters"]
    requested = task["seed"]
    for key in ("requested_seed", "seed"):
        if row.get(key) not in (None, "") and int(row[key]) != requested:
            raise ValueError(f"Runner {key} differs from independently planned seed")
    verify_effective(task["parameters"], effective, requested)
    raw = read_summary(Path(f"{prefix}_summary.csv"))
    if raw["actual_seed"] != requested or configuration["actual_seed"] != requested:
        raise ValueError("Raw actual seed differs from requested seed")
    if row.get("actual_seed") is not None and int(row["actual_seed"]) != requested:
        raise ValueError("Runner actual seed differs from raw seed")
    expected_centers = centers(effective["hotspot_centers"]) if scenario(effective) != "U" else []
    if centers(configuration["actual_hotspot_centers"]) != expected_centers:
        raise ValueError("Actual hotspot centers differ from manual scenario")
    meta = cell_metadata(effective)
    n, layers = effective["grid_size"], effective["num_layers"]
    channels, routers, spares = n * n * (layers - 1), n * n * layers, spare_population(effective)
    if raw["router_count"] != routers or raw["failed_tsvs"] > channels or raw["failed_redundant_tsvs"] > spares or raw["failed_redundant_tsvs"] > raw["failed_tsvs"]:
        raise ValueError("Channel/router/spare population accounting differs")
    if raw["in_flight_at_end"] != 0 or raw["total_requests"] != raw["completed_requests"] + raw["failed_requests"]:
        raise ValueError("Run has not drained or terminal counts do not conserve requests")
    if raw["elapsed_cycles"] < effective["simulation_cycles"]:
        raise ValueError("Run stopped before the generation horizon")
    if raw["router_events"] != raw["horizontal_hops"] + raw["vertical_hops"] or raw["redundant_vertical_hops"] > raw["vertical_hops"]:
        raise ValueError("Router/link event accounting differs")
    if raw["total_requests"] == 0 and any(raw[name] != 0 for name in ("router_events", "horizontal_hops", "vertical_hops", "redundant_vertical_hops")):
        raise ValueError("Zero generated traffic must have zero launch counters")
    if raw["redundant_tsv_usages"] != raw["redundant_vertical_hops"]:
        raise ValueError("Redundant TSV usages differ from spare launch counters")
    for name, expected in power_formulas(power, raw, routers).items():
        assert_close(raw[name], expected, name)
    last = None
    with Path(f"{prefix}_power.csv").open(encoding="utf-8", newline="") as handle:
        for last in csv.DictReader(handle):
            pass
    if last is None:
        raise ValueError("Power trace has no records")
    for name in ("elapsed_cycles", "elapsed_seconds", "router_events", "horizontal_hops", "vertical_hops",
                 "redundant_vertical_hops", "router_static_mw", "router_dynamic_mw", "horizontal_mw",
                 "vertical_mw", "total_mw", "total_energy_pj", "completed_requests"):
        trace_name = "completed_flits" if name == "completed_requests" else name
        value = int(last[trace_name]) if name in INT_METRICS else float(last[trace_name])
        if name in INT_METRICS:
            if value != raw[name]:
                raise ValueError(f"Power trace integer mismatch: {name}")
        else:
            assert_close(value, raw[name], f"Final power trace {name}")
    requests, seen, latencies = [], set(), []
    common = {**meta, "run_id": task["run_id"], "requested_seed": requested, "actual_seed": requested,
              "observed_failed_fraction": raw["failed_tsvs"] / channels}
    with Path(f"{prefix}_requests.csv").open(encoding="utf-8", newline="") as handle:
        for request in csv.DictReader(handle):
            request_id = int(request["Request ID"])
            if request_id < 0:
                raise ValueError("Request ID must be nonnegative")
            if request_id in seen:
                raise ValueError(f"Duplicate terminal request ID: {request_id}")
            seen.add(request_id)
            state = request["Status"]
            if state not in ("Completed", "Failed"):
                raise ValueError(f"Nonterminal request: {state}")
            generated = int(request["Generate Time"])
            if not 0 <= generated < effective["simulation_cycles"]:
                raise ValueError("Request generation timestamp outside horizon")
            coords = {f"{end.lower()}_{axis.lower()}": int(request[f"{end} {axis}"]) for end in ("Start", "End") for axis in ("X", "Y", "Z")}
            if any(not 0 <= coords[f"{end}_{axis}"] < (layers if axis == "z" else n) for end in ("start", "end") for axis in ("x", "y", "z")):
                raise ValueError("Request endpoint outside grid")
            if coords["start_z"] >= layers - 1 or coords["end_z"] != layers - 1:
                raise ValueError("Request endpoints violate generated lower-layer source/top-layer destination workload")
            completion, latency = None, None
            if state == "Completed":
                completion, latency = int(request["Complete Time"]), int(request["Latency"])
                if completion < generated or completion > raw["elapsed_cycles"] or latency != completion - generated:
                    raise ValueError("Completion timestamps do not reconstruct latency")
                latencies.append(latency)
            elif request["Complete Time"] not in ("N/A", "") or request["Latency"] not in ("N/A", ""):
                raise ValueError("Failed request completion/latency must be undefined")
            horizontal = int(request["Horizontal Distance"])
            if horizontal < 0:
                raise ValueError("Negative request horizontal distance")
            requests.append({**common, "request_id": request_id, **coords, "generation_time": generated,
                             "completion_time": completion, "latency_cycles": latency,
                             "horizontal_distance": horizontal, "terminal_status": state})
    requests.sort(key=lambda r: r["request_id"])
    if len(requests) != raw["total_requests"] or len(latencies) != raw["completed_requests"]:
        raise ValueError("Raw request rows do not reconstruct summary counts")
    total_horizontal = sum(r["horizontal_distance"] for r in requests)
    if total_horizontal != raw["total_horizontal_distance"] or total_horizontal != raw["horizontal_hops"]:
        raise ValueError("Request horizontal distances differ from total distance or initial-only horizontal launch counters")
    assert_close(raw["average_horizontal_distance"], total_horizontal / len(requests) if requests else 0.0,
                 "Average horizontal distance")
    completed = raw["completed_requests"]
    total_latency = sum(latencies)
    mean_latency = total_latency / completed if completed else None
    success = 100 * completed / raw["total_requests"] if raw["total_requests"] else None
    energy_per = raw["total_energy_pj"] / completed if completed else None
    for name, expected in (("average_latency_cycles", mean_latency), ("success_rate_pct", success), ("energy_per_completed_flit_pj", energy_per)):
        if expected is None:
            if raw.get(name) is not None:
                raise ValueError(f"Undefined {name} exported as a number")
        elif raw.get(name) is None:
            raise ValueError(f"Missing defined metric {name}")
        else:
            assert_close(raw[name], expected, name)
    if completed:
        if raw.get("min_latency_cycles") != min(latencies) or raw.get("max_latency_cycles") != max(latencies):
            raise ValueError("Raw min/max latency differs from requests")
    elif raw.get("min_latency_cycles") is not None or raw.get("max_latency_cycles") is not None:
        raise ValueError("Undefined min/max latency exported as a number with no completions")
    raw.update({"average_latency_cycles": mean_latency, "success_rate_pct": success,
                "energy_per_completed_flit_pj": energy_per, "total_completed_latency_cycles": total_latency,
                "median_latency_cycles": statistics.median(latencies) if completed else None,
                "p95_latency_cycles": sorted(latencies)[math.ceil(0.95 * completed) - 1] if completed else None,
                "average_completed_horizontal_distance": sum(r["horizontal_distance"] for r in requests if r["terminal_status"] == "Completed") / completed if completed else None,
                "observed_failed_fraction": raw["failed_tsvs"] / channels,
                "logical_channel_count": channels, "spare_channel_count": spares,
                "drain_cycles": raw["elapsed_cycles"] - effective["simulation_cycles"],
                "dynamic_mw": raw["router_dynamic_mw"] + raw["horizontal_mw"] + raw["vertical_mw"],
                "total_mw_per_layer": raw["total_mw"] / layers,
                "horizontal_hops_per_generated": raw["horizontal_hops"] / raw["total_requests"] if raw["total_requests"] else None,
                "spare_hops_per_generated": raw["redundant_vertical_hops"] / raw["total_requests"] if raw["total_requests"] else None})
    raw["router_static_energy_pj"] = raw["router_static_mw"] * raw["elapsed_seconds"] * 1e9
    raw["router_dynamic_energy_pj"] = raw["router_events"] * power["router_event_energy_pj"]
    raw["dynamic_energy_pj"] = raw["router_dynamic_energy_pj"] + raw["horizontal_energy_pj"] + raw["vertical_energy_pj"]
    record = {**row, **common, **raw, "analysis_status": "valid", "analysis_error": "",
              "effective_parameters": effective, "power_parameters": power,
              **{f"parameter.{k}": v for k, v in effective.items()}}
    record["output_prefix"] = str(prefix)
    record["raw_sha256"] = {suffix: sha256(Path(f"{prefix}_{suffix}")) for suffix in ("config.json", "summary.csv", "requests.csv", "power.csv")}
    traffic = [[r[k] for k in ("request_id", "start_x", "start_y", "start_z", "end_x", "end_y", "end_z", "generation_time")] for r in requests]
    record["traffic_sha256"] = hashlib.sha256(json.dumps(traffic, separators=(",", ":")).encode()).hexdigest()
    return record, requests, configuration


def bootstrap_weights(rng: np.random.Generator, n: int, resamples: int) -> np.ndarray:
    """Each row counts complete seed runs in one ordinary nonparametric resample."""
    if n == 0:
        return np.zeros((resamples, 0), dtype=float)
    indices = rng.integers(0, n, size=(resamples, n))
    weights = np.zeros((resamples, n), dtype=float)
    np.add.at(weights, (np.arange(resamples)[:, None], indices), 1)
    return weights


def safe_ratio(numerator: Any, denominator: Any) -> np.ndarray:
    numerator, denominator = np.broadcast_arrays(np.asarray(numerator, float), np.asarray(denominator, float))
    return np.divide(numerator, denominator, out=np.full(numerator.shape, np.nan), where=denominator > 0)


def interval(distribution: np.ndarray) -> tuple[float | None, float | None, int]:
    finite = distribution[np.isfinite(distribution)]
    if len(finite) == 0:
        return None, None, 0
    lo, hi = np.quantile(finite, [0.025, 0.975], method="linear")
    return float(lo), float(hi), len(finite)


def summarize_values(values: list, weights: np.ndarray) -> dict:
    values = np.array([np.nan if v is None else v for v in values], dtype=float)
    defined = np.isfinite(values)
    distribution = safe_ratio(weights @ np.nan_to_num(values, nan=0.0), weights @ defined.astype(float))
    lo, hi, bootstrap_defined = interval(distribution)
    finite = values[defined]
    return {"mean": float(finite.mean()) if len(finite) else None,
            "sd": float(finite.std(ddof=1)) if len(finite) > 1 else None,
            "ci95_low": lo, "ci95_high": hi, "defined_runs": int(defined.sum()),
            "undefined_runs": int((~defined).sum()), "bootstrap_defined_resamples": bootstrap_defined,
            "degenerate_interval": int(lo is not None and lo == hi)}


def summarize_cell(planned: list[dict], runs: list[dict], rng: np.random.Generator, resamples: int) -> tuple[dict, np.ndarray]:
    good = [r for r in runs if r["analysis_status"] == "valid"]
    weights = bootstrap_weights(rng, len(good), resamples)
    row = {**cell_metadata(planned[0]["parameters"]), "planned_runs": len(planned), "valid_runs": len(good),
           "missing_runs": sum(r["analysis_status"] == "missing" for r in runs),
           "operational_failed_runs": sum(r["analysis_status"] == "operational_failure" for r in runs),
           "invalid_runs": sum(r["analysis_status"] == "invalid" for r in runs),
           "planned_seeds": [t["seed"] for t in planned], "valid_seeds": [r["actual_seed"] for r in good],
           "no_completion_runs": sum(r["completed_requests"] == 0 for r in good),
           "zero_traffic_runs": sum(r["total_requests"] == 0 for r in good),
           "complete_cell": len(good) == len(planned),
           "bootstrap_unit": "whole seed run", "primary_weighting": "equal valid seed runs; defined values only"}
    for metric in METRICS:
        row.update({f"{metric}_{key}": value for key, value in summarize_values([r.get(metric) for r in good], weights).items()})
    for name, (numerator, denominator, multiplier) in POOLED.items():
        nums = np.array([r[numerator] for r in good], float)
        dens = np.array([r[denominator] for r in good], float)
        distribution = multiplier * safe_ratio(weights @ nums, weights @ dens)
        lo, hi, defined = interval(distribution)
        row.update({f"pooled_{name}": float(multiplier * nums.sum() / dens.sum()) if dens.sum() else None,
                    f"pooled_{name}_numerator": float(nums.sum()), f"pooled_{name}_denominator": float(dens.sum()),
                    f"pooled_{name}_ci95_low": lo, f"pooled_{name}_ci95_high": hi,
                    f"pooled_{name}_bootstrap_defined_resamples": defined})
    return row, weights


def probability_map(parameters: dict) -> tuple[np.ndarray, np.ndarray]:
    """Reconstruct the native fixed map from exported settings, never from faults."""
    n = int(parameters.get("grid_size", 4 * parameters["grid_factor"]))
    layers = int(parameters["num_layers"])
    weights = np.ones((layers - 1, n, n), dtype=float)
    y, x = np.mgrid[:n, :n]
    if scenario(parameters) != "U":
        radius, multiplier = float(parameters["hotspot_radius"]), float(parameters["hotspot_multiplier"])
        if radius <= 0 or not math.isfinite(radius) or multiplier < 1 or not math.isfinite(multiplier):
            raise ValueError("Invalid hotspot parameters")
        influences = np.zeros_like(weights)
        for cx, cy, z in centers(parameters["hotspot_centers"]):
            if not (0 <= cx < n and 0 <= cy < n and 0 <= z < layers - 1):
                raise ValueError("Map center outside logical interfaces")
            gaussian = np.exp(-0.5 * (((x - cx) / radius) ** 2 + ((y - cy) / radius) ** 2))
            influences[z] = np.maximum(influences[z], gaussian)
        weights += (multiplier - 1) * influences
    f = float(parameters["initial_failure_rate"])
    if not 0 <= f <= 1:
        raise ValueError("Invalid configured map mean")
    if f in (0, 1):
        return np.full_like(weights, f), weights
    # Independent bisection implementation of min(1, scale * weight).
    low, high = 0.0, 1.0
    for _ in range(100):
        scale = (low + high) / 2
        if np.minimum(1.0, scale * weights).mean() < f:
            low = scale
        else:
            high = scale
    probabilities = np.minimum(1.0, (low + high) / 2 * weights)
    if abs(probabilities.mean() - f) > 1e-12 or probabilities.min() < 0 or probabilities.max() > 1:
        raise ValueError("Reconstructed probability map fails mean/bounds")
    return probabilities, weights


def fixed_profile(parameters: dict, power: dict | None = None) -> str:
    ignored = {"num_layers", "grid_size", "output_prefix", "random_seed", "initial_failure_rate", "failure_rate",
               "failure_model", "hotspot_centers", "hotspot_count"}
    profile = {k: v for k, v in parameters.items() if k not in ignored}
    if power is not None:
        profile["power_parameters"] = power
    return json.dumps(profile, sort_keys=True, separators=(",", ":"))


def matched_contrasts(runs: list[dict], rng: np.random.Generator, resamples: int) -> tuple[list[dict], list[dict]]:
    groups = defaultdict(dict)
    for run in runs:
        if run["analysis_status"] == "valid":
            key = (run["configured_failure_fraction"], run["num_layers"], run["redundancy"])
            groups[key].setdefault(run["fault_scenario"], {})[run["actual_seed"]] = run
    contrasts, audit = [], []
    for (f, layers, layout), scenarios in sorted(groups.items()):
        names = [s for s in SCENARIOS if s in scenarios]
        if "U" not in names or len(names) == 1:
            continue
        # Common blocks give all displayed contrasts the same resampled seeds.
        common = sorted(set.intersection(*(set(scenarios[s]) for s in names)))
        matched = []
        for seed in common:
            base = scenarios["U"][seed]
            for name in names[1:]:
                other = scenarios[name][seed]
                same = base["traffic_sha256"] == other["traffic_sha256"]
                profile_same = fixed_profile(base["effective_parameters"], base["power_parameters"]) == fixed_profile(other["effective_parameters"], other["power_parameters"])
                audit.append({"configured_failure_fraction": f, "num_layers": layers, "redundancy": layout,
                              "seed": seed, "comparison": name + "-U", "uniform_run_id": base["run_id"],
                              "hotspot_run_id": other["run_id"], "traffic_equal": same, "fixed_settings_equal": profile_same})
            if all(base["traffic_sha256"] == scenarios[name][seed]["traffic_sha256"] and
                   fixed_profile(base["effective_parameters"], base["power_parameters"]) == fixed_profile(scenarios[name][seed]["effective_parameters"], scenarios[name][seed]["power_parameters"]) for name in names[1:]):
                matched.append(seed)
        weights = bootstrap_weights(rng, len(matched), resamples)
        for name in names[1:]:
            row = {"configured_failure_fraction": f, "num_layers": layers, "redundancy": layout,
                   "comparison": name + "-U", "matched_runs": len(matched), "matched_seeds": matched,
                   "joint_scenarios": names, "uniform_valid_runs": len(scenarios["U"]),
                   "hotspot_valid_runs": len(scenarios[name]),
                   "excluded_uniform_runs": len(scenarios["U"]) - len(matched),
                   "bootstrap_unit": "matched whole-seed blocks, jointly resampled across all available scenarios"}
            base = [scenarios["U"][s] for s in matched]
            hot = [scenarios[name][s] for s in matched]
            for metric in METRICS:
                differences = [h[metric] - b[metric] if h.get(metric) is not None and b.get(metric) is not None else None for h, b in zip(hot, base)]
                row.update({f"{metric}_{k}": v for k, v in summarize_values(differences, weights).items()})
            for metric, (num, den, scale) in POOLED.items():
                def estimate(selected):
                    nums, dens = np.array([r[num] for r in selected], float), np.array([r[den] for r in selected], float)
                    return scale * safe_ratio(weights @ nums, weights @ dens), float(scale * nums.sum() / dens.sum()) if dens.sum() else None
                hot_boot, hot_value = estimate(hot)
                base_boot, base_value = estimate(base)
                lo, hi, defined = interval(hot_boot - base_boot)
                row.update({f"pooled_{metric}_difference": hot_value - base_value if hot_value is not None and base_value is not None else None,
                            f"pooled_{metric}_ci95_low": lo, f"pooled_{metric}_ci95_high": hi,
                            f"pooled_{metric}_bootstrap_defined_resamples": defined})
            contrasts.append(row)
    return contrasts, audit


def audit_fixed_seed_traffic(runs: list[dict]) -> list[dict]:
    """Fault rate/model changes must preserve traffic at fixed L/layout/seed."""
    groups = defaultdict(list)
    for run in runs:
        if run["analysis_status"] == "valid":
            groups[(run["num_layers"], run["redundancy"], run["actual_seed"])].append(run)
    rows = []
    for (layers, layout, seed), group in sorted(groups.items()):
        group.sort(key=lambda r: (r["configured_failure_fraction"], SCENARIOS.index(r["fault_scenario"])))
        base = group[0]
        for other in group[1:]:
            rows.append({"num_layers": layers, "redundancy": layout, "seed": seed,
                         "reference_run_id": base["run_id"], "compared_run_id": other["run_id"],
                         "configured_failure_fraction": other["configured_failure_fraction"],
                         "fault_scenario": other["fault_scenario"],
                         "traffic_equal": base["traffic_sha256"] == other["traffic_sha256"],
                         "fixed_settings_equal": fixed_profile(base["effective_parameters"], base["power_parameters"]) == fixed_profile(other["effective_parameters"], other["power_parameters"])})
    return rows


def load_inputs(directory: Path) -> tuple[list[dict], list[dict], dict]:
    spec = json.loads((directory / "batch.json").read_text(encoding="utf-8"))
    tasks = [{"run_id": t.run_id, "configuration": t.configuration, "variant": t.variant,
              "repeat": t.repeat, "seed": t.seed, "parameters": t.parameters,
              "output_prefix": str(directory / "runs" / t.run_id)} for t in runner.build_tasks(spec)]
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8")) if (directory / "manifest.json").is_file() else {}
    if manifest.get("tasks"):
        manifest_tasks = {t["run_id"]: t for t in manifest["tasks"]}
        if len(manifest_tasks) != len(manifest["tasks"]) or set(manifest_tasks) != {t["run_id"] for t in tasks}:
            raise ValueError("Manifest planned tasks differ from independently rebuilt frozen batch")
        for task in tasks:
            declared = manifest_tasks[task["run_id"]]
            if task["seed"] != declared["seed"] or task["parameters"] != declared["parameters"]:
                raise ValueError("Manifest seed/settings differ from frozen batch")
            task["output_prefix"] = declared["output_prefix"]
    if (directory / "results.json").is_file():
        results = json.loads((directory / "results.json").read_text(encoding="utf-8"))
        rows = results["runs"] if isinstance(results, dict) else results
    elif (directory / "runs.csv").is_file():
        with (directory / "runs.csv").open(encoding="utf-8", newline="") as handle:
            rows = list(csv.DictReader(handle))
    else:
        rows = []
    return tasks, rows, manifest


def plot_exports(output: Path, summaries: list[dict], contrasts: list[dict], map_cells: list[dict], maps: dict,
                 delivery: list[dict], resamples: int, seed: int) -> list[str]:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    output.mkdir(parents=True, exist_ok=True)
    artifacts = []
    colors = {"U": "#333333", "H1": "#0072B2", "H2": "#D55E00", "H12": "#009E73"}
    note = f"Equal seed weights; pointwise 95% whole-seed bootstrap ({resamples:,}; RNG {seed}). Incomplete cells marked x."

    def save(fig, name, footer=note):
        fig.text(0.5, 0.005, footer, ha="center", va="bottom", fontsize=7, wrap=True)
        fig.tight_layout(rect=(0, 0.045, 1, 0.96))
        for extension in ("png", "pdf"):
            path = output / f"{name}.{extension}"
            fig.savefig(path, dpi=150, bbox_inches="tight")
            artifacts.append(str(path))
        plt.close(fig)

    metrics = {
        "success_rate_pct": ("Delivered / generated (%)", "delivery_vs_failure"),
        "average_latency_cycles": ("Completed-only mean latency (cycles)", "latency_vs_failure"),
        "p95_latency_cycles": ("Completed-only nearest-rank p95 (cycles)", "p95_latency_vs_failure"),
        "horizontal_hops_per_generated": ("Horizontal launches / generated flit", "horizontal_activity"),
        "spare_hops_per_generated": ("Spare launches / generated flit", "spare_activity"),
        "total_mw": ("Mean total modeled power (mW)", "power_total"),
        "router_static_mw": ("Mean idle modeled power (mW)", "power_idle"),
        "dynamic_mw": ("Mean dynamic modeled power (mW)", "power_dynamic"),
        "total_mw_per_layer": ("Mean modeled power / layer (mW)", "power_per_layer"),
        "energy_per_completed_flit_pj": ("All modeled energy / completed flit (pJ)", "energy_per_completion"),
        "observed_failed_fraction": ("Observed failed channel fraction", "observed_vs_configured"),
        "completed_requests": ("Mean completed flits per seed", "completed_counts"),
        "drain_cycles": ("Mean drain duration (cycles)", "drain_duration"),
    }
    layers = sorted({s["num_layers"] for s in summaries})
    for metric, (label, filename) in metrics.items():
        fig, axes = plt.subplots(2, 2, figsize=(11, 7), squeeze=False)
        fig.suptitle(label)
        for axis, layer in zip(axes.flat, layers):
            for name in SCENARIOS:
                cells = sorted([s for s in summaries if s["num_layers"] == layer and s["fault_scenario"] == name], key=lambda s: s["configured_failure_fraction"])
                xs = np.array([100 * s["configured_failure_fraction"] for s in cells])
                ys = np.array([np.nan if s[f"{metric}_mean"] is None else s[f"{metric}_mean"] for s in cells])
                lo = np.array([np.nan if s[f"{metric}_ci95_low"] is None else s[f"{metric}_ci95_low"] for s in cells])
                hi = np.array([np.nan if s[f"{metric}_ci95_high"] is None else s[f"{metric}_ci95_high"] for s in cells])
                axis.plot(xs, ys, ".-", color=colors[name], label=name)
                axis.fill_between(xs, lo, hi, alpha=0.14, color=colors[name])
                bad = [i for i, s in enumerate(cells) if not s["complete_cell"]]
                axis.scatter(xs[bad], ys[bad], marker="x", color="red", zorder=5)
            if metric == "observed_failed_fraction":
                axis.plot([0, 70], [0, .7], "--", color="gray", label="configured mean")
            axis.set(title=f"L={layer}", xlabel="Configured mean initial failure (%)", ylabel=label)
            axis.grid(alpha=.25)
            axis.legend(fontsize=8)
        for axis in list(axes.flat)[len(layers):]:
            axis.set_visible(False)
        save(fig, filename)

    for group_name, group_metrics in (("power_event_components", ("router_dynamic_mw", "horizontal_mw", "vertical_mw")),
                                      ("seed_value_counts", ("average_latency_cycles", "energy_per_completed_flit_pj"))):
        fig, axes = plt.subplots(2, 2, figsize=(12, 8))
        for axis, layer in zip(axes.flat, layers):
            for name in SCENARIOS:
                cells = sorted([s for s in summaries if s["num_layers"] == layer and s["fault_scenario"] == name], key=lambda s: s["configured_failure_fraction"])
                for index, metric in enumerate(group_metrics):
                    ys = [s[f"{metric}_mean"] if group_name == "power_event_components" else s[f"{metric}_defined_runs"] for s in cells]
                    axis.plot([100*s["configured_failure_fraction"] for s in cells], ys, linestyle=("-", "--", ":")[index], color=colors[name], label=f"{name}: {metric.replace('_mw','')}")
            axis.set(title=f"L={layer}", xlabel="Configured mean initial failure (%)", ylabel="Mean dynamic modeled power (mW)" if group_name == "power_event_components" else "Seeds with defined value")
            axis.grid(alpha=.25)
            axis.legend(fontsize=6)
        fig.suptitle("Router-event and link contributions" if group_name == "power_event_components" else "Defined latency/energy seeds (zero completions remain undefined)")
        save(fig, group_name)

    for metric in ("success_rate_pct", "average_latency_cycles", "horizontal_hops_per_generated", "dynamic_mw", "energy_per_completed_flit_pj"):
        fig, axes = plt.subplots(2, 2, figsize=(11, 7))
        for axis, layer in zip(axes.flat, layers):
            for name in SCENARIOS[1:]:
                selected = sorted([s for s in contrasts if s["num_layers"] == layer and s["comparison"] == name + "-U"], key=lambda s:s["configured_failure_fraction"])
                xs = [100*s["configured_failure_fraction"] for s in selected]
                def values(suffix):
                    return [np.nan if s[f"{metric}_{suffix}"] is None else s[f"{metric}_{suffix}"] for s in selected]
                axis.plot(xs, values("mean"), ".-", color=colors[name], label=f"{name}-U")
                axis.fill_between(xs, values("ci95_low"), values("ci95_high"), alpha=.14, color=colors[name])
            axis.axhline(0, color="gray", linestyle="--")
            axis.set(title=f"L={layer}", xlabel="Configured mean initial failure (%)", ylabel=f"Paired difference: {metric}")
            axis.grid(alpha=.25)
            axis.legend(fontsize=8)
        fig.suptitle(f"Matched hotspot minus uniform: {metric}")
        save(fig, f"contrast_{metric}", f"Pointwise 95% bootstrap of verified matched seed blocks, jointly across scenarios ({resamples:,}; RNG {seed}).")

    for layer in layers:
        fig, axes = plt.subplots(2, 2, figsize=(12, 8))
        selected_layer = [r for r in delivery if r["num_layers"] == layer]
        rates = sorted({r["configured_failure_fraction"] for r in selected_layer})
        palette = plt.get_cmap("viridis")
        for axis, name in zip(axes.flat, SCENARIOS):
            for i, rate in enumerate(rates):
                selected = sorted([r for r in selected_layer if r["fault_scenario"] == name and r["configured_failure_fraction"] == rate], key=lambda r:r["latency_threshold_cycles"])
                if not selected:
                    continue
                color = palette(i / max(1, len(rates)-1))
                axis.plot([r["latency_threshold_cycles"] for r in selected], [r["equal_seed_delivery_fraction"] for r in selected], color=color, label=f"f={rate:.0%}")
                axis.fill_between([r["latency_threshold_cycles"] for r in selected], [r["ci95_low"] if r["ci95_low"] is not None else np.nan for r in selected], [r["ci95_high"] if r["ci95_high"] is not None else np.nan for r in selected], color=color, alpha=.08)
            axis.set(title=f"L={layer}, {name}", xlabel="Completion latency threshold (cycles)", ylabel="Completed by threshold / generated")
            axis.set_ylim(0, 1.02)
            axis.grid(alpha=.25)
            if axis.get_legend_handles_labels()[0]:
                axis.legend(fontsize=6, ncol=2)
        fig.suptitle("Unconditional delivery by latency (failed requests stay in denominator)")
        save(fig, f"delivery_by_latency_L{layer}")

    for layer, f in sorted({(c["num_layers"], c["configured_failure_fraction"]) for c in map_cells}):
        selected = [c for c in map_cells if c["num_layers"] == layer and c["configured_failure_fraction"] == f]
        fig, axes = plt.subplots(len(selected), layer-1, figsize=(3.2*(layer-1)+.5, 2.8*len(selected)), squeeze=False)
        for row_index, cell in enumerate(selected):
            array = maps[cell["map_key"]]
            for z in range(layer-1):
                axis = axes[row_index, z]
                im = axis.imshow(array[z], origin="lower", vmin=0, vmax=1, cmap="magma", interpolation="nearest")
                for cx, cy, cz in cell["hotspot_centers"]:
                    if cz == z:
                        axis.scatter(cx, cy, marker="+", color="cyan", s=100)
                clipped_y, clipped_x = np.where(array[z] >= 1)
                axis.scatter(clipped_x, clipped_y, marker="s", facecolors="none", edgecolors="white", s=18, linewidths=.4)
                axis.set(title=f"{cell['fault_scenario']}, interface {z}; mean={array[z].mean():.4f}", xlabel="x", ylabel="y")
                fig.colorbar(im, ax=axis, fraction=.046, pad=.04)
        fig.suptitle(f"Configured probability maps: L={layer}, global f={f:.0%}")
        save(fig, f"probability_maps_L{layer}_f{round(100*f):02d}", "Reconstructed fixed native map: max Gaussian influence, clipped normalization; cyan centers, white saturated cells. Not realized faults.")
    return artifacts


def analyze(directory: Path, output: Path, rng_seed: int = DEFAULT_RNG_SEED,
            resamples: int = BOOTSTRAP_RESAMPLES, make_plots: bool = True) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    tasks, input_rows, manifest = load_inputs(directory)
    rng = np.random.default_rng(rng_seed)
    grouped_tasks, seen_cells = defaultdict(list), set()
    for task in tasks:
        key = cell_key(task["parameters"])
        seed_key = (*key, task["seed"])
        if task["seed"] == 0 or seed_key in seen_cells:
            raise ValueError("Duplicate cell/seed or random seed zero in planned tasks")
        seen_cells.add(seed_key)
        grouped_tasks[key].append(task)
    row_lookup, errors, duplicate_ids = {}, [], set()
    for name, expected_hash in manifest.get("provenance", {}).get("power_files_sha256", {}).items():
        path = Path(name)
        if not path.is_file() or sha256(path) != expected_hash:
            errors.append({"run_id": "provenance", "error": f"Recorded power file missing or SHA256 changed: {name}"})
    for row in input_rows:
        run_id = row["run_id"]
        if run_id in row_lookup:
            errors.append({"run_id": run_id, "error": "Duplicate runner result row"})
            duplicate_ids.add(run_id)
        row_lookup[run_id] = row
    extra_ids = set(row_lookup) - {t["run_id"] for t in tasks}
    errors.extend({"run_id": x, "error": "Unplanned runner result row"} for x in sorted(extra_ids))
    runs, requests, configurations = [], [], {}
    for task in tasks:
        run_id = task["run_id"]
        row = row_lookup.get(run_id)
        base = {"run_id": run_id, "configuration": task["configuration"], "requested_seed": task["seed"],
                **cell_metadata(task["parameters"]), "planned_parameters": task["parameters"],
                "output_prefix": task["output_prefix"]}
        if run_id in duplicate_ids:
            record = {**base, **row, "analysis_status": "invalid", "analysis_error": "Duplicate runner result row"}
        elif row is None:
            record = {**base, "status": "missing", "analysis_status": "missing", "analysis_error": "No runner result"}
        elif row["status"] != "ok":
            record = {**base, **row, "analysis_status": "operational_failure", "analysis_error": row.get("error", "Runner status is not ok")}
        else:
            try:
                prefix = Path(task["output_prefix"])
                if not prefix.is_absolute():
                    prefix = runner.project_path(prefix)
                record, run_requests, configuration = audit_run(task, row, prefix)
                requests.extend(run_requests)
                configurations.setdefault(cell_key(task["parameters"]), configuration)
            except (OSError, ValueError, KeyError, TypeError, csv.Error) as error:
                record = {**base, **row, "analysis_status": "invalid", "analysis_error": str(error)}
        if record["analysis_status"] != "valid":
            errors.append({"run_id": run_id, "error": record["analysis_error"], "status": record["analysis_status"]})
        runs.append(record)
    profiles = {fixed_profile(r["effective_parameters"], r["power_parameters"]) for r in runs if r["analysis_status"] == "valid"}
    if len(profiles) > 1:
        errors.append({"run_id": "stage", "error": "Different fixed workload/power profiles mixed in one stage; cells are not scientifically comparable"})
    grouped_runs = defaultdict(list)
    for run in runs:
        grouped_runs[(run["configured_failure_fraction"], run["num_layers"], run["redundancy"], run["fault_scenario"])].append(run)
    summaries, delivery, map_cells, map_data = [], [], [], {}
    requests_by_run = defaultdict(list)
    for request in requests:
        requests_by_run[request["run_id"]].append(request)
    max_latency = max((r["latency_cycles"] or 0 for r in requests), default=0)
    thresholds = np.unique(np.rint(np.linspace(0, max_latency, 65)).astype(int))
    for index, key in enumerate(sorted(grouped_tasks), 1):
        summary, weights = summarize_cell(grouped_tasks[key], grouped_runs[key], rng, resamples)
        summaries.append(summary)
        good = [r for r in grouped_runs[key] if r["analysis_status"] == "valid"]
        empirical = []
        generated = []
        for run in good:
            times = np.array(sorted(r["latency_cycles"] for r in requests_by_run[run["run_id"]] if r["latency_cycles"] is not None))
            nums = np.searchsorted(times, thresholds, side="right")
            empirical.append(nums)
            generated.append(run["total_requests"])
        nums = np.array(empirical, float).reshape((len(good), len(thresholds)))
        dens = np.array(generated, float)
        curves = safe_ratio(nums, dens[:, None])
        defined = np.isfinite(curves)
        boot = safe_ratio(weights @ np.nan_to_num(curves, nan=0), weights @ defined.astype(float))
        pooled_boot = safe_ratio(weights @ nums, (weights @ dens)[:, None])
        for t_index, threshold in enumerate(thresholds):
            lo, hi, defined_boot = interval(boot[:, t_index])
            pooled_lo, pooled_hi, pooled_defined = interval(pooled_boot[:, t_index])
            finite = curves[np.isfinite(curves[:, t_index]), t_index]
            delivery.append({**cell_metadata(grouped_tasks[key][0]["parameters"]), "latency_threshold_cycles": int(threshold),
                             "valid_runs": len(good), "defined_runs": len(finite), "planned_runs": len(grouped_tasks[key]),
                             "equal_seed_delivery_fraction": float(finite.mean()) if len(finite) else None,
                             "ci95_low": lo, "ci95_high": hi, "bootstrap_defined_resamples": defined_boot,
                             "pooled_delivery_fraction": float(nums[:,t_index].sum() / dens.sum()) if dens.sum() else None,
                             "pooled_ci95_low": pooled_lo, "pooled_ci95_high": pooled_hi,
                             "pooled_bootstrap_defined_resamples": pooled_defined,
                             "completed_by_threshold_numerator": int(nums[:,t_index].sum()), "generated_denominator": int(dens.sum())})
        params = configurations.get(key, {}).get("simulation", grouped_tasks[key][0]["parameters"])
        probabilities, influences = probability_map(params)
        map_key = f"cell_{index:03d}"
        map_data[map_key] = probabilities
        map_data[map_key + "_weights"] = influences
        map_cells.append({**cell_metadata(params), "map_key": map_key, "map_source": "independent reconstruction of exported effective settings" if key in configurations else "planned settings: no validated raw config",
                          "global_mean_probability": float(probabilities.mean()), "mean_error": float(probabilities.mean() - key[0]),
                          "min_probability": float(probabilities.min()), "max_probability": float(probabilities.max()),
                          "saturated_fraction": float((probabilities >= 1).mean()),
                          "interface_means": probabilities.mean(axis=(1,2)).tolist(),
                          "interface_saturated_fractions": (probabilities >= 1).mean(axis=(1,2)).tolist(),
                          "weight_rule": "1 + (multiplier - 1) * max Gaussian; Gaussian confined to center's lower interface",
                          "background_weights_above_bottom_are_one": bool(np.all(influences[1:] == 1)),
                          "bottom_interface_exposure_fraction": 1 / (params["num_layers"] - 1)})
    contrasts, matched_audit = matched_contrasts(runs, rng, resamples)
    cross_rate_audit = audit_fixed_seed_traffic(runs)
    errors.extend({"run_id": a["hotspot_run_id"], "error": "Matched scenario traffic or fixed settings mismatch", "comparison": a["comparison"]} for a in matched_audit if not a["traffic_equal"] or not a["fixed_settings_equal"])
    errors.extend({"run_id": a["compared_run_id"], "error": "Traffic changed across fault rate/model at fixed L/layout/seed"} for a in cross_rate_audit if not a["traffic_equal"] or not a["fixed_settings_equal"])
    write_csv(output / "requests.csv", requests, REQUEST_FIELDS)
    write_csv(output / "runs.csv", runs)
    write_csv(output / "summary.csv", summaries)
    readable_fields = ["num_layers", "fault_scenario", "configured_failure_fraction", "planned_runs", "valid_runs", "missing_runs", "operational_failed_runs", "invalid_runs", "no_completion_runs", "zero_traffic_runs", "complete_cell"]
    for name in ("observed_failed_fraction", "success_rate_pct", "average_latency_cycles", "dynamic_mw", "total_mw", "energy_per_completed_flit_pj"):
        readable_fields.extend(f"{name}_{suffix}" for suffix in ("mean", "ci95_low", "ci95_high", "defined_runs"))
    readable_fields.extend(f"pooled_{name}" for name in POOLED)
    write_csv(output / "summary_readable.csv", [{k:s[k] for k in readable_fields} for s in summaries], readable_fields)
    write_csv(output / "contrasts.csv", contrasts, None if contrasts else ["configured_failure_fraction", "num_layers", "redundancy", "comparison", "matched_runs"])
    write_csv(output / "matched_traffic.csv", matched_audit, None if matched_audit else ["configured_failure_fraction", "num_layers", "redundancy", "seed", "comparison", "traffic_equal", "fixed_settings_equal"])
    write_csv(output / "fixed_seed_traffic.csv", cross_rate_audit, None if cross_rate_audit else ["num_layers", "redundancy", "seed", "reference_run_id", "compared_run_id", "traffic_equal", "fixed_settings_equal"])
    write_csv(output / "delivery_by_latency.csv", delivery)
    write_csv(output / "probability_map_diagnostics.csv", map_cells)
    np.savez_compressed(output / "probability_maps.npz", **map_data)
    write_json(output / "runs.json", runs)
    write_json(output / "summary.json", summaries)
    write_json(output / "contrasts.json", contrasts)
    write_json(output / "audit.json", {"planned_runs": len(tasks), "runner_result_rows": len(input_rows),
                "valid_runs": sum(r["analysis_status"] == "valid" for r in runs),
                "missing_runs": sum(r["analysis_status"] == "missing" for r in runs),
                "operational_failed_runs": sum(r["analysis_status"] == "operational_failure" for r in runs),
                "invalid_runs": sum(r["analysis_status"] == "invalid" for r in runs), "errors": errors})
    plots = plot_exports(output / "figures", summaries, contrasts, map_cells, map_data, delivery, resamples, rng_seed) if make_plots else []
    import matplotlib
    metadata = {"schema_version": 1, "source_directory": str(directory.resolve()), "analysis_script_sha256": sha256(Path(__file__)),
                "python": sys.version, "platform": platform.platform(), "numpy": np.__version__, "matplotlib": matplotlib.__version__,
                "analysis_rng_seed": rng_seed, "analysis_rng": "numpy.default_rng/PCG64", "bootstrap_resamples": resamples,
                "bootstrap_interval": "pointwise percentile 2.5/97.5; numpy linear quantiles; whole seed runs",
                "bootstrap_undefined_resamples": "excluded from interval, explicitly counted; undefined run values never replaced with zero",
                "paired_bootstrap": "verified exact traffic (ID, endpoints, generation time), fixed L/layout/seed; common seed blocks jointly across available scenarios",
                "primary_weighting": "equal valid seed runs; defined seed metrics only, counts retained",
                "pooled_weighting": {name: {"numerator": num, "denominator": den, "scale": scale} for name,(num,den,scale) in POOLED.items()},
                "median": "average of central two for even completion counts", "p95": "nearest rank ceil(0.95*n)",
                "unconditional_delivery": "completed with latency <= threshold / generated; failures remain denominator, no administrative censoring",
                "probability_maps": "independent reconstruction, no realized-fault inference; bounds/global-mean tolerance 1e-12",
                "uncertainty_limits": "Descriptive pointwise intervals, not simultaneous inference/equivalence. Sparse or constant runs can give degenerate intervals; flags/counts retained.",
                "planned_runs": len(tasks), "valid_runs": sum(r["analysis_status"] == "valid" for r in runs), "errors": len(errors),
                "input_hashes": {name: sha256(directory / name) for name in ("batch.json", "manifest.json", "results.json", "runs.csv") if (directory / name).is_file()},
                "manifest_provenance": manifest.get("provenance", {}), "plots": plots}
    write_json(output / "analysis.json", metadata)
    (output / "README.txt").write_text(
        "TSVRA reproducible experiment analysis\n\n"
        f"Regenerate: python3 code/scripts/analyze_experiments.py --results-dir {directory.resolve()} --output-dir {output.resolve()} --bootstrap-seed {rng_seed} --bootstrap-resamples {resamples}\n\n"
        "Install optional analysis packages: python3 -m pip install -r code/scripts/requirements-analysis.txt\n"
        "requests.csv: joined endpoints (End Z), integer timestamps, latency and terminal states; no per-request power allocation.\n"
        "runs.csv/runs.json: every planned seed, raw-audited run statistics, settings, hashes and operational/analysis errors.\n"
        "summary_readable.csv: compact report table by layer/scenario/rate. summary.csv/summary.json: complete metrics, SD, pointwise intervals, all defined/undefined/degenerate counts and pooled numerator/denominator pairs.\n"
        "contrasts.csv/contrasts.json: hotspot-minus-uniform with verified matched seed blocks resampled jointly. matched_traffic.csv retains pair audits; fixed_seed_traffic.csv verifies traffic across all fault rates/models at fixed L/layout/seed.\n"
        "delivery_by_latency.csv: unconditional completed-by-threshold/generated fractions and both equal-seed and pooled intervals. Failures stay in the denominator.\n"
        "probability_maps.npz: cell-key arrays indexed [lower_interface_z,y,x] and their max-Gaussian weights; diagnostics CSV maps keys to settings/centers/means/clipping. These are independent reconstructions, not realized-fault maps.\n"
        "figures/: standalone PNG/PDF curves, component plots, contrasts, unconditional delivery and all spatial maps. Latency curves are accompanied by delivery, completed_counts and seed_value_counts figures/tables.\n"
        "audit.json: completeness and every missing/operational/invalid job. Nonzero analyzer exit status means unresolved audit errors; incomplete cells retain flags.\n"
        "analysis.json: RNG seed, resamples, versions, script/input hashes, definitions, and provenance.\n\n"
        "Replication unit is a whole independent seed run. Primary means weight defined seed values equally. Pooled delivery/latency/energy retain numerator-denominator pairs in every whole-seed resample. No completion makes latency and energy/completion undefined; no traffic makes success undefined. CSV blank and JSON null mean undefined, never zero. Bootstrap resamples with no defined denominator are excluded and counted.\n"
        "Bootstrap intervals are descriptive pointwise percentile intervals, not simultaneous significance or equivalence. Sparse/constant results can produce uninformative/degenerate intervals; the full summary reports flags and sample counts. Completed-only latency must be interpreted alongside success to account for survivor selection.\n",
        encoding="utf-8")
    return metadata


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--bootstrap-seed", type=int, default=DEFAULT_RNG_SEED)
    parser.add_argument("--bootstrap-resamples", type=int, default=BOOTSTRAP_RESAMPLES)
    parser.add_argument("--no-plots", action="store_true", help="Audit/export sheets and map data only")
    args = parser.parse_args()
    if args.bootstrap_seed < 0 or args.bootstrap_resamples < 1:
        parser.error("Bootstrap seed must be nonnegative and resample count positive")
    try:
        metadata = analyze(args.results_dir.resolve(), (args.output_dir or args.results_dir / "analysis").resolve(),
                           args.bootstrap_seed, args.bootstrap_resamples, not args.no_plots)
        print(f"Analysis: {metadata['valid_runs']}/{metadata['planned_runs']} valid; {metadata['errors']} audit errors; output={args.output_dir or args.results_dir / 'analysis'}")
        return 1 if metadata["errors"] else 0
    except (OSError, ValueError, TypeError, KeyError, csv.Error) as error:
        print(f"Analysis error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
