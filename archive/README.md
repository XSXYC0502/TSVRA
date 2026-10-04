# Original materials and history

Files are preserved for provenance, outside the active simulation path.

- `power/TSVRA_功耗模型最终方案与数据_2026-08-13/`: original 12,000-row synthetic dataset, spreadsheet, trained tree surrogate, metrics, scripts and source notes. The original label-generation script was not supplied. This model is not required by the explicit runtime model.
- `results/`: unchanged historical experiment reports. Some did not save full settings; original metadata is retained rather than reconstructed.
- `docs/`: previous design notes and run instructions. Current instructions are in the repository root and `docs/`.
- `scripts/`: legacy runners, replaced by `experiments.py`.
- `manifest.json`: relative paths, byte lengths and SHA-256 values recorded during cleanup.

Original package caveats: 0.76 mW/router was a residual of a paper's components, not measured idle power. ±20% bounds were deterministic, not confidence intervals. Correct TSV paper title: *Through-Silicon-Via-aware Delay and Power Prediction Model for Buffered Interconnects in 3D ICs* (Kim and Lim, SLIP 2010). Current calibration is in `docs/power-model.md`.
