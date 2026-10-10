#!/usr/bin/env python3
"""Regenerate the post-analysis rate-band diagnostic from retained main outputs.

Dependencies: NumPy and Python standard library. Default input/output directory
is this script's directory; --analysis-dir can select another preserved bundle.
The original design, simulator outputs, and primary analysis remain unchanged.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

import numpy as np

SCENARIOS = ("U", "H1", "H2", "H12")
RNG_SEED = 20261009
RESAMPLES = 10000


def json_safe(value):
    if isinstance(value, dict):
        return {str(k): json_safe(v) for k, v in value.items()}
    if isinstance(value, (tuple, list)):
        return [json_safe(v) for v in value]
    if isinstance(value, np.generic):
        return json_safe(value.item())
    if isinstance(value, float) and not math.isfinite(value):
        return None
    return value


def write_json(path, value):
    path.write_text(json.dumps(json_safe(value), indent=2, allow_nan=False) + "\n", encoding="utf-8")


def write_csv(path, rows):
    fields = list(dict.fromkeys(k for row in rows for k in row))
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({k: json.dumps(v, separators=(",", ":")) if isinstance(v, (list, dict)) else v
                             for k, v in json_safe(row).items()})


def bootstrap_weights(rng):
    indices = rng.integers(0, 10, size=(RESAMPLES, 10))
    weights = np.zeros((RESAMPLES, 10), dtype=float)
    np.add.at(weights, (np.arange(RESAMPLES)[:, None], indices), 1)
    return weights


def summarize_values(values, weights):
    values = np.array([np.nan if v is None else v for v in values], dtype=float)
    defined = np.isfinite(values)
    numerator = weights @ np.nan_to_num(values, nan=0.0)
    denominator = weights @ defined.astype(float)
    distribution = np.divide(numerator, denominator, out=np.full(numerator.shape, np.nan), where=denominator > 0)
    bootstrap_defined = distribution[np.isfinite(distribution)]
    lo, hi = (float(x) for x in np.quantile(bootstrap_defined, [0.025, 0.975], method="linear"))
    finite = values[defined]
    return {"mean": float(finite.mean()), "sd": float(finite.std(ddof=1)),
            "ci95_low": lo, "ci95_high": hi, "defined_runs": int(defined.sum()),
            "undefined_runs": int((~defined).sum()), "bootstrap_defined_resamples": len(bootstrap_defined),
            "degenerate_interval": int(lo == hi)}


def regenerate(directory):
    source = directory / "runs.json"
    source_bytes = source.read_bytes()
    runs = json.loads(source_bytes)
    rows, matching_audit = [], []
    rng = np.random.default_rng(RNG_SEED)

    def selected(layer, rate, name="U", limit=130):
        return sorted([x for x in runs if x["num_layers"] == layer and x["fault_scenario"] == name
                       and x["configured_failure_fraction"] == rate and x["actual_seed"] <= limit],
                      key=lambda x: x["actual_seed"])

    def mean(group, key):
        return statistics.fmean(x[key] for x in group)

    def verify_pair(before, after):
        if [x["actual_seed"] for x in before] != [x["actual_seed"] for x in after]:
            raise ValueError("Unmatched seed list")
        for b, h in zip(before, after):
            same = b["traffic_sha256"] == h["traffic_sha256"]
            matching_audit.append({"seed": b["actual_seed"], "before_run_id": b["run_id"],
                                   "after_run_id": h["run_id"], "exact_traffic_equal": same})
            if not same:
                raise ValueError("Traffic mismatch")

    def mean_difference(before, after, key, weights):
        values = [h[key] - b[key] for b, h in zip(before, after)]
        return {f"paired_{key}_{k}": v for k, v in summarize_values(values, weights).items()}

    for layer in range(2, 6):
        weights = bootstrap_weights(rng)
        healthy30 = selected(layer, 0)
        healthy20 = selected(layer, 0, limit=120)
        healthy10 = selected(layer, 0, limit=110)
        f70 = selected(layer, .7)
        verify_pair(healthy10, f70)
        for run in healthy30:
            # The adjacent runs directory also permits moving a retained bundle.
            requests_path = directory.parent / "runs" / f"{run['run_id']}_requests.csv"
            if not requests_path.is_file():
                requests_path = Path(run["output_prefix"] + "_requests.csv")
            with requests_path.open(encoding="utf-8", newline="") as handle:
                requests = list(csv.DictReader(handle))
            manhattan = [abs(int(r["End X"]) - int(r["Start X"])) + abs(int(r["End Y"]) - int(r["Start Y"]))
                         for r in requests]
            if any(int(r["Horizontal Distance"]) != m for r, m in zip(requests, manhattan)):
                raise ValueError("Healthy path not Manhattan")
            run["source_destination_manhattan_mean"] = statistics.fmean(manhattan)
        row = {"diagnostic": "U70 versus healthy seed prefixes", "num_layers": layer, "fault_scenario": "U",
               "before_rate": 0, "after_rate": .7, "before_published_seed_count": 30, "after_seed_count": 10,
               "matched_seed_count": 10, "matched_seeds": list(range(101, 111)),
               "healthy_latency_mean_30_seeds": mean(healthy30, "average_latency_cycles"),
               "healthy_latency_mean_20_seeds": mean(healthy20, "average_latency_cycles"),
               "healthy_latency_mean_10_seeds": mean(healthy10, "average_latency_cycles"),
               "U70_latency_mean_10_seeds": mean(f70, "average_latency_cycles"),
               "published_heterogeneous_delta_cycles": mean(f70, "average_latency_cycles") - mean(healthy30, "average_latency_cycles"),
               "healthy_manhattan_mean_30_seeds": mean(healthy30, "source_destination_manhattan_mean"),
               "healthy_manhattan_mean_20_seeds": mean(healthy20, "source_destination_manhattan_mean"),
               "healthy_manhattan_mean_10_seeds": mean(healthy10, "source_destination_manhattan_mean"),
               "U70_completed_horizontal_mean_10_seeds": mean(f70, "average_completed_horizontal_distance"),
               "healthy_published_run_ids": [x["run_id"] for x in healthy30],
               "before_matched_run_ids": [x["run_id"] for x in healthy10],
               "after_matched_run_ids": [x["run_id"] for x in f70]}
        row.update(mean_difference(healthy10, f70, "average_latency_cycles", weights))
        row.update(mean_difference(healthy10, f70, "average_completed_horizontal_distance", weights))
        rows.append(row)
        for name in SCENARIOS:
            before20 = selected(layer, .4, name)
            before10 = selected(layer, .4, name, 110)
            after10 = selected(layer, .45, name)
            verify_pair(before10, after10)
            row = {"diagnostic": "40-to-45 percent seed-band boundary", "num_layers": layer, "fault_scenario": name,
                   "before_rate": .4, "after_rate": .45, "before_published_seed_count": 20, "after_seed_count": 10,
                   "matched_seed_count": 10, "matched_seeds": list(range(101, 111)),
                   "before_published_latency_mean_20_seeds": mean(before20, "average_latency_cycles"),
                   "before_matched_latency_mean_10_seeds": mean(before10, "average_latency_cycles"),
                   "after_latency_mean_10_seeds": mean(after10, "average_latency_cycles"),
                   "published_heterogeneous_delta_cycles": mean(after10, "average_latency_cycles") - mean(before20, "average_latency_cycles"),
                   "seed_subset_shift_at_fixed_f40_cycles": mean(before10, "average_latency_cycles") - mean(before20, "average_latency_cycles"),
                   "before_published_run_ids": [x["run_id"] for x in before20],
                   "before_matched_run_ids": [x["run_id"] for x in before10],
                   "after_matched_run_ids": [x["run_id"] for x in after10]}
            row.update(mean_difference(before10, after10, "average_latency_cycles", weights))
            row.update(mean_difference(before10, after10, "average_completed_horizontal_distance", weights))
            rows.append(row)

    script = Path(__file__).resolve()
    metadata = {"schema_version": 1,
                "purpose": "Disposition of visible seed-band sampling dip; post-analysis diagnostic, original matrix/results unchanged",
                "source": "runs.json and identified healthy raw request CSVs",
                "source_runs_sha256": hashlib.sha256(source_bytes).hexdigest(),
                "definitions": {
                    "published_heterogeneous_delta_cycles": "after cell mean minus before published cell mean; different seed counts",
                    "paired_metric_mean": "mean(after seed metric minus before seed metric) using exact common seeds101..110",
                    "seed_subset_shift_at_fixed_f40_cycles": "f40 mean on seeds101..110 minus same cell mean on seeds101..120",
                    "healthy_manhattan_mean": "per-run mean abs(End X-Start X)+abs(End Y-Start Y), then equally weighted across selected seed runs; each healthy request checked to match exported Horizontal Distance"},
                "identity": "published band delta = fixed-f40 seed subset shift + paired f40-to-f45 delta",
                "interval_method": "pointwise whole-seed percentile bootstrap10000 resamples; NumPy linear quantiles; diagnostic comparisons share whole-seed blocks across scenarios within each L",
                "rng_seed": RNG_SEED, "numpy_version": np.__version__,
                "limitations": "Post-analysis descriptive anomaly diagnostic, not simultaneous significance. Original rate-band repetition schedule remains intact.",
                "regeneration_script": script.name,
                "regeneration_script_sha256": hashlib.sha256(script.read_bytes()).hexdigest(),
                "regeneration_command": ["python3", str(script), "--analysis-dir", str(directory)],
                "traffic_audit": matching_audit, "rows": rows}
    write_json(directory / "rate_band_diagnostic.json", metadata)
    write_csv(directory / "rate_band_diagnostic.csv", rows)
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--analysis-dir", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    metadata = regenerate(args.analysis_dir.resolve())
    print(f"Regenerated {len(metadata['rows'])} diagnostic rows; {len(metadata['traffic_audit'])} verified traffic comparisons; RNG {RNG_SEED}; resamples {RESAMPLES}")


if __name__ == "__main__":
    main()
