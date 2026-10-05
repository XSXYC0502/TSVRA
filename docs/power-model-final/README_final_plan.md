# TSVRA power-prediction research package (45 nm)

## Relationship to the current project

This is the retained August 2026 research package: its complete synthetic dataset, spreadsheet, trained regressor, scripts, and evaluation outputs are kept together here. Run its scripts from `docs/power-model-final/` with the dependencies in `requirements.txt`.

The current simulator uses an explicit event/idle energy model, configured by the repository-root `power_model.ini` and documented in [the runtime power model](../power-model.md). It does not load this package or call its regressor. Current offline router calibration evidence is in `tools/power_calibration/`, relative to the repository root. The two workflows use different power assumptions, so the saved regression metrics below do not validate the current simulator's coefficients.

The original script that generated the synthetic labels was not supplied. `train_power_model.py` retrains a regressor from the supplied CSV; it does not regenerate the dataset. The data split and training seed are fixed, but the dependency versions are lower bounds rather than a pinned environment, so exact retraining results can vary with library versions.

## Original dataset scope

At **45 nm, 1.0 V, and 1 GHz**, this package takes TSVRA network size, traffic, faults and clusters, rerouting, and TSV geometry as inputs and predicts `network_total_power_mW` for the whole network.

The supplied notes describe architecture-level labels generated from interconnect switching-energy formulas, published TSV capacitance parameters, and a DSENT paper operating point. They support studying this synthetic dataset and its surrogate. They are not measured silicon data or sign-off power. The router paper's 0.76 mW residual after subtracting buffer/crossbar/control terms is not an independently measured idle or leakage coefficient. The dataset's `label_lower_20pct_mW` and `label_upper_20pct_mW` are deterministic ±20% bounds, not confidence intervals.

## Deliverables

- `TSVRA_45nm_power_model_final_training_data.xlsx`: the plan, data dictionary, full training data, and calibration sources.
- `data_sources.md`: literature sources, adopted parameters, generation formulas, and the limits of use.
- `data/tsvra_power_training_45nm.csv`: 12,000 samples ready for training.
- `data/dataset_manifest.json`: data version, split, and ranges.
- `models/network_power_hist_gradient_boosting.joblib`: the trained model.
- `train_power_model.py`: retrains from the CSV and writes evaluation results.
- `predict_power.py`: batch prediction for a new TSVRA parameter table.
- `results/metrics.json`: validation-set and test-set metrics.
- `results/test_predictions.csv`: per-row test-set predictions.
- `results/feature_importance.csv`: permutation feature importance.

## Data and Task Definition

- Sample count: 12,000
- Split: train 8,393; validation 1,792; test 1,815
- Inputs: 18 numeric features and 2 categorical features
- Primary label: `network_total_power_mW`
- Fixed technology domain: 45 nm, 1.0 V, 1 GHz
- Random seed: 20260813
- Label range: 17.835–20,647.149 mW, mean 1,696.321 mW

Train the primary label first. Do not feed the intermediate power components from the same table in as inputs, or the label will leak.

## Checked Baseline

The final model is `HistGradientBoostingRegressor`:

- Validation set: R² 0.99685, MAE 60.76 mW, MAPE 5.77%
- Test set: R² 0.99615, MAE 58.37 mW, MAPE 6.22%

These metrics measure how well the model reproduces the supplied synthetic labels. They are not the error relative to measurements from a real chip.

## How to Use

From this directory:

Retraining writes over `models/network_power_hist_gradient_boosting.joblib` and the three evaluation files in `results/`. Run the training workflow in a copy of this package if you want to keep the supplied model and evaluation evidence.

```powershell
python -m pip install -r requirements.txt
python train_power_model.py
```

To predict a new CSV that contains the required feature columns:

```powershell
python predict_power.py new_samples.csv -o power_predictions.csv
```

The safest template for a new sample is to copy the header and one row from the training CSV, then replace the input parameters. `predict_power.py` ignores extra columns and reports a clear error when a required column is missing.

## Modeling Basis

The original notes scale router power from a DSENT paper's 45 nm, 64-bit, 1 GHz loaded operating point. This differs from the current runtime model's independently collected DSENT bulk reference. Horizontal and vertical interconnects use the switching-energy relation `0.5 × C × V²`. TSV capacitance uses geometry cases from Kim and Lim. The original notes describe fault rate, cluster intensity, and reroute rate as affecting effective injection, hop count, and redundant vertical transfers; the absent label-generation script limits exact reconstruction of that process.

Sources:

- DSENT: A Tool Connecting Emerging Photonics with Electronics for Opto-Electronic Networks-on-Chip — https://projects.csail.mit.edu/wiki/pub/LSPgroup/PublicationList/dsent_nocs12.pdf
- Through-Silicon-Via-aware Delay and Power Prediction Model for Buffered Interconnects in 3D ICs (Kim and Lim, SLIP 2010) — https://gtcad.gatech.edu/www/papers/p25-kim.pdf

## Limits and Later Upgrades

Do not extrapolate the 45 nm model directly to another process, voltage, or frequency. When Vivado, board-level, or chip measurements become available, keep the existing 20 input fields, add `measured_power_mW`, and either retrain on the measured labels or calibrate the residual of the current predictions. That upgrades the model to one based on experimental data.
