# Experiment report assets

These assets accompany [the experiment report](../experiment-report.md). They are byte-identical copies of selected results from `code/output/experiment-study-20261009/`; [manifest.json](manifest.json) records each copied file's repository-relative source path, byte count and SHA-256. The original bundle retains all raw request/run tables, 152 main figure files, source archives and execution records. The copies here can be tracked with the report even though `code/output/` is Git-ignored.

| Directory | Contents and origin |
|---|---|
| [figures/](figures/) | Ten selected main-study figures, each in PNG and PDF, copied from `main/analysis/figures/`. |
| [tables/summary_readable.csv](tables/summary_readable.csv) | Compact results for all 196 main cells. |
| [tables/summary.csv](tables/summary.csv) | Complete summary metrics, intervals, defined/undefined counts, and separately labeled pooled ratios. |
| [tables/contrasts.csv](tables/contrasts.csv) | Hotspot-minus-uniform contrasts using verified matched seed runs. |
| [tables/probability_map_diagnostics.csv](tables/probability_map_diagnostics.csv) | Independently reconstructed configured probability maps: bounds, interface means and clipping. |
| [tables/rate_band_diagnostic.csv](tables/rate_band_diagnostic.csv) | Post-analysis checks of the changing seed counts at rate-band boundaries. |
| [metadata/main-audit.json](metadata/main-audit.json), [metadata/pilot-audit.json](metadata/pilot-audit.json) | Original audits: 3,960/3,960 main and 156/156 pilot runs valid; no missing, operationally failed or invalid runs. |
| [metadata/analysis.json](metadata/analysis.json) | Main analysis definitions, versions, source/input hashes and frozen provenance. |
| [metadata/rate_band_diagnostic.json](metadata/rate_band_diagnostic.json), [regeneration script](metadata/regenerate_rate_band_diagnostic.py) | Diagnostic definitions, traffic audits, input/script hashes and the exact original Python script. |

## Reading the results

The independent replication unit is a whole seed run. Main cells use 30 seeds (101–130) at 5–20%, 20 (101–120) at 25–40%, and 10 (101–110) at 45–70%; uniform 0% controls use 30. Pilot seeds 901–903 remain separate. Primary means give equal weight to defined seed metrics. Pooled delivery, latency and energy/completion use their separately retained numerator/denominator pairs; they can differ from equal-seed means.

Pointwise 95% intervals use a whole-seed percentile bootstrap with **10,000 resamples** and **PCG64 RNG seed 20261009**, using NumPy's linear quantiles. Matched contrasts resample paired seed blocks jointly after verifying exact generated traffic. CSV blanks and JSON nulls preserve undefined values; undefined resamples are excluded and counted. These intervals describe the sampled runs; constant delivery produces degenerate intervals and does not establish certain physical reliability or equivalence. Different seed counts can cause apparent rate-curve dips, as documented by the diagnostic.

The simulator uses initial-only faults, healthy horizontal links, unlimited routing, a fixed global traffic rate and reference architecture power coefficients. Layer changes alter load per router and exposure to the bottom-interface hotspots. Probability maps describe configured risk rather than realized defects. Reported latency and power characterize this model and workload; they require calibration for physical chip claims.

## Generation and reproduction

The recorded simulations were generated from repository root with:

```bash
python3 code/scripts/run_experiments.py --config code/config/experiments.pilot.json --jobs 1
python3 code/scripts/run_experiments.py --config code/config/experiments.main.json --jobs 4 --timeout-seconds 30
```

Use fresh output directories for a new sweep and retain the pilot review before the main run. See the [experiment plan](../experiment-plan.md) and the original bundle's `engineering/executed-commands.json` for validation, pilot review and complete execution records.

With the retained original raw bundle available, regenerate the main analysis from repository root:

```bash
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 python3 code/scripts/analyze_experiments.py --results-dir code/output/experiment-study-20261009/main --bootstrap-seed 20261009 --bootstrap-resamples 10000
python3 docs/experiment-results/metadata/regenerate_rate_band_diagnostic.py --analysis-dir code/output/experiment-study-20261009/main/analysis
```

Analysis dependencies are listed in `code/scripts/requirements-analysis.txt` (recorded NumPy 2.2.6 and Matplotlib 3.10.7; Python 3.13.5). The diagnostic needs NumPy, original `main/analysis/runs.json` and healthy-run request CSVs in adjacent `main/runs/`; it can otherwise fall back to the original exported absolute paths. The copied script defaults to its own directory, so **pass `--analysis-dir` explicitly**. This compact documentation bundle does not contain the required raw inputs and cannot regenerate results alone. Regeneration writes into the selected analysis directory; refresh documentation copies and their manifest after an intentional analysis update.
