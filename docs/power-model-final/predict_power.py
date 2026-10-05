from __future__ import annotations

import argparse
from pathlib import Path

import joblib
import pandas as pd


ROOT = Path(__file__).resolve().parent
DEFAULT_MODEL = ROOT / "models" / "network_power_hist_gradient_boosting.joblib"
FEATURES = [
    "router_count",
    "num_layers",
    "flit_width_bit",
    "injection_rate_flit_per_cycle_router",
    "toggle_rate_transition_per_bit_flit",
    "hotspot_weight",
    "avg_horizontal_hops",
    "avg_vertical_hops",
    "redundant_vertical_fraction",
    "initial_fault_rate",
    "cluster_intensity",
    "reroute_rate",
    "horizontal_length_um",
    "tsv_width_um",
    "tsv_height_um",
    "tsv_liner_um",
    "tsv_total_cap_fF",
    "tsv_resistance_ohm",
    "traffic_model",
    "tsv_profile",
]


def main() -> None:
    parser = argparse.ArgumentParser(description="Predict TSVRA 45 nm network power from a CSV file.")
    parser.add_argument("input_csv", type=Path, help="CSV containing the 20 required feature columns")
    parser.add_argument("-o", "--output", type=Path, default=Path("power_predictions.csv"))
    parser.add_argument("--model", type=Path, default=DEFAULT_MODEL)
    args = parser.parse_args()

    data = pd.read_csv(args.input_csv)
    missing = [column for column in FEATURES if column not in data.columns]
    if missing:
        raise ValueError(f"Missing required columns: {', '.join(missing)}")

    model = joblib.load(args.model)
    result = data.copy()
    result["predicted_network_total_power_mW"] = model.predict(data[FEATURES])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    result.to_csv(args.output, index=False)
    print(f"Wrote {len(result)} predictions to {args.output.resolve()}")


if __name__ == "__main__":
    main()
