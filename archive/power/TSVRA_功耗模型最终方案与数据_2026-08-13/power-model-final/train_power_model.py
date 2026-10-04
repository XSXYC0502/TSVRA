from __future__ import annotations

import json
from pathlib import Path

import joblib
import numpy as np
import pandas as pd
from sklearn.compose import ColumnTransformer
from sklearn.ensemble import HistGradientBoostingRegressor
from sklearn.inspection import permutation_importance
from sklearn.metrics import mean_absolute_error, mean_squared_error, r2_score
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import OneHotEncoder


ROOT = Path(__file__).resolve().parent
DATA = ROOT / "data" / "tsvra_power_training_45nm.csv"
MODEL_DIR = ROOT / "models"
RESULT_DIR = ROOT / "results"

TARGET = "network_total_power_mW"
NUMERIC_FEATURES = [
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
]
CATEGORICAL_FEATURES = ["traffic_model", "tsv_profile"]
FEATURES = NUMERIC_FEATURES + CATEGORICAL_FEATURES


def metrics(y_true: pd.Series, y_pred: np.ndarray) -> dict[str, float]:
    abs_pct = np.abs((y_true.to_numpy() - y_pred) / np.maximum(np.abs(y_true.to_numpy()), 1e-9))
    return {
        "n": int(len(y_true)),
        "mae_mW": float(mean_absolute_error(y_true, y_pred)),
        "rmse_mW": float(mean_squared_error(y_true, y_pred) ** 0.5),
        "r2": float(r2_score(y_true, y_pred)),
        "mape_pct": float(abs_pct.mean() * 100),
        "median_ape_pct": float(np.median(abs_pct) * 100),
    }


def main() -> None:
    MODEL_DIR.mkdir(parents=True, exist_ok=True)
    RESULT_DIR.mkdir(parents=True, exist_ok=True)
    df = pd.read_csv(DATA)

    train = df[df["split"] == "train"].copy()
    validation = df[df["split"] == "validation"].copy()
    test = df[df["split"] == "test"].copy()

    preprocessor = ColumnTransformer(
        transformers=[
            ("numeric", "passthrough", NUMERIC_FEATURES),
            ("categorical", OneHotEncoder(handle_unknown="ignore", sparse_output=False), CATEGORICAL_FEATURES),
        ],
        remainder="drop",
    )
    regressor = HistGradientBoostingRegressor(
        max_iter=350,
        learning_rate=0.08,
        max_leaf_nodes=31,
        l2_regularization=0.1,
        random_state=20260813,
    )
    model = Pipeline([("preprocess", preprocessor), ("regressor", regressor)])
    model.fit(train[FEATURES], train[TARGET])

    report = {
        "target": TARGET,
        "features": FEATURES,
        "model": "HistGradientBoostingRegressor",
        "random_state": 20260813,
        "validation": metrics(validation[TARGET], model.predict(validation[FEATURES])),
        "test": metrics(test[TARGET], model.predict(test[FEATURES])),
        "scope": "45nm, 1.0V, 1GHz; physics-plus-DSENT-calibrated labels",
        "warning": "Not silicon-measured signoff power. Do not extrapolate across process, voltage, or frequency.",
    }

    test_pred = model.predict(test[FEATURES])
    predictions = test[["sample_id", TARGET]].copy()
    predictions["predicted_network_total_power_mW"] = test_pred
    predictions["absolute_error_mW"] = np.abs(predictions[TARGET] - test_pred)
    predictions["absolute_percentage_error_pct"] = (
        predictions["absolute_error_mW"] / predictions[TARGET].clip(lower=1e-9) * 100
    )

    perm = permutation_importance(
        model,
        test[FEATURES],
        test[TARGET],
        n_repeats=5,
        random_state=20260813,
        n_jobs=-1,
        scoring="r2",
    )
    importances = pd.DataFrame(
        {
            "feature": FEATURES,
            "importance": perm.importances_mean,
            "importance_std": perm.importances_std,
        }
    ).sort_values("importance", ascending=False)

    joblib.dump(model, MODEL_DIR / "network_power_hist_gradient_boosting.joblib")
    (RESULT_DIR / "metrics.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    predictions.to_csv(RESULT_DIR / "test_predictions.csv", index=False)
    importances.to_csv(RESULT_DIR / "feature_importance.csv", index=False)
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
