# TSVRA test plan, experiment results, and report

**Status: complete — correctness gate passed, all 4,116 experiment runs audited, tables/plots/report delivered.** Updated 9 October 2026.

The organized standalone [experiment report](experiment-report.md) presents the completed findings, figures and complete mean-latency sweep. Its [supporting tables and figures](experiment-results/README.md) are copied into `docs/`; this file retains the approved protocol and execution history.

This Markdown document is the canonical record for the test plan, approval decisions, execution history, later experiment results, and final report. It translates the requirements in the supplied `Exp Plan.docx` into a reproducible workflow; those requirements are recorded in section 1. Maintain this file directly; Word documents are no longer the working format.

The design-only statements and proposed commands below describe the original review state. On 9 October 2026 the user instructed “Implement the plan” and confirmed that this computer is a server and batch CPU/GPU execution is allowed. This authorized the implementation, correctness gate, 156-run pilot and 3,960-run main sweep under the declared settings, subject to the pilot suitability review. All stages are complete; validated results and conclusions appear in sections 10–11.

The original design was based on repository revision `729023b0bc648e4f244a6c5ff136ab9ce1c15c96`. The added design choices below were subsequently approved and executed unchanged; they are distinct from the source document's requirements and the measured results. The actual execution revision and archived implementation are identified in section 10.1.

## 1. Questions and scope

The source document asks for initial recording sheets; failure rates of **5%, 10%, 15%, 20%, 25%, 30%, 35%, 40%, 45%, 50%, 60%, and 70%**; **2, 3, 4, and 5 layers**; denser measurements at lower failure rates; overall latency, success and power tables; and conclusions. Preserve those rates and layer counts.

The study will answer: (1) does the implementation produce internally consistent routing, failure, latency and power results; (2) how do delivery success, completed-flit latency and modeled power change with failure probability and layer count; and (3) how does concentrating the same expected defect fraction around point 1, point 2, or both change those outcomes?

Use the existing **shared** spare layout for the primary experiment. Tests will still exercise supported layout behavior. A full comparison with `corner4` and `none`, exact workload replay, physical calibration, new routing algorithms and the retained learned power predictor are separate extensions, rather than dependencies of this plan.

Successful experiments validate implementation behavior and characterize this simulator under declared assumptions. The defect distribution and architecture power estimate do not constitute measured chip yield or silicon power.

## 2. Define the experiment before running it

### 2.1 Failure probability and spatial scenarios

Interpret “total failure rate” as the **configured mean initial defect probability across all logical vertical channels, including spares**. Use `failure_mode = a`, `initial_failure_rate = f`, and `runtime_failure_rate = 0`. An observed run's failed-channel fraction is a separate stochastic measurement. Do not force exactly f percent of channels to fail, and do not pass 5–70% as a failure probability per cycle.

There are `N²(L−1)` logical vertical channels, where `N = 4 × grid_factor`. The top layer has routing nodes but no extra channels. A defect at `(x,y,z)` disables the vertical edge between z and z+1 in both directions; z denotes the lower interface.

| Scenario | Native configuration | Interpretation |
|---|---|---|
| U: uniform | `failure_model=uniform` | Spatial control with the same configured mean f. |
| H1: point 1 | `failure_model=hotspot`; center `(5,5,0)`; count 1 | Higher risk near point 1 on the bottom interface. |
| H2: point 2 | `failure_model=hotspot`; center `(10,10,0)`; count 1 | Higher risk near point 2 on the bottom interface. |
| H12: points 1+2 | `failure_model=hotspot`; both centers; count 2 | Two regions sharing the same global mean f. |

For all hotspot scenarios propose radius **3 grid cells** and multiplier **5**. These locations and parameters are added design choices. They are valid for all four layer counts and will be explicitly included in each configuration.

The implemented hotspot rule uses the **maximum** Gaussian influence of the centers and then normalizes/clips probabilities to preserve the global mean. H12 does not add two failure rates or overlay two independently sampled fault sets. Local probabilities can reach 1 at large f; record the fraction of saturated channels when explaining spatial results. Point 1 lies at an ordinary-channel position; point 2 coincides with a shared spare. Their comparison intentionally includes alignment with the spare layout and is not a pure comparison of geometric location.

Centers remain on interface z=0 to retain literal one-point/two-point semantics. Its fraction of all channels is 1/(L−1), and 1/(L−1) of generated sources start on layer 0. Downward routes can also encounter this interface. Changing L therefore changes normalization and exposure to the hotspot; report these as part of this experiment's interpretation. A study with the same risk map on every interface would instead replicate each x/y center for all z=0…L−2; that is an optional, separately labeled sensitivity experiment.

### 2.2 Fixed settings

| Setting | Proposed value | Reason |
|---|---|---|
| Grid | `grid_factor=4`: 16×16 per layer | Holds planar geometry fixed. |
| Redundancy | `shared` | Keeps the current primary layout fixed. |
| Generation horizon | 10,000 cycles | Fixed measurement window followed by full drain. |
| Traffic probability | 0.02 globally per cycle | About 200 generated flits per run in expectation; explicit proposed load. |
| Delays | vertical 5; horizontal 500; router 15 cycles | Uses the native timing profile consistently. |
| Routing weights | `lambda1=0`, `lambda2=0` | Establishes an unweighted routing baseline. |
| Routing limits | path length 0; horizontal distance 0; reliability minimum 0 | Uses the native disabled limits without an arbitrary cap. |
| Failure timing | initial only; runtime probability 0 | Isolates the draft's defect sweep. |
| Power | `code/config/power_model.ini`, unchanged | Holds operating point and coefficients fixed; retain file hash and exact exported coefficients. |
| Main seeds | consecutive nonzero seeds 101…130, with prefixes by rate band | Reproducible repeats; never use seed 0. |

The native defaults, example INI and browser have different traffic/delay profiles. Every experiment setting above must be explicit; changing playback speed does not change simulation time or power. The expected 200 flits is a workload estimate, not a precision guarantee. The pilot will check variability and computational cost before authorizing any enlargement or changed workload.

Sources come from layers 0…L−2 and destinations from top layer L−1, using the existing spatially weighted generator. Global injection stays fixed as L increases, so offered load per router decreases. Record actual endpoints; fixed source/destination CLI injection is not currently available. Matching seeds yields matched generated traffic across fault scenarios at the same layer count and layout, which the validation must verify. Do not assume identical endpoint traces across layer counts or different spare layouts.

## 3. Correctness gate before measurement

First run the existing verification entry points after approval. Inspect their results and add only missing checks needed for this experiment. Existing coverage includes routing/timing, normalization and runtime lifetimes, power, CLI/JSON parity, state handling and WebSocket controls; tests need not be duplicated to recreate those features.

| Area | Required evidence and meaningful additions |
|---|---|
| Geometry and configuration | For L=2…5, nodes are 256L and channels 256(L−1); top-layer nodes are not extra channels. Shared layout has 25 spares per interface. Reject invalid coordinates/probabilities; export the exact effective stage-specific settings and manual centers. |
| Defect probability maps | U/H1/H2/H12 are deterministic for a fixed configuration; every channel probability lies in [0,1]; the mean equals f within 1e−12. Check all planned rates/layers, clipping at high f, Gaussian maximum behavior, and lower-interface locality. Verify probability maps are frozen. |
| Routing fixtures | Use explicit endpoints with the public Grid/Router APIs: healthy direct vertical route, horizontal detour, recoverable failed channel, unavailable spare, disconnected interface and a constrained route. Assert adjacent steps, bounds and healthy vertical crossings. A no-wait planned path with dispatch delay 1 has physical duration `1 + 20×vertical_hops + 515×horizontal_hops`. Full simulator latency must also include actual resource waits. |
| Timing and failures | Cover reservation boundaries, a runtime failure before/during a crossing, reroute from the last reached node, and retention of completed horizontal distance. Runtime checks use their own suitable per-cycle probabilities and are separate from the initial-defect matrix. |
| Accounting and drain | Normal completion requires generated = completed + failed, unique terminal request IDs, and no requests in flight at end. Reconstruct counts and completed latency from raw request timestamps. Stop-at-horizon results are partial. Record horizon occupancy separately. Verify zero traffic, zero faults and 100% initial defects. |
| Event power | Router events = horizontal launches + vertical launches; spare launches are a subset of vertical launches. Search attempts/waits add no hop energy; an already launched hop retains its energy after failure. Independently recompute components from exported coefficients and counters, including idle energy through drain. |
| Reproducibility and interfaces | Add full replay checks for valid shared g=4 profiles at L=2…5: canonical config/summary/power records and request rows sorted by ID, including endpoints, times, status and distance; ignore output-prefix differences. Existing native replay coverage only compares completion count. Verify CSV/JSON metrics agree and fault changes preserve traffic at fixed L/layout. Reuse browser validation/state/control suites. |
| Batch and analysis | Verify matrix size/seed uniqueness, explicit fault flags, centers, output collision protection, malformed/partial outputs and failed-process handling. The current runner omits In Flight At End from its metric map; the acceptance check must read and require this value to be zero. Use small saved/artificial fixtures for formulas/missing values/intervals instead of executing the full sweep in a test suite. |

For integer counts and coordinates use exact equality. For cross-export energy/power identities use `max(1e−8 absolute in the reported unit, 1e−8 relative)` unless a documented format requires a different tolerance. Recompute request latency from integer timestamps. A probability-map tolerance does not demand that an individual realized failed fraction equal f.

Proposed commands, **not executed during design**, from the repository root:

```text
cmake -S code/backend -B code/build -DCMAKE_BUILD_TYPE=Release
cmake --build code/build --parallel
ctest --test-dir code/build --output-on-failure
npm --prefix code/frontend test
npm --prefix code/frontend run build
npm --prefix code/frontend run test:integration
```

The correctness gate requires all four CTest registrations (native, failure, power and Python integration), frontend helper tests, production build and WebSocket integration. Verify prerequisites rather than allowing a missing Python integration registration to count as a complete pass. Fix a demonstrated defect with a focused regression test before freezing a new source revision for experiments.

## 4. Staged execution and run budget

### 4.1 Pilot

Run f = 5%, 30%, 70% × four layers × four spatial scenarios × seeds 901, 902, 903: **144 runs**. Add uniform 0%-fault controls × four layers × those three seeds: **12 runs**. Total pilot: **156 runs**, with the same horizon and traffic settings as the proposed main experiment. Pilot seeds and outputs remain separate from the final analysis.

Measure wall time, peak resource use where available, output size, generated/completed counts, drain duration, missing latency and between-seed variability. Start sequentially; choose bounded concurrency up to four jobs only when resource measurements support it. Produce map diagnostics for the manual-center scenarios.

Use observed timings to estimate full-run cost: sum estimated cell/repetition costs, divided by measured useful concurrency, with drain/runtime variation shown. Set timeouts conservatively from the pilot and preserve every timed-out run as an operational failure. No wall-clock budget is claimed before this measurement.

If pilot results support the approved settings and feasible cost, proceed with the main matrix. If they require changing the horizon, load, center placement or repetition budget, document a revised design and seek approval before the expanded sweep. Do not tune settings to obtain a preferred trend or layout ranking.

### 4.2 Main experiment

| Rate band | Rates | Seeds per cell | Runs across 4 layers × 4 scenarios |
|---|---|---|---|
| Low | 5%, 10%, 15%, 20% | 30: seeds 101…130 | 1,920 |
| Middle | 25%, 30%, 35%, 40% | 20: seeds 101…120 | 1,280 |
| High | 45%, 50%, 60%, 70% | 10: seeds 101…110 | 640 |
| Healthy control | 0%, uniform only | 30: seeds 101…130 | 120 |

The primary matrix contains **192 cells and 3,840 runs**. Healthy controls add four cells and 120 runs: **3,960 main runs**. Pilot plus main: **4,116 experiment runs**, excluding small engineering fixtures. Higher repeat counts at lower failure rates implement the supplied document's sampling preference; they are a starting budget, not an assertion that every effect is detectable.

The existing JSON runner supports per-configuration matrices and seed/repetition overrides. Use separate named configurations per scenario and rate band; no simulator feature change is needed to launch this sweep. Create isolated output directories per stage and retry shard. A timed-out/crashed run is a failed job, rather than a request-delivery failure; retain the diagnostic and retry the same settings/seed if appropriate, preserving its original evidence. Do not silently drop it or replace it with a new seed.

## 5. Recording sheets and metric definitions

Correct the original sheet's last endpoint label from the apparent “Start X” typo to **End Z**. Keep fault point 1, point 2 and 1+2 as mutually exclusive scenario indicators; preserve explicit centers as metadata. Store separate request and run tables so whole-run power is not misread as a per-request measurement.

**Request sheet:** run ID, requested/actual seed, f, observed run failure fraction, fault scenario/centers, L, request ID, Start X/Y/Z, End X/Y/Z, generation time, completion time, latency in cycles, horizontal distance and terminal status. Join existing request CSVs to run metadata. A failed request has no completion time/latency; use N/A, not zero. Success is a terminal status at request level and a fraction at run level. Do not invent a per-request power allocation.

**Run sheet:** one row per seed/cell, including all fixed settings, generated/completed/failed counts, success rate, completed-only mean/median/p95 latency, observed failed channels and failed spares, router/channel/spare population, in-flight counts at horizon/end, horizontal/vertical/spare launch counters, elapsed cycles, elapsed simulated seconds, drain duration, modeled mean power and components in mW, total energy in pJ, energy/completed flit, wall seconds, operational status and provenance. Define p95 by nearest rank `ceil(0.95 × completion_count)` in sorted latency samples; the median averages the central two values for an even count.

**Summary sheet:** one row per f × scenario × L, with planned/valid/missing run counts, configured and observed defect fractions, sample mean and standard deviation, 95% interval, delivery success, completed-only latency, mean modeled power and energy/completed flit. Include fault scenario indicators and centers so the summary corresponds to the source document. Retain the runner's existing pooled columns with their weighting clearly identified.

| Quantity | Calculation and interpretation |
|---|---|
| Observed failed fraction | `Failed TSVs / [256(L−1)]`; failed spares are already a subset of Failed TSVs and must not be added again. |
| Delivery success | `100 × completed / generated`, measured after full drain. Undefined if no traffic was generated. |
| Latency | `complete_time − generate_time`, for completed flits only; includes dispatch, waiting and rerouting. Report success alongside it to expose survivor selection. |
| Mean power | `total_energy_pj / (elapsed_seconds × 10^9)` in mW, over generation plus drain. Report idle and dynamic components separately. |
| Energy/completed flit | `all modeled energy / completed`; includes failed traffic and idle energy. Undefined with zero completions. |
| Drain | `elapsed_cycles − generation_horizon` for a normally completed run; retain actual elapsed cycles and horizon occupancy. |

At 1 GHz the cycle-to-time mapping is 1 ns. It is a declared mapping, not a physical timing measurement. No-completion runs retain valid energy/power and success=0 when generated>0; latency and energy/completed remain blank/null. A stopped run retains partial accounting but does not qualify as a completed experiment run.

## 6. Analysis and interpretation

Treat each independent seed/run as the replication unit; requests in one run share faults and contention. Primary tables weight valid seed runs equally. Supplement them with pooled delivery `sum(completed)/sum(generated)`, completed-flit latency `sum(latency)/sum(completed)` and energy `sum(energy)/sum(completed)`; these are different estimands from means of per-run ratios and must be labeled accordingly.

Compute pointwise 95% bootstrap intervals by resampling whole seed runs within each cell, using 10,000 resamples and a recorded analysis RNG seed. For H1−U, H2−U and H12−U at fixed f and L, resample matched seed pairs after verifying traffic equality. Resample seed blocks jointly across matched scenarios when plotting contrasts. Do not pair request IDs across L or across different spare layouts. Preserve run-level numerator/denominator pairs when recomputing pooled ratios.

For completed-only latency and energy/completed, report how many seed runs have defined values and how many have no completions. If all runs lack completions, show N/A; avoid interpreting faster survivor latency as better overall performance. Sparse or constant seed results can yield uninformative/degenerate bootstrap intervals; identify this limitation rather than claiming certain reliability or no effect. This is a descriptive study: pointwise intervals across many cells do not establish simultaneous significance or equivalence.

With healthy horizontal edges and no routing constraints, healthy vertical alternatives can maintain connectivity even at high defect fractions. Success may stay at 100%; this is an expected model outcome, rather than proof of physical reliability. Do not introduce a routing cap to force failures. When delivery saturates, interpret latency, horizontal hops, spare launches and energy as the main sensitivity measurements.

Deliver: delivery-vs-failure curves by L with separate fault scenarios and intervals; completed-only latency curves accompanied by success/counts; horizontal-hop/spare-use curves; total/idle/dynamic power and energy/completed-flit curves; observed-vs-configured failed fractions; and spatial probability maps showing centers/clipping. Include hotspot-minus-uniform differences and an unconditional delivery-by-latency curve, `completed with latency ≤ t / generated`, so non-delivery stays visible. In fully drained runs, failed requests are non-delivery outcomes, rather than administratively censored completions. Use standalone, exportable plots.

Keep layer-normalized power alongside raw total power. The fixed idle term alone is approximately 8.43, 12.64, 16.85 and 21.06 W for L=2,3,4,5; it can dominate total-power differences. Preserve sufficient numeric precision and show router-event/link contributions separately. These values follow the configured router population and coefficients, not measured chip power.

Draw conclusions conditional on this workload, grid, redundancy layout, hotspot geometry, initial-only fault model and power coefficients. Unexpected trends trigger checks of raw requests, clipping, survivor selection, contention and layer exposure; no monotonic trend is a pass/fail rule. Historical aggregate tables lack sufficient provenance to serve as a current-code oracle.

## 7. Authorized implementation tasks

Use **GPT-6.1-sol with ultra reasoning** for three workstreams, supervised by the primary agent. Assign non-overlapping files and agree the run/summary schema before parallel editing.

| Owner | Scope and proposed files | Reviewable result |
|---|---|---|
| Agent A: native correctness | Existing `code/backend/tests/` suites; narrowly scoped native fixes only for demonstrated defects | Coverage inventory, focused fixtures, passing native and CLI/JSON checks. |
| Agent B: experiment configuration | New pilot/main JSON files under `code/config/`; manifest/validation support under `code/scripts/` only where needed | Dry-run count/seed checks, frozen explicit settings, collision-safe commands and provenance. |
| Agent C: analysis and presentation | New analysis/plotting support under `code/scripts/`; frontend tests only for a demonstrated coverage gap | Joined request/run/summary sheets, missing-value checks, reproducible intervals and plots. |
| Primary agent | Integration review, shared schemas, existing frontend verification, pilot execution, budget review and main-run coordination | Approved gates, preserved evidence, final report and conclusions tied to raw data. |

Sequence: approve this design → implement focused verification/configuration/analysis work → run correctness gate → freeze source/settings → pilot and cost review → main sweep if approved settings remain suitable → audited tables/plots/conclusions. The user's implementation request authorizes this sequence. Any material change to the agreed research question or workload returns for design review.

Completion evidence requires: all required engineering checks pass; every planned cell has accounted-for runs/seeds; normal-run terminal and energy identities pass; all operational failures are visible/resolved or explicitly qualify the results; raw CSV/config/log bundles are retained; the summary/plots regenerate from them; and no undefined value is converted to zero.

## 8. Evidence bundle and approval

Each stage should produce an isolated bundle containing the exact batch JSON, resolved command lines, source revision/diff status, executable and power-file hashes, compiler/CMake/Python/Node versions, machine description, stage timestamps, jobs/timeouts, analysis settings and all per-run logs/configuration/request/summary/power files. Retain planned and actual seeds plus actual hotspot centers. Reproducibility is promised for a recorded toolchain; different C++ standard-library distribution implementations may produce different traces.

Repository basis: [README](../README.md), [problem setting](problem-setting.md), [failure model](failure-model.md), [power model](power-model.md), [architecture](architecture.md), [roadmap](roadmap.md), [grid implementation](../code/backend/src/grid.cpp), [statistics](../code/backend/src/statistics.cpp), [simulator interface](../code/backend/include/simulator.hpp), [native tests](../code/backend/tests/), [batch runner](../code/scripts/run_experiments.py), [example batch configuration](../code/config/experiments.example.json), and [frontend tests](../code/frontend/tests/). Review recommendations were obtained from three GPT-6.1-sol ultra subagents without implementation or simulation.

**Approved scope:** the initial-only interpretation; shared layout; manual centers/radius/multiplier and bottom-interface scope; fixed native timing and traffic/horizon profile; correctness gate; 156-run pilot and 3,960-run main budget; uncertainty/recording rules; and workstream split. Implementation begins with correctness/configuration work and the pilot. Further changes to scientific settings or run budget require an updated reviewable proposal.

## 9. Approval, changes, and execution record

The requested Markdown format, implementation and planned execution are approved. Material changes to the declared scientific settings or run budget still require review; routine implementation and measured concurrency/timeout choices are within the authorized scope.

| Date | Decision or change | Effect on execution |
|---|---|---|
| 2026-10-09 | Initial design prepared and independently reviewed by three GPT-6.1-sol ultra agents. | Design only; no implementation or experiments. |
| 2026-10-09 | User requested Markdown for the plan, later experiment results, and report. | This file becomes the maintained record; implementation approval is still pending. |
| 2026-10-09 | User instructed “check exp plan in docs/experiment-plan.md . Implement the plan.” and confirmed “this computer is server, batch cpu/gpu run is allowed.” | Implementation, correctness checks, the 156-run pilot, and the unchanged 3,960-run main sweep are authorized. Three GPT-6.1-sol ultra workstreams started; the primary agent supervises integration and execution. |
| 2026-10-09 | Correctness/fixture gates passed; source/settings frozen; pilot suitability confirmed. | Main proceeded with unchanged scientific settings, four CPU jobs and 30-second per-process timeout. |
| 2026-10-09 | All 156 pilot and 3,960 main runs accepted; all analysis audits passed; final tables, figures and report generated. | Complete, with no scientific changes, operational failures or retries; forecast miss and rate-band sampling anomaly documented in sections 10–11. |

For subsequent design changes, append the date, changed setting, reason, expected effect on comparisons/run budget, and approval reference where a material change requires approval. Keep the old executed settings identifiable so results from different protocols are not combined accidentally.

| Stage | Planned scope | Current status | Evidence |
|---|---|---|---|
| Design review | Code review, experiment settings, metric definitions and task split | Approved by implementation request | Sections 1–8 and approval record |
| Implementation | Focused verification, configurations and analysis tools | Complete; no simulator algorithm or power coefficient changes required | Source snapshot and implementation files |
| Correctness gate | Required native/frontend checks and focused additions | All four CTest suites, 32 Python fixtures, frontend helpers/build/WebSocket integration passed | `../code/output/experiment-study-20261009/engineering/` |
| Pilot | 156 runs; seeds 901–903 | Complete: 156 accepted; no operational failures/missing runs | `../code/output/experiment-study-20261009/pilot/` |
| Pilot review | Runtime/resource forecast, variability and suitability of approved settings | Passed; unchanged settings; jobs=4, timeout=30 seconds | `../code/output/experiment-study-20261009/pilot/pilot-review.json` |
| Main sweep | 3,960 runs; rate-dependent prefixes of seeds 101–130 | Complete: 3,960 accepted; no failed/missing/retried runs | `../code/output/experiment-study-20261009/main/manifest.json` |
| Analysis and report | Validated tables, intervals, plots and evidence-linked findings | Complete: 196 main cells, 152 main PNG/PDF files, canonical report | Sections 10–11 and retained analysis artifacts |

For each executed stage, append a record with:

- Approval scope, start/end timestamps, operator/agent and execution status.
- Frozen source revision and relevant changes; compiler/CMake/Python/Node versions and build settings.
- Executable and power-file hashes, effective operating parameters, machine/resources, jobs and timeout.
- Exact batch configuration and invocation, seeds, output-directory links, logs and raw artifacts.
- Planned, attempted, accepted, failed, missing and retried run counts; completeness by cell/seed.
- Check results, anomalies, retry disposition and any change needed before the next stage.

Do not count code review or document-format checks as passed simulator tests. Preserve failures and retry history. A successful process exit alone does not establish valid drained/accounted results.

## 10. Experiment results — completed 9 October 2026

**All 4,116 planned experiment runs completed and passed accounting validation: 156 pilot runs and 3,960 main runs.** Main results comprise 196 complete cells and 811,488 generated/completed flits; the pilot comprises 52 cells and 33,072 generated/completed flits. No experiment run crashed, timed out, was missing, required a retry, retained final in-flight traffic, or had undefined completed latency/energy. Engineering fixtures and pilot seeds are excluded from main findings.

### 10.1 Correctness and frozen implementation

| Check group | Outcome | Evidence |
|---|---|---|
| CTest registrations | All four required registrations present | [registration log](../code/output/experiment-study-20261009/engineering/ctest-registrations.log) |
| Native topology/routing/failure/power plus CLI/JSON/replay | 4/4 suites passed; 6.53 seconds in the primary final gate | [CTest log](../code/output/experiment-study-20261009/engineering/ctest-final.log) |
| Batch, analysis and pilot review fixtures | 32/32 passed: 13 batch, 16 analysis, 3 pilot-review checks | [Python test log](../code/output/experiment-study-20261009/engineering/python-final.log) |
| Frontend helpers | 10/10 passed | [helper log](../code/output/experiment-study-20261009/engineering/frontend-test.log) |
| Frontend production build and WebSocket integration | Both passed, including state/control/runtime discovery checks | [build log](../code/output/experiment-study-20261009/engineering/frontend-build.log), [WebSocket log](../code/output/experiment-study-20261009/engineering/frontend-integration.log) |
| Dry-run budget/seed/settings checks | Exactly 156 pilot and 3,960 main commands | [pilot commands](../code/output/experiment-study-20261009/engineering/pilot-dry-run.log), [main commands](../code/output/experiment-study-20261009/engineering/main-dry-run.log) |
| Raw-run accounting and independent power identities | Every planned run accepted by the runner and independent analyzer; independent review found maximum relative energy residual 5.76×10⁻¹⁶ | [pilot audit](../code/output/experiment-study-20261009/pilot/analysis/audit.json), [main audit](../code/output/experiment-study-20261009/main/analysis/audit.json) |

No simulator algorithm or power coefficient change was necessary. Focused additions exercise all 192 planned probability profiles, geometry/spares, routing/timing/waits, runtime failures and traveled-distance retention, unique terminal records, complete replay and traffic independence. Malformed-input regressions tighten the new runner/analyzer and pilot review; they are separate from measured simulator outcomes.

Execution used revision `8d14413ba4b635f8d359094e3f0c9cc35ff01d11` plus the archived implementation diff/untracked additions. Native/runtime source stayed frozen between pilot and main. Both manifests record active-source hash `2d67efa80b26beacbf33bc9cb4aa9e9d9e0151f88df13028c1371810571bf2cd`, executable SHA-256 `8322f1eece3aa3668cadb9f47c9295ca6ff4769bf96d583faffa9c774847a96e`, and unchanged power-file SHA-256 `c70498efd6dee6c584a45dfbf598d0079a51d793965962eb83bd1439ff230e20`. The exact [source/binary archive](../code/output/experiment-study-20261009/engineering/frozen-source.zip), [file hashes](../code/output/experiment-study-20261009/engineering/source-freeze.json), each stage's `source.diff`, and full manifests retain the reviewable revision rather than implying an uncommitted change is a new Git commit.

Toolchain: Release C++20, GCC 14.2.0, CMake 3.31.6, Python 3.13.5, Node 24.21.0, npm 12.2.0, NumPy 2.2.6 and Matplotlib 3.10.7. Machine: AMD EPYC 7532, 32 exposed CPU cores, approximately 31 GiB RAM. Measurements used CPU processes. GNU `time` was unavailable; study job resources use per-child POSIX `wait4`. Peak RSS includes inherited Python memory before `exec` and is a conservative job measure, not native-only simulator RAM. See [prerequisites](../code/output/experiment-study-20261009/engineering/prerequisites.json) and stage manifests for full details.

### 10.2 Pilot, resource review and unchanged production decision

| Item | Measured result |
|---|---|
| Scope/completeness | 156/156 accepted, 52/52 cells, seeds 901–903; 0 failed/missing/retried |
| UTC start/end | 2026-10-09T07:20:52.226305+00:00 → 2026-10-09T07:20:58.062551+00:00 |
| Sequential batch wall time | 5.836 seconds |
| Per-run wall time | Mean 0.03520 s; minimum 0.02873 s; maximum 0.09951 s |
| Per-job peak RSS | Maximum 32,516 KiB; conservative pre-exec-inclusive measurement |
| Generated/completed flits | 199–223 per run; 33,072 total; all completed |
| Drain duration | Mean 11,819.55 cycles; minimum 10,934; maximum 17,340 |
| Undefined latency/energy | 0 runs |
| Raw bundle size at runner completion | 16,204,951 bytes; later analysis/review/archive additions are separate |
| Useful concurrency measurement | 16 excluded engineering fixtures at jobs 1 and 4; 3.6855× measured speedup |
| Main cost forecast | 37.423 s; observed-profile range 34.056–44.855 s, excluding analysis and inadequately capturing scale-dependent batch overhead |
| Production settings | Unchanged scientific settings and seed budget; jobs 4; timeout 30 seconds per simulator process |

[Full pilot review](../code/output/experiment-study-20261009/pilot/pilot-review.json) retains each cell's timing, variability, resource use, undefined counts, interpolation and cost estimates. [Concurrency measurements](../code/output/experiment-study-20261009/engineering/concurrency-calibration.json) retain exact commands and seeds 970–985, excluded from experiment counts. Every pilot probability-map mean differs from configured f by at most 3.33×10⁻¹⁶; clipping/interface diagnostics and 80 PNG/PDF files are retained in [pilot analysis](../code/output/experiment-study-20261009/pilot/analysis/README.txt).

The settings were feasible and suitable for the declared descriptive study, so the original authorization covered proceeding. No horizon/load/center/repetition change was made. Three pilot seeds provide an operational check, not a statistical power guarantee.

Main UTC execution was 2026-10-09T07:22:37.759600+00:00 → 2026-10-09T07:28:56.470957+00:00, taking **378.711 seconds**. This was **10.12×** the pilot forecast. Mean native child CPU stayed approximately 0.0264 seconds/run versus 0.0278 in the pilot; increasing batch/report/directory-scan overhead is a plausible explanation, not a profiled attribution. Mean accepted-job wall time was 0.21086 s, maximum 1.73308 s; no timeout occurred. The main runner recorded 402,884,154 bytes at completion, about 2.1% below the operational storage forecast; later analysis increases the bundle size. The main analyzer took 190.273 s and peak RSS 2,419,112 KiB; see [analysis resources](../code/output/experiment-study-20261009/engineering/main-analysis-resource.json).

### 10.3 Main recording sheets, completeness and representative results

| Artifact | Location | Validation |
|---|---|---|
| Frozen batch and provenance | [batch JSON](../code/output/experiment-study-20261009/main/batch.json), [manifest](../code/output/experiment-study-20261009/main/manifest.json), [source archive](../code/output/experiment-study-20261009/main/frozen-source.zip) | 3,960 unique planned and actual nonzero seed runs; hashes and commands retained |
| Raw simulator exports/logs | [runner table](../code/output/experiment-study-20261009/main/runs.csv), prefixes in its `output_prefix` column and manifest | Four raw exports, process log and result record for every planned run |
| Joined request sheet | [requests.csv](../code/output/experiment-study-20261009/main/analysis/requests.csv) | 811,488 unique within-run terminal IDs; End Z, endpoints and integer timestamps; no per-request power allocation |
| Audited run sheet | [runs.csv](../code/output/experiment-study-20261009/main/analysis/runs.csv), [runs.json](../code/output/experiment-study-20261009/main/analysis/runs.json) | Exact effective settings, coefficients, counts, horizon/end occupancy, resources, energy and latency reconstruction |
| Compact/full cell summaries | [summary_readable.csv](../code/output/experiment-study-20261009/main/analysis/summary_readable.csv), [full CSV](../code/output/experiment-study-20261009/main/analysis/summary.csv), [full JSON](../code/output/experiment-study-20261009/main/analysis/summary.json) | All 196 cells complete; SD/pointwise intervals/defined counts plus explicitly pooled ratios |
| Paired comparisons | [contrasts](../code/output/experiment-study-20261009/main/analysis/contrasts.csv), [traffic audit](../code/output/experiment-study-20261009/main/analysis/matched_traffic.csv) | 144 contrasts; 2,880 verified hotspot/uniform seed pairs |
| Traffic across rates | [fixed-seed audit](../code/output/experiment-study-20261009/main/analysis/fixed_seed_traffic.csv) | 3,840 comparisons pass at fixed L/layout/seed; no pairing across L |
| Probability maps | [diagnostics](../code/output/experiment-study-20261009/main/analysis/probability_map_diagnostics.csv), [array archive](../code/output/experiment-study-20261009/main/analysis/probability_maps.npz) | Max mean error 3.33×10⁻¹⁶; centers/interface means/saturation recorded |
| Unconditional delivery | [delivery-by-latency](../code/output/experiment-study-20261009/main/analysis/delivery_by_latency.csv) | Completed with latency ≤ t / generated; equal-seed and pooled estimates/intervals; failures retain their denominator |
| Analysis provenance and figures | [settings/hashes](../code/output/experiment-study-20261009/main/analysis/analysis.json), [reproduction instructions](../code/output/experiment-study-20261009/main/analysis/README.txt) | PCG64 seed 20261009, 10,000 whole-seed resamples; 76 figures, PNG and PDF each |

Primary numbers below are equally weighted means of independent seed runs. Rates 5%, 30% and 70% use 30, 20 and 10 accepted runs per cell respectively. The full linked tables include all twelve requested rates, medians, nearest-rank p95, SDs and uncertainty/defined counts. All reported cells have success **100%** and defined latency/energy in every seed; the success bootstrap intervals are consequently degenerate [100%,100%] and do not establish certain physical reliability.

Healthy uniform controls, f=0:

| L | Accepted/planned | Mean latency [95% interval], cycles | Mean total power, mW | Mean energy/completed flit, pJ |
|---|---|---|---|---|
| 2 | 30/30 | 6,461.6 [6,364.6, 6,564.3] | 8,426.2808 | 908,661 |
| 3 | 30/30 | 6,029.0 [5,940.9, 6,116.2] | 12,639.0041 | 1,340,059 |
| 4 | 30/30 | 5,882.9 [5,799.1, 5,969.7] | 16,851.7185 | 1,772,582 |
| 5 | 30/30 | 5,813.7 [5,728.6, 5,901.1] | 21,064.4263 | 2,211,718 |

Representative fault cells. The 70% columns describe configured f=0.70; observed fractions are separate stochastic measurements. Power covers generation plus drain, and dynamic power is router events plus horizontal/vertical links.

| L | Scenario | Latency at 5%, cycles | Latency at 30%, cycles | Latency at 70% [95% interval], cycles | Observed failure at 70% | Total power at 70%, mW | Dynamic power at 70%, mW | Energy/completed at 70%, pJ |
|---|---|---|---|---|---|---|---|---|
| 2 | U | 6,468.9 | 6,496.8 | 6,402.1 [6,240.2, 6,560.2] | 68.87% | 8,426.2615 | 0.91973 | 914,846 |
| 2 | H1 | 6,468.3 | 6,474.2 | 6,813.4 [6,661.1, 6,946.1] | 68.75% | 8,426.3113 | 0.96951 | 905,624 |
| 2 | H2 | 6,466.9 | 6,498.9 | 6,707.3 [6,595.1, 6,822.4] | 69.45% | 8,426.2987 | 0.95686 | 915,675 |
| 2 | H12 | 6,465.0 | 6,493.4 | 6,647.5 [6,501.1, 6,791.6] | 69.45% | 8,426.2953 | 0.95352 | 907,509 |
| 3 | U | 6,029.2 | 6,026.2 | 6,065.5 [5,945.4, 6,189.8] | 68.87% | 12,639.0011 | 0.98839 | 1,333,298 |
| 3 | H1 | 6,032.6 | 6,059.1 | 6,493.7 [6,378.3, 6,617.0] | 69.98% | 12,639.0482 | 1.03552 | 1,330,966 |
| 3 | H2 | 6,031.1 | 6,066.3 | 6,489.5 [6,383.9, 6,592.7] | 69.67% | 12,639.0478 | 1.03506 | 1,331,665 |
| 3 | H12 | 6,031.5 | 6,056.0 | 6,914.5 [6,776.0, 7,055.9] | 69.57% | 12,639.0527 | 1.03998 | 1,368,886 |
| 4 | U | 5,887.5 | 5,910.7 | 6,027.1 [5,907.6, 6,152.8] | 68.89% | 16,851.7151 | 1.03155 | 1,772,472 |
| 4 | H1 | 5,887.3 | 5,913.1 | 6,291.8 [6,187.7, 6,399.4] | 69.44% | 16,851.7603 | 1.07674 | 1,760,691 |
| 4 | H2 | 5,887.3 | 5,922.5 | 6,351.2 [6,205.5, 6,485.3] | 69.65% | 16,851.7498 | 1.06621 | 1,780,598 |
| 4 | H12 | 5,887.6 | 5,921.4 | 6,541.2 [6,394.9, 6,686.9] | 69.78% | 16,851.7689 | 1.08525 | 1,794,271 |
| 5 | U | 5,819.5 | 5,842.3 | 6,060.2 [5,935.3, 6,199.8] | 69.36% | 21,064.4354 | 1.08092 | 2,203,457 |
| 5 | H1 | 5,819.9 | 5,861.8 | 6,258.7 [6,140.1, 6,385.5] | 69.38% | 21,064.4659 | 1.11138 | 2,210,407 |
| 5 | H2 | 5,820.5 | 5,857.3 | 6,307.8 [6,167.8, 6,448.9] | 69.61% | 21,064.4673 | 1.11282 | 2,215,800 |
| 5 | H12 | 5,820.6 | 5,848.3 | 6,462.8 [6,342.6, 6,589.5] | 69.55% | 21,064.4919 | 1.13741 | 2,220,602 |

Selected matched H12−U contrasts at f=70%. The same seed blocks are resampled jointly across U/H1/H2/H12; differences are means of paired run quantities, not differences between independently sampled requests.

| L | Matched seeds | Latency difference [95% interval], cycles | Dynamic-power difference, mW | Energy/completed difference [95% interval], pJ |
|---|---|---|---|---|
| 2 | 10 | 245.36 [175.78, 316.91] | 0.033788 | -7,336.9 [-28,253.9, 12,866.4] |
| 3 | 10 | 848.99 [756.30, 942.17] | 0.051596 | 35,587.4 [3,787.8, 75,569.5] |
| 4 | 10 | 514.09 [406.82, 639.82] | 0.053706 | 21,798.8 [-17,383.0, 65,376.7] |
| 5 | 10 | 402.53 [274.13, 516.97] | 0.056491 | 17,145.1 [-20,477.5, 62,311.4] |

At f=70%, all twelve hotspot-minus-uniform latency intervals are positive in this finite study; all twelve 5% latency intervals include zero. These are descriptive pointwise intervals across many comparisons, not simultaneous significance or equivalence. Most displayed energy/completed intervals span zero, so they do not establish an energy ranking.

### 10.4 Figures and anomaly disposition

Every figure here uses shared redundancy, 16×16 planar geometry, initial-only faults, a 10,000-cycle generation horizon, global traffic 0.02, V/H/router delays 5/500/15, and full drain. Main sample counts are 30/20/10 seeds in low/middle/high bands and 30 healthy uniform seeds; no cells are missing. Curve ribbons are pointwise 95% whole-seed percentile bootstrap intervals (10,000 resamples, PCG64 seed 20261009); units are on each axis. Matched-contrast ribbons use joint seed blocks. Maps are deterministic probability reconstructions of exported settings and do not have sampling intervals. PNG and PDF versions share the same basename.

| Figure | Purpose and interpretation |
|---|---|
| [Delivery vs failure](../code/output/experiment-study-20261009/main/analysis/figures/delivery_vs_failure.pdf) | All measured delivery is 100%; degenerate intervals describe sample saturation only. |
| [Completed-only mean latency](../code/output/experiment-study-20261009/main/analysis/figures/latency_vs_failure.pdf), [p95](../code/output/experiment-study-20261009/main/analysis/figures/p95_latency_vs_failure.pdf) | Read alongside [completed counts](../code/output/experiment-study-20261009/main/analysis/figures/completed_counts.pdf) and [defined seed counts](../code/output/experiment-study-20261009/main/analysis/figures/seed_value_counts.pdf). |
| [Horizontal activity](../code/output/experiment-study-20261009/main/analysis/figures/horizontal_activity.pdf), [spare activity](../code/output/experiment-study-20261009/main/analysis/figures/spare_activity.pdf) | Actual launches/generated flit; spare launches are a subset of vertical launches. |
| [Total power](../code/output/experiment-study-20261009/main/analysis/figures/power_total.pdf), [idle](../code/output/experiment-study-20261009/main/analysis/figures/power_idle.pdf), [dynamic](../code/output/experiment-study-20261009/main/analysis/figures/power_dynamic.pdf), [event/link components](../code/output/experiment-study-20261009/main/analysis/figures/power_event_components.pdf) | Modeled mW over generation plus drain; component precision is needed beside the large idle term. |
| [Power per layer](../code/output/experiment-study-20261009/main/analysis/figures/power_per_layer.pdf), [energy/completion](../code/output/experiment-study-20261009/main/analysis/figures/energy_per_completion.pdf), [drain](../code/output/experiment-study-20261009/main/analysis/figures/drain_duration.pdf) | Separates router-population scaling from time-integrated energy. |
| [Observed vs configured defects](../code/output/experiment-study-20261009/main/analysis/figures/observed_vs_configured.pdf) | Failed spares are already included in total failed channels. |
| [Paired latency contrasts](../code/output/experiment-study-20261009/main/analysis/figures/contrast_average_latency_cycles.pdf), [dynamic power](../code/output/experiment-study-20261009/main/analysis/figures/contrast_dynamic_mw.pdf), [energy](../code/output/experiment-study-20261009/main/analysis/figures/contrast_energy_per_completed_flit_pj.pdf) | Exact traffic is verified before forming H1/H2/H12−U differences. |
| [Unconditional delivery by latency, L=2](../code/output/experiment-study-20261009/main/analysis/figures/delivery_by_latency_L2.pdf) | L=3/4/5 versions accompany it; denominator is all generated traffic. All curves reach 1 by the global maximum latency 18,588 cycles. |
| [Probability maps, L=5/f=70%](../code/output/experiment-study-20261009/main/analysis/figures/probability_maps_L5_f70.pdf) | Cyan centers and white clipping markers; all layers/rates have corresponding maps and CSV diagnostics. |

The common apparent latency drop at 45% was investigated using the raw, matched traffic and [rate-band diagnostic](../code/output/experiment-study-20261009/main/analysis/rate_band_diagnostic.json) ([table](../code/output/experiment-study-20261009/main/analysis/rate_band_diagnostic.csv)). At 40→45% the primary means change from 20 to 10 seeds; matching both rates to seeds 101–110 reverses 14 of the 16 displayed decreases to increases. For L=2, healthy latency is 6,461.57 cycles across 30 seeds but 6,329.28 across the matched ten; U70 is 6,402.10. The apparent unpaired decrease of 59.46 cycles therefore becomes a matched increase of 72.82 [−12.86,148.51] cycles. Healthy horizontal distance also changes from 10.68518 to 10.46238 hops/flit when restricted to those ten seeds, whereas U70 is 10.53742. This is a sampling-composition explanation, not evidence that faults improve routing. The diagnostic retains 200 passing traffic checks, source IDs/hashes/formulas and its regeneration script; it does not alter primary tables or repetition budgets.

Energy and latency do not have identical rankings: L=2/f70 H12 has longer mean latency than U yet mean elapsed cycles 21,941.5 versus 22,129.2, reducing idle energy through drain and yielding the negative mean energy contrast above. Its energy interval includes zero. Mean latency is not the final completion time; dynamic mean power divides hop energy by elapsed time. Main results and pilot results are kept separate, including cases where longer pilot drain lowered mean dynamic power despite higher hop energy.

## 11. Final experiment report

### 11.1 Questions and actual methods

The study validates implementation consistency and characterizes delivery, completed-flit latency, route activity and architecture power under uniform and manual point-1/point-2/two-point initial defects. All approved settings in sections 1–6 were executed unchanged, including shared layout, bottom-interface centers, radius 3 and multiplier 5, configured mean f rather than exact realized defect fractions, and unconstrained unweighted routing. Pilot and main seeds remain separate. Whole seed runs are the independent replication units; primary means weight them equally, while separately labeled pooled ratios retain numerator/denominator pairs. Delivery and accounting use full drain, integer latency comes from raw timestamps, and power is independently reconstructed from exported coefficients and launch counts.

Native source/configuration, raw evidence, effective settings, toolchain and analysis versions are frozen and linked above. This is implementation validation and a modeled architectural study, not a chip-yield or silicon-power measurement.

### 11.2 Findings

**Delivery saturated:** all 811,488 main flits completed in all 3,960 runs, with zero final occupancy. Healthy horizontal meshes and disabled route limits provide alternate paths when each interface retains a healthy crossing. This is a model explanation consistent with the measured success, not a measured physical reliability claim. Because no main flit failed, the completed-only samples exclude no main requests here; success/counts and undefined-value rules remain explicit for future workloads.

**Spatial concentration changes overhead:** at f70 every hotspot/U paired latency interval is positive. H12−U penalties are approximately 245, 849, 514 and 403 cycles for L=2,3,4,5. H12 has the smallest hotspot latency penalty at two layers and the largest at three through five; two centers do not impose a universal ordering. At f5 the twelve corresponding intervals include zero; this does not establish equivalence. At f70 U→H12 horizontal launches/generated increase from 10.53742→10.88239, 10.60685→11.56150, 10.66691→11.50099 and 10.77020→11.57344 across L=2…5. Spare activity is not monotonic: L5 falls from 0.04312 to 0.03266 launches/generated while detours increase. Full curves and paired intervals are in section 10.

**Router idle power dominates:** fixed idle power is 8,425.3417984, 12,638.0126976, 16,850.6835968 and 21,063.354496 mW for L=2…5. Cell mean dynamic power is only 0.917680–1.137408 mW; idle supplies at least 99.988494% of total mean power. Layer-normalized total power spans about 4,212.882760–4,213.155654 mW/layer. At f=70%, all twelve hotspot dynamic-power intervals are positive, but their mean penalties of 0.030467–0.056491 mW are tiny next to idle. Energy/completed includes all idle and traffic energy through drain; its intervals do not justify a universal hotspot energy ranking.

### 11.3 Interpretation and limitations

The normalized H1/H2 probability maps are reflections with identical clipping, but point 1 is ordinary and point 2 aligns with a shared spare; their performance contrast mixes location, workload and spare alignment. H12 uses the maximum Gaussian influence rather than additive independent fault fields.

At f70 the H12 bottom-interface means are 0.7000, 0.8849609, 0.9208741 and 0.9340065 for L=2…5. Corresponding upper-interface background means are 0.5150391 for L3, 0.5895629 for L4 and 0.6219978 for L5, preserving global f=0.70. Total saturated fractions are 0.2890625, 0.30859375, 0.22135417 and 0.171875. Increasing L changes both normalization and the bottom-interface/source exposure fraction 1/(L−1). The unchanged global injection also reduces offered load per router. Layer comparisons are not isolated geometric or physical chip-power comparisons, and endpoint traces are not paired across layers.

Rate bands contain different seed-prefix sizes. The documented 45% anomaly shows why unpaired rate means can be nonmonotonic even when most matched comparisons increase. Fault realizations and waits also vary by seed. Pointwise bootstrap intervals are descriptive; shared-fault requests do not turn 811,488 flits into that many independent experimental replications. Constant delivery gives uninformative degenerate intervals, and finite seed counts do not prove zero risk, significance across all cells, or equivalence.

The channel abstraction, healthy horizontal network, unlimited routing, initial-only model and reference power coefficients limit physical interpretation. Idle energy depends strongly on drain completion; a lower mean power alone does not demonstrate greater energy efficiency. The operational cost forecast substantially missed batch overhead, while native CPU costs stayed stable; future batch estimates should measure runner cost at representative scale. There are no unresolved operational failures or accounting anomalies in this study.

### 11.4 Conclusions and follow-up

The declared implementation, export, routing/failure/timing and energy-accounting checks pass, and the entire approved experiment budget is accounted for. Under this workload, delivery remains saturated across 5–70% configured defects; high-rate spatial concentration is visible primarily in latency and horizontal/spare activity. Total modeled power is determined mainly by router-population idle power, with much smaller fault-sensitive event/link terms and drain-sensitive energy/completion.

Useful future studies, separately authorized, would compare spare layouts, replicate hotspot centers across every interface to control layer exposure, use identical seed counts or matched-seed sensitivity curves across rate bands, examine different offered loads, and calibrate architecture power/timing against physical evidence. No routing cap, workload tuning or repetition enlargement was introduced to force a desired trend.

### 11.5 Reproduction and artifact index

The maintained record is this Markdown file. [Executed commands](../code/output/experiment-study-20261009/engineering/executed-commands.json), [main reproduction instructions](../code/output/experiment-study-20261009/main/analysis/README.txt), stage manifests, frozen source archives and the [artifact index](../code/output/experiment-study-20261009/artifact-index.json) link every stage to retained evidence. Generated outputs live under `code/output/experiment-study-20261009/` and are intentionally Git-ignored; keep or copy this bundle with the source changes when transferring the study.

Reproduce simulations into fresh output directories to preserve collision protection, review the pilot before main, and use its operational evidence for concurrency/timeouts. Regenerate the recorded main tables and plots from the retained raw files without new simulations:

```bash
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 python3 code/scripts/analyze_experiments.py --results-dir code/output/experiment-study-20261009/main --bootstrap-seed 20261009 --bootstrap-resamples 10000
```

The recorded NumPy/Matplotlib versions, PCG64 seed, exact exported coefficients and raw/file hashes define this analysis. Probability maps are independent reconstructions of the frozen native rule, checked against its native test matrix. Reproducibility across different C++ standard-library distributions is not promised. CSV blanks and JSON null retain undefined quantities; operational failures, if introduced in a reproduction, must stay visible rather than being dropped or assigned new seeds.
