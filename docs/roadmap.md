# TSVRA roadmap

This roadmap describes follow-up work for the current codebase. Proposed work below is not an implemented feature or a demonstrated research result.

## Implemented baseline

- C++20 simulation with shared, corner4, and no redundancy; logical vertical channels at the lower interface; configurable traffic, link/router delays, and routing constraints.
- Uniform and fixed Gaussian hotspot failure distributions, separately configured initial/runtime probabilities, geometric runtime lifetimes, and reproducible RNG streams.
- A common native/JSON simulation loop, completion drain, routing/bypass diagnostics, and energy accounting for actual launched hops and continuous router idle activity.
- Nuxt visualization with shared launch validation, WebSocket/native lifecycle management, preview geometry for every layout, bounded histories, and pause/step/speed controls.
- Editable JSON experiment matrices through `experiments.py`, parallel runs, preserved per-run evidence, accounting validation, and aggregate reports.
- Native routing/accounting, failure, and power tests; native CLI/JSON integration checks; web validation/state tests; and a real WebSocket/native integration check.
- Current model, parameter, architecture, protocol, and calibration documentation. Native link defaults and the web/example link profile intentionally differ; [the parameter reference](hardcoded-parameters.md) explains the profiles.

## Research validation

The next research step is to design and run a documented redundancy comparison. The [historical tables](historical-results/README.md) lack sufficient metadata to establish the current model's result. A previous roadmap's claim that all layouts were equivalent should not be treated as a present conclusion.

1. Define the comparison question and hold grid size, configured traffic probability, fault settings, generation horizon, delays, and seed sets fixed across layouts. Matching seeds currently does not guarantee matching request traces: rejecting designated spare endpoints consumes different traffic RNG draws for different layouts. An exact workload comparison would require trace replay or a deliberately layout-independent endpoint generator, neither of which is implemented.
2. Include uniform and hotspot faults, several failure rates, and horizontal-distance constraints. Record actual channel populations and spare fractions when comparing layouts.
3. Report delivery success, completed-flit latency, horizontal/vertical hops, bypass encounters and outcomes, and energy. Separate horizon state from final drained results. Reroute events are available in JSON streaming, but a count for the CLI/batch reports would require an additional exported counter or separate event analysis.
4. Analyze per-seed variability and uncertainty before claiming a layout benefit or a null result. The runner currently supplies run means and pooled ratios, rather than a statistical hypothesis-testing workflow.
5. Keep physical validation separate: the failure distribution is a declared simulation choice and the power model is an architecture estimate. Measured defect or power data would support later calibration.

Useful future model studies include the effect of endpoint placement, constrained horizontal routing, channel contention, and spare access. Change the model only for an explicit research question, with matching documentation and regression checks. Topology, traffic, and latency settings should not be adjusted merely to obtain a preferred layout ranking.

## Engineering follow-up

| Proposed work | Current starting point | Completion evidence |
| --- | --- | --- |
| Continuous integration across supported platforms | CMake/CTest and npm scripts exist; no checked-in CI workflow | Native build/tests and web tests/build run automatically; Windows behavior is checked on Windows |
| Versioned benchmark scenarios | JSON experiment matrices and retained run outputs exist | A small documented matrix can be reproduced from a clean checkout |
| Stronger experiment provenance | Effective configuration and actual seeds are exported | Batch outputs also identify source revision, toolchain, and start time |
| Research analysis tooling | Per-run CSVs and pooled summaries exist | Analysis reports uncertainty, supports paired-seed comparisons, and can be regenerated from retained outputs |
| UI result export and scenario presets | Live dashboard and controls exist | Users can export a run's effective settings and result bundle, and load documented presets |
| Large-grid performance evaluation | Instanced meshes, bounded histories, and web configuration limits exist | Reproducible measurements identify native memory/runtime and browser rendering limits |

The retained [power-prediction package](power-model-final/README_final_plan.md) remains available for separate model-comparison work. Integrating its regressor into the simulator, adding reinforcement learning, or adding new redundancy layouts requires a new design and validation plan; none is implemented by preserving those materials.
