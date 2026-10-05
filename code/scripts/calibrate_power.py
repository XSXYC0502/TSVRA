#!/usr/bin/env python3
"""Offline DSENT calibration: fit nonnegative component slopes, preserve evidence.

Uses only the Python standard library. The simulator reads the resulting INI;
this tool and DSENT are never invoked by a simulation or batch run.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import subprocess
import sys
from pathlib import Path

CODE_ROOT = Path(__file__).resolve().parents[1]
REVISION = "1b4bb98f5bfb96ca394b71e7ef3e9e500a1e3997"
SOURCE = f"https://github.com/mit-carbon/Graphite/tree/{REVISION}/contrib/dsent/dsent-core"
CONFIG = CODE_ROOT / "config" / "dsent_router.cfg"
RATES = (0.0, 0.08, 0.16, 0.32)
LABELS = ("event_rate_hz", "buffer_w", "crossbar_w", "control_w", "clock_w", "leakage_w", "total_w")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_sample(path: Path) -> dict[str, float]:
    text = path.read_text(encoding="utf-8")
    if re.search(r"(?:error|fail|cannot meet)", text, re.IGNORECASE):
        raise ValueError(f"DSENT reported an error in {path}")
    found = dict(re.findall(r"^tsvra_(\w+)=\s*([+\-\d.eE]+)\s*$", text, re.MULTILINE))
    if set(found) != set(LABELS):
        raise ValueError(f"Missing or unexpected calibration values in {path}")
    sample = {label: float(found[label]) for label in LABELS}
    if any(not math.isfinite(value) or value < 0 for value in sample.values()):
        raise ValueError(f"Nonfinite or negative DSENT output in {path}")
    return sample


def verify_reference_configuration() -> None:
    values = dict(re.findall(r"^([\w>\-]+)\s*=\s*([^\n#]*)", CONFIG.read_text(encoding="utf-8"), re.MULTILINE))
    reference = {
        "ModelName": "Router", "ElectricalTechModelFilename": "tech/tech_models/Bulk45LVT.model",
        "NumberInputPorts": "6", "NumberOutputPorts": "6", "NumberBitsPerFlit": "64",
        "NumberVirtualNetworks": "1", "NumberVirtualChannelsPerVirtualNetwork": "[8]",
        "NumberBuffersPerVirtualChannel": "[2]", "InputPort->BufferModel": "DFFRAM",
        "CrossbarModel": "MultiplexerCrossbar", "SwitchAllocator->ArbiterModel": "MatrixArbiter",
        "ClockTreeModel": "BroadcastHTree", "ClockTree->NumberLevels": "5", "ClockTree->WireLayer": "Global",
        "ClockTree->WireWidthMultiplier": "1.0", "IsPerformTimingOptimization": "true",
        "TimingOptimization->StartNetNames": "[*]",
    }
    if any(values.get(key, "").strip() != value for key, value in reference.items()) or float(values["Frequency"]) != 1e9:
        raise ValueError("This collector supports the declared 6-port, 64-bit, 8-VC, 16-buffer, 1GHz reference configuration")


def collect(root: Path, evidence: Path) -> dict:
    verify_reference_configuration()
    binary = root / "dsent.exe"
    if not binary.is_file():
        raise ValueError(f"Build the pinned DSENT source first: missing {binary}")
    revision = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=root, text=True
    ).strip()
    if revision != REVISION:
        raise ValueError(f"Expected DSENT source revision {REVISION}, got {revision}")
    subprocess.run(["git", "diff", "--quiet", "HEAD", "--", "."], cwd=root, check=True)
    evidence.mkdir(parents=True, exist_ok=True)
    entries = []
    for index, injection in enumerate(RATES):
        command = [str(binary), "-cfg", str(CONFIG), "-overwrite", f"InjectionRate={injection};"]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, check=True)
        name = f"sample_{index:02d}.txt"
        path = evidence / name
        path.write_text(result.stdout + result.stderr, encoding="utf-8")
        read_sample(path)
        entries.append({"file": name, "injection_rate_per_port": injection, "sha256": digest(path)})
    manifest = {
        "schema_version": 1,
        "source_url": SOURCE,
        "source_revision": revision,
        "source_version": "DSENT 0.9 (22 May 2012)",
        "technology": "Bulk45LVT",
        "technology_sha256": digest(root / "tech" / "tech_models" / "Bulk45LVT.model"),
        "configuration_sha256": digest(CONFIG),
        "frequency_hz": 1e9,
        "voltage_v": 1.0,
        "temperature_k": 340,
        "router_ports": 6,
        "flit_bits": 64,
        "virtual_channels_per_port": 8,
        "buffers_per_virtual_channel": 2,
        "clock_tree_levels": 5,
        "timing_optimization": True,
        "samples": entries,
    }
    (evidence / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def fit(evidence: Path) -> dict:
    verify_reference_configuration()
    manifest = json.loads((evidence / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("source_revision") != REVISION or manifest.get("technology") != "Bulk45LVT":
        raise ValueError("This calibration accepts only the declared pinned Bulk45LVT reference")
    if manifest.get("configuration_sha256") != digest(CONFIG):
        raise ValueError("Calibration configuration differs from the evidence manifest")
    required_reference = {"frequency_hz": 1e9, "voltage_v": 1.0, "temperature_k": 340,
                          "router_ports": 6, "flit_bits": 64, "virtual_channels_per_port": 8,
                          "buffers_per_virtual_channel": 2, "clock_tree_levels": 5, "timing_optimization": True}
    if any(manifest.get(key) != value for key, value in required_reference.items()):
        raise ValueError("Imported evidence does not describe the declared equivalent reference router")
    samples = []
    for entry in manifest["samples"]:
        path = evidence / entry["file"]
        if digest(path) != entry["sha256"]:
            raise ValueError(f"Calibration evidence checksum changed: {path}")
        sample = read_sample(path)
        rate = entry["injection_rate_per_port"]
        if not isinstance(rate, (int, float)) or not math.isfinite(rate) or rate < 0:
            raise ValueError("Invalid declared injection rate")
        if not math.isclose(sample["event_rate_hz"], rate * 6e9, rel_tol=1e-8, abs_tol=1e-8):
            raise ValueError("Declared injection rate disagrees with DSENT event rate")
        samples.append(sample)
    if len({sample["event_rate_hz"] for sample in samples}) < 3 or sum(sample["event_rate_hz"] == 0 for sample in samples) != 1:
        raise ValueError("Supply one true idle point and at least two nonzero activity points")
    idle = next(sample for sample in samples if sample["event_rate_hz"] == 0)
    idle_w = idle["total_w"]
    idle_components = idle["clock_w"] + idle["leakage_w"]
    if not math.isclose(idle_w, idle_components, rel_tol=1e-6, abs_tol=1e-12):
        raise ValueError("Idle total must equal DSENT clock power plus explicit DSENT leakage")
    if any(idle[field] != 0 for field in ("buffer_w", "crossbar_w", "control_w")):
        raise ValueError("Unexpected event activity in the idle point")
    sum_square_rates = sum(sample["event_rate_hz"] ** 2 for sample in samples)
    if not sum_square_rates or not math.isfinite(sum_square_rates):
        raise ValueError("Invalid calibration event rates")
    coefficients = {}
    for component in ("buffer", "crossbar", "control"):
        slope_j = sum(sample["event_rate_hz"] * sample[f"{component}_w"] for sample in samples) / sum_square_rates
        if not math.isfinite(slope_j) or slope_j < 0:
            raise ValueError(f"Invalid {component} slope")
        coefficients[f"router_{component}_event_pj"] = slope_j * 1e12
    max_relative_error = 0.0
    for sample in samples:
        if not math.isclose(sample["clock_w"], idle["clock_w"], rel_tol=1e-6, abs_tol=1e-12):
            raise ValueError("Clock power changed across the calibration sweep")
        if not math.isclose(sample["leakage_w"], idle["leakage_w"], rel_tol=1e-6, abs_tol=1e-12):
            raise ValueError("Leakage changed across the calibration sweep")
        predicted = idle_components + sample["event_rate_hz"] * sum(coefficients.values()) * 1e-12
        error = abs(predicted - sample["total_w"]) / max(sample["total_w"], 1e-12)
        max_relative_error = max(max_relative_error, error)
    if max_relative_error > 1e-5:
        raise ValueError(f"Reference is not linear enough for this event model: relative error {max_relative_error}")
    return {
        "parameter_version": "tsvra-power-v1-dsent09-bulk45",
        "calibration_status": "dsent_bulk45_reference",
        "source": manifest,
        "router_idle_mw": idle_components * 1e3,
        "router_clock_mw": idle["clock_w"] * 1e3,
        "router_leakage_mw": idle["leakage_w"] * 1e3,
        **coefficients,
        "maximum_relative_reconstruction_error": max_relative_error,
        "samples": samples,
        "limitations": [
            "Analytical DSENT bulk45 at 340 K; not the SOI45 SPICE validation router in the NOCS2012 paper.",
            "The fixed idle term includes continuously running router clock distribution; no adaptive whole-router gating.",
            "One read+write, one crossbar traversal, one two-stage switch allocation per launched hop.",
            "No independent VC allocation/credit bookkeeping calibration; see DSENT0.9 known limitations.",
            "The simulator's horizontal/vertical cycle delays and 15-cycle router service are architectural assumptions.",
        ],
    }


def write_ini(path: Path, report: dict) -> None:
    coefficients = {key: report[key] for key in (
        "router_idle_mw", "router_buffer_event_pj", "router_crossbar_event_pj", "router_control_event_pj"
    )}
    parameters = {
        "parameter_version": report["parameter_version"],
        "calibration_status": report["calibration_status"],
        "technology": "45nm_bulk_CMOS_Bulk45LVT_340K",
        "router_source": SOURCE,
        "link_source": "DSENT_NOCS2012_TableI_KimLim_SLIP2010_Table2",
        "calibration_source_revision": REVISION,
        "calibration_configuration": "6ports_64bit_8VC_2buffers_per_VC_16per_port_clockHTree5_340K_1V_1GHz",
        "frequency_hz": 1e9,
        "voltage_v": 1.0,
        "flit_bits": 64,
        "physical_signal_tsvs": 64,
        "total_toggle_probability": 0.5,
        **coefficients,
        "horizontal_link_length_um": 1000.0,
        "horizontal_capacitance_ff_per_um": 0.150,
        "tsv_width_um": 2.0,
        "tsv_height_um": 20.0,
        "tsv_capacitance_ff": 67.5,
        "redundant_vertical_capacitance_multiplier": 1.0,
    }
    lines = [
        "# Generated OFFLINE by code/scripts/calibrate_power.py; editable simulation inputs.",
        "# Full evidence: code/scripts/power_calibration/manifest.json and fit.json.",
        "# Router is actual DSENT Bulk45LVT at 340 K; it is not the SOI45 paper validation router.",
        f"# Router idle: clock={report['router_clock_mw']:.10g} mW + DSENT leakage={report['router_leakage_mw']:.10g} mW.",
        "# Horizontal length 1000 um is a declared nominal geometry assumption.",
        "# Wire 0.150 fF/um comes from DSENT paper Table I; it is not a fitted Bulk45LVT wire model.",
        "# TSV square width/height are provenance; capacitance is explicit and does not auto-scale with geometry.",
        "# Probability counts both 01 and 10 transitions; E=.5*C*V^2*64*p(toggle).",
        "# Frequency maps simulation cycles to seconds; it does not establish physical hop timing.",
        "# Router coefficients apply at their calibrated operating point; recalibrate after V/f changes.",
        "# Keep calibration_status/source accurate if you replace assumptions or coefficients.",
        "",
    ]
    lines.extend(f"{key} = {value:.17g}" if isinstance(value, float) else f"{key} = {value}" for key, value in parameters.items())
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--dsent-root", type=Path, help="Built pinned contrib/dsent/dsent-core directory")
    source.add_argument("--input-dir", type=Path, help="Import previously captured idle+activity evidence")
    parser.add_argument("--evidence-dir", type=Path, default=CODE_ROOT / "scripts" / "power_calibration")
    parser.add_argument("--output", type=Path, default=CODE_ROOT / "config" / "power_model.ini")
    args = parser.parse_args()
    evidence = args.input_dir.resolve() if args.input_dir else args.evidence_dir.resolve()
    try:
        if args.dsent_root:
            collect(args.dsent_root.resolve(), evidence)
        report = fit(evidence)
        write_ini(args.output.resolve(), report)
        (evidence / "fit.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        print(f"Wrote {args.output}; idle={report['router_idle_mw']:.8g}mW/router, event={sum(report[key] for key in report if key.endswith('_event_pj')):.8g}pJ")
        return 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"Calibration error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
