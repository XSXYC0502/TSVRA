# TSVRA Roadmap

## Purpose

This roadmap is based on the current repository state, not on the older README alone. The codebase already contains three meaningful layers of work:

1. A C++ simulator with configurable redundancy layouts, clustered failure injection, JSON streaming, CSV export, and a basic test target.
2. A Nuxt 4 visualization app that streams live simulation state over WebSocket and renders dashboard, 3D grid, heatmap, and failure views.
3. A research/experiment track that already produced a negative result: under the current model, redundancy layout does not materially change outcomes.

Because of that third point, the roadmap must prioritize scientific validity and model clarity before adding more surface area.

## Current State Snapshot

### What is already implemented

- Native simulator in `src/` and `include/` with:
  - configurable defaults in `Config`
  - redundancy modes: `shared`, `corner4`, `none`
  - failure models: `uniform`, `clustered`
  - congestion/risk parameters: `lambda1`, `lambda2`
  - path constraints: `max_path_length`, `reliability_min`
  - JSON streaming mode for web integration
- Web stack in `web-nuxt/` with:
  - Nitro WebSocket bridge to `bin/tsvra`
  - lockstep ack protocol
  - Pinia state store
  - dashboard + 3D grid + heatmap + failure views
- Experiment artifacts in `experiment-results/` and `refine-logs/`
- Working native tests via `ctest`
- Built binaries already present in `bin/`

### What is drifting or incomplete

- `README.md` and parts of `docs/` do not match the current simulator defaults or features.
- The frontend still hardcodes preview/form defaults that differ from `./bin/tsvra --print-defaults`.
- The frontend config surface exposes only a subset of backend capabilities.
- Tests cover only a narrow slice of behavior.
- There is no full CI path for native build, native tests, and web checks together.
- The research logs show the current model does not make redundancy layout matter, which weakens the main scientific story.

### High-confidence repo observations driving this roadmap

- `Config::Config()` defaults are now literature-calibrated relative to the older README: 4 layers, `failure_rate=1e-5`, `vertical_delay=5`, `horizontal_delay=500`, `simulation_cycles=100000`, and `shared` redundancy.
- `README.md`, `docs/web-visualization.md`, and `docs/hardcoded-parameters.md` still describe older defaults and older assumptions in several places.
- `web-nuxt/stores/simulation.ts` preview initialization still assumes legacy-style redundancy placement and old defaults.
- `web-nuxt/components/config/SimConfigForm.vue` and `web-nuxt/server/utils/simulationManager.ts` do not yet expose the full simulator parameter set.
- `tests/test_main.cpp` validates request generation and end-to-end upward routing, but not redundancy semantics, clustered failure properties, JSON protocol stability, or web integration.
- `experiment-results/ANALYSIS.md` reports a null result: all redundancy layouts are statistically indistinguishable in the current simulation regime.

## Strategic Goals

1. Make the simulator scientifically defensible.
2. Make defaults, docs, CLI, and UI agree with each other.
3. Make experiments reproducible enough for paper/report use.
4. Make the web app a reliable inspection tool rather than a partially separate implementation.
5. Add engineering safeguards so future changes do not reintroduce drift.

## Roadmap Principles

- Model validity before feature count.
- Single source of truth for defaults and protocol shape.
- Reproducibility before visual polish.
- Backend/UI parity instead of duplicated assumptions.
- Tests for invariants, not only happy-path demos.

## Phase 1: Baseline Alignment And Documentation Cleanup

### Goal

Remove the current mismatch between code, docs, CLI output, and frontend defaults.

### Why this comes first

Right now the repository tells different stories depending on whether someone reads the README, runs `--print-defaults`, or opens the web app. That makes every later experiment and feature harder to trust.

### Deliverables

- Refresh `README.md` to match the real simulator capabilities and defaults.
- Update `docs/problem-setting.md`, `docs/web-visualization.md`, and `docs/hardcoded-parameters.md` so they no longer describe stale defaults or stale architecture.
- Make the web frontend initialize from backend defaults wherever possible instead of maintaining a second set of assumptions.
- Document the JSON protocol as it exists now, including redundancy and failure-model metadata.
- Add one concise architecture document describing the current backend/web/data flow.

### Code Areas

- `README.md`
- `docs/problem-setting.md`
- `docs/web-visualization.md`
- `docs/hardcoded-parameters.md`
- `web-nuxt/stores/simulation.ts`
- `web-nuxt/components/config/SimConfigForm.vue`
- `web-nuxt/server/utils/simulationManager.ts`

### Exit Criteria

- `./bin/tsvra --print-defaults`, docs, and initial web form values agree.
- The frontend no longer visualizes legacy redundancy as the default preview when the backend default is `shared`.
- A new contributor can understand the current system without reading historical refinement logs.

## Phase 2: Scientific Model Validation And Research Reframing

### Goal

Decide whether the project should:

1. keep the current routing model and explicitly position the null result as the main finding, or
2. change the simulator model so redundancy strategies can produce measurable differences.

### Why this is the core phase

The experiment logs already show the current simulator does not demonstrate meaningful layout differences. If that remains true, more UI or automation will not strengthen the project. The project needs either a clearer negative-result thesis or a more discriminative model.

### Work Items

- Audit the redundancy bypass path in the router and simulator with targeted diagnostics.
- Measure when `find_bypass_spare()` is actually invoked and when it changes route feasibility rather than just route cost.
- Define benchmark scenarios that are specifically capable of stressing redundancy behavior.
- Evaluate whether the current horizontal routing freedom is masking the effect of vertical redundancy.
- Decide whether to add one or more of the following model constraints:
  - tighter topology or blocked regions
  - TSV contention/capacity limits
  - layer-specific traffic sources
  - destination constraints beyond "any top-layer position"
  - failure regimes that target critical vertical corridors
- If the null result is retained, formalize it as a supported outcome and adjust the project narrative accordingly.

### Expected Outputs

- A short design note explaining why redundancy currently has near-zero measured effect.
- A decision memo selecting one path:
  - `negative-result publication path`, or
  - `model-revision path`
- A small benchmark suite that can be rerun after simulator changes.

### Code Areas

- `src/router.cpp`
- `src/grid.cpp`
- `src/simulator.cpp`
- `src/request_generator.cpp`
- `src/statistics.cpp`
- `experiment-results/`
- `run_experiments.py`

### Exit Criteria

- We can explain the null result with evidence, not speculation.
- We have an explicit decision on whether layout differentiation is still a target requirement.
- Follow-up experiments can answer that requirement without ad hoc parameter hunting.

## Phase 3: Correctness, Invariants, And Test Coverage

### Goal

Turn the current simulator from "works in demos" into "safe to evolve".

### Current Gap

The existing native test target passes, but it only covers request generation and upward completion. The code now contains more advanced behavior than the tests protect.

### Work Items

- Add unit tests for `Config` parsing and validation.
- Add tests for `Grid` redundancy invariants:
  - `shared` placement
  - `corner4` placement
  - `none`
  - per-region spare coverage rules
- Add statistical tests for clustered failure sampling:
  - mean preservation
  - no invalid coordinates
  - runtime sampler excludes already failed TSVs
- Add router tests for:
  - bypass routing through a spare
  - path reconstruction with `redundant_via`
  - `lambda1`, `lambda2`, `max_path_length`, and `reliability_min`
- Add JSON protocol tests for `init`, `cycle`, and `done` messages.
- Add web-side smoke tests for start/pause/resume/stop and config serialization.

### Code Areas

- `tests/test_main.cpp`
- possibly split into multiple test files under `tests/`
- `web-nuxt/server/api/_ws.ts`
- `web-nuxt/server/utils/simulationManager.ts`
- `web-nuxt/types/simulation.ts`

### Exit Criteria

- Core simulator invariants are covered by automated tests.
- The JSON contract is protected against accidental breaking changes.
- Parameter additions require touching tests, which reduces drift.

## Phase 4: Frontend And Backend Parity

### Goal

Make the web app a faithful operator console for the simulator, not a second partial model.

### Current Gap

The backend supports more parameters and semantics than the UI exposes, and the preview grid still encodes old redundancy assumptions.

### Work Items

- Extend the web form to expose the backend options that matter for experiments:
  - redundancy layout
  - failure model
  - cluster strength/radius
  - `lambda1`, `lambda2`
  - path constraints
  - seed presets
- Replace the preview-grid logic so it mirrors the selected redundancy layout.
- Improve type coverage so the web config schema matches the native config schema more closely.
- Add scenario presets for common experiment modes.
- Allow exporting run metadata and summary artifacts from the UI.
- Review rendering performance in quad mode and large grids.

### Code Areas

- `web-nuxt/components/config/SimConfigForm.vue`
- `web-nuxt/stores/simulation.ts`
- `web-nuxt/types/simulation.ts`
- `web-nuxt/components/grid3d/`
- `web-nuxt/components/heatmap/`
- `web-nuxt/components/failure/`
- `web-nuxt/server/utils/simulationManager.ts`

### Exit Criteria

- Every important simulator feature can be configured and interpreted from the UI.
- The UI preview reflects the same redundancy semantics as the backend.
- The web app is usable for actual experiment setup, not only demos.

## Phase 5: Reproducible Experiment Pipeline

### Goal

Make experiment execution, aggregation, and reporting repeatable.

### Work Items

- Turn `run_experiments.py` into the canonical batch runner with documented inputs and outputs.
- Standardize experiment manifests:
  - parameters
  - seed sets
  - output folders
  - summary tables
- Store raw results and derived analysis in a predictable structure.
- Add scripts to regenerate key tables from CSV outputs.
- Capture provenance:
  - commit hash
  - defaults snapshot
  - command line
  - timestamp
- Define a minimal benchmark matrix for regression checking after model changes.

### Deliverables

- A documented experiment runner workflow.
- Rebuildable analysis outputs instead of one-off result files.
- A small regression suite for research claims.

### Code Areas

- `run_experiments.py`
- `experiment-results/`
- `refine-logs/`
- possibly a new `scripts/` directory

### Exit Criteria

- Another developer can reproduce the main tables from repository instructions.
- New model changes can be evaluated against the same benchmark matrix.
- Research claims are tied to explicit run manifests.

## Phase 6: CI, Packaging, And Release Readiness

### Goal

Add lightweight engineering discipline so the repository remains stable as it grows.

### Work Items

- Add CI for:
  - native configure/build
  - native tests
  - web install/build
  - basic type/lint checks if introduced
- Decide on formatting/linting strategy for C++ and TypeScript.
- Add a clean developer setup guide for both native and web workflows.
- Separate generated artifacts from source-controlled documents more clearly.
- Add release notes or milestone docs for major experiment/model updates.

### Exit Criteria

- A pull request can verify native and web health automatically.
- Generated outputs are clearly distinguished from source documents.
- New contributors can bootstrap the project without reading terminal history.

## Recommended Priority Order

1. Phase 1: Baseline alignment and docs cleanup
2. Phase 2: Scientific model validation and research reframing
3. Phase 3: Correctness and test coverage
4. Phase 4: Frontend/backend parity
5. Phase 5: Reproducible experiment pipeline
6. Phase 6: CI and release readiness

## Immediate Next Milestone

The next milestone should be a short, focused stabilization pass with these concrete outcomes:

- docs and defaults aligned
- web defaults pulled into parity with backend reality
- one diagnostic report explaining the current null result
- at least one new test module protecting redundancy semantics

That milestone is small enough to finish quickly and strong enough to de-risk every later phase.

## Explicit Non-Goals For Now

- Adding entirely new visualization modes before model parity is fixed.
- Expanding the web app into a general-purpose dashboard unrelated to simulator research.
- Large algorithmic rewrites before we decide whether the null result is a bug, a model property, or the actual finding.
- Paper-style polishing before experiment reproducibility is in place.

## Definition Of Success

The roadmap is successful if, at the end of these phases, TSVRA is:

- internally consistent
- experimentally reproducible
- clear about what scientific claim it does or does not support
- protected by enough tests to evolve safely
- usable from both CLI and web without contradicting itself
