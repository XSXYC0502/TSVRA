# Historical experiment reports

These tables preserve earlier TSVRA experiment results. They are reference material, rather than benchmarks of the current implementation.

| Location | Retained content |
| --- | --- |
| `experiment_method_summary.md` | Failure-rate/layer comparison reporting horizontal distance and delivery success |
| `threshold_calibration_full_t20/` | Full comparison with the horizontal-distance cap identified as 20 by its directory name |
| `threshold_calibration_short/t12/` through `t22/` | Short comparisons for caps identified as 12, 14, 16, 18, 20, and 22 |
| `batch_cap_smoke/`, `batch_default20_smoke/`, `batch_smoke_md/` | Single-point smoke-run summaries |

The reports contain aggregate tables only. They do not retain complete effective settings, source revisions, seed lists, individual requests, or raw run outputs. Directory names provide some context, but they are insufficient to reconstruct every run. In particular, the cap in a directory name should not be used to infer the simulator's present default.

`Correct Rate` is the legacy label for delivery success; it does not establish correctness of the physical model. Current channel accounting, failure handling, traffic settings, router service time, and drain behavior must be checked before comparing these values with new runs. These tables alone do not establish that one redundancy layout is superior, or that layouts are equivalent.

For new results, use [`experiments.py`](../../experiments.py) and [`experiments/example.json`](../../experiments/example.json). The current runner retains batch settings, per-run logs and exports, actual seeds, and aggregate reports. See [the current experiment workflow](../power-model.md#reporting-and-batch-experiments).
