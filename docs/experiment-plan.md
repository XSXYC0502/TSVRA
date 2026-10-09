# TSVRA test plan, experiment results, and report

**Status: design only; awaiting implementation approval.** Updated 9 October 2026.

This Markdown document is the canonical record for the test plan, approval decisions, execution history, later experiment results, and final report. It translates the requirements in the supplied `Exp Plan.docx` into a reproducible workflow; those requirements are recorded in section 1. Maintain this file directly; Word documents are no longer the working format.

No simulator source, configuration, or tests have been changed, and no builds, tests, or simulations have been executed for this proposal. Results and conclusions below remain pending until approved work produces verifiable evidence.

The design is based on repository revision `729023b0bc648e4f244a6c5ff136ab9ce1c15c96`. All added choices below are proposed settings, not requirements already specified in the source document or measured results.

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

## 7. Tasks after approval

Use **GPT-6.1-sol with ultra reasoning** for three workstreams, supervised by the primary agent. Assign non-overlapping files and agree the run/summary schema before parallel editing.

| Owner | Scope and proposed files | Reviewable result |
|---|---|---|
| Agent A: native correctness | Existing `code/backend/tests/` suites; narrowly scoped native fixes only for demonstrated defects | Coverage inventory, focused fixtures, passing native and CLI/JSON checks. |
| Agent B: experiment configuration | New pilot/main JSON files under `code/config/`; manifest/validation support under `code/scripts/` only where needed | Dry-run count/seed checks, frozen explicit settings, collision-safe commands and provenance. |
| Agent C: analysis and presentation | New analysis/plotting support under `code/scripts/`; frontend tests only for a demonstrated coverage gap | Joined request/run/summary sheets, missing-value checks, reproducible intervals and plots. |
| Primary agent | Integration review, shared schemas, existing frontend verification, pilot execution, budget review and main-run coordination | Approved gates, preserved evidence, final report and conclusions tied to raw data. |

Sequence: approve this design → implement focused verification/configuration/analysis work → run correctness gate → freeze source/settings → pilot and cost review → main sweep if approved settings remain suitable → audited tables/plots/conclusions. Neither code edits nor runs are authorized by the current design-only request. Any material change to the agreed research question or workload returns for design review.

Completion evidence requires: all required engineering checks pass; every planned cell has accounted-for runs/seeds; normal-run terminal and energy identities pass; all operational failures are visible/resolved or explicitly qualify the results; raw CSV/config/log bundles are retained; the summary/plots regenerate from them; and no undefined value is converted to zero.

## 8. Evidence bundle and approval

Each stage should produce an isolated bundle containing the exact batch JSON, resolved command lines, source revision/diff status, executable and power-file hashes, compiler/CMake/Python/Node versions, machine description, stage timestamps, jobs/timeouts, analysis settings and all per-run logs/configuration/request/summary/power files. Retain planned and actual seeds plus actual hotspot centers. Reproducibility is promised for a recorded toolchain; different C++ standard-library distribution implementations may produce different traces.

Repository basis: [README](../README.md), [problem setting](problem-setting.md), [failure model](failure-model.md), [power model](power-model.md), [architecture](architecture.md), [roadmap](roadmap.md), [grid implementation](../code/backend/src/grid.cpp), [statistics](../code/backend/src/statistics.cpp), [simulator interface](../code/backend/include/simulator.hpp), [native tests](../code/backend/tests/), [batch runner](../code/scripts/run_experiments.py), [example batch configuration](../code/config/experiments.example.json), and [frontend tests](../code/frontend/tests/). Review recommendations were obtained from three GPT-6.1-sol ultra subagents without implementation or simulation.

**Approval requested:** the initial-only interpretation; shared layout; proposed manual centers/radius/multiplier and bottom-interface scope; fixed native timing and traffic/horizon profile; correctness gate; 156-run pilot and 3,960-run main budget; uncertainty/recording rules; and workstream split. If approved, implementation begins with the correctness/configuration work and pilot. Further changes to scientific settings or run budget require an updated reviewable proposal.

## 9. Approval, changes, and execution record

The requested Markdown format is approved. Implementation and experiment execution remain pending the user's approval of the design. Record approval scope here before changing a proposed stage to an authorized stage.

| Date | Decision or change | Effect on execution |
|---|---|---|
| 2026-10-09 | Initial design prepared and independently reviewed by three GPT-6.1-sol ultra agents. | Design only; no implementation or experiments. |
| 2026-10-09 | User requested Markdown for the plan, later experiment results, and report. | This file becomes the maintained record; implementation approval is still pending. |

For subsequent design changes, append the date, changed setting, reason, expected effect on comparisons/run budget, and approval reference where a material change requires approval. Keep the old executed settings identifiable so results from different protocols are not combined accidentally.

| Stage | Planned scope | Current status | Evidence |
|---|---|---|---|
| Design review | Code review, experiment settings, metric definitions and task split | Draft prepared; awaiting implementation approval | Sections 1–8 |
| Implementation | Focused verification, configurations and analysis tools | Not started | Pending |
| Correctness gate | Required native/frontend checks and focused additions | Not executed | Pending |
| Pilot | 156 runs; seeds 901–903 | Not executed | Pending |
| Pilot review | Runtime/resource forecast, variability and suitability of approved settings | Pending pilot | Pending |
| Main sweep | 3,960 runs; rate-dependent prefixes of seeds 101–130 | Not executed | Pending |
| Analysis and report | Validated tables, intervals, plots and evidence-linked findings | Not started | Sections 10–11 reserved below |

For each executed stage, append a record with:

- Approval scope, start/end timestamps, operator/agent and execution status.
- Frozen source revision and relevant changes; compiler/CMake/Python/Node versions and build settings.
- Executable and power-file hashes, effective operating parameters, machine/resources, jobs and timeout.
- Exact batch configuration and invocation, seeds, output-directory links, logs and raw artifacts.
- Planned, attempted, accepted, failed, missing and retried run counts; completeness by cell/seed.
- Check results, anomalies, retry disposition and any change needed before the next stage.

Do not count code review or document-format checks as passed simulator tests. Preserve failures and retry history. A successful process exit alone does not establish valid drained/accounted results.

## 10. Experiment results — pending execution

**No new experiment results have been collected.** The budgets in this document are planned counts. This section will be populated from retained, validated outputs after approval and execution. Historical aggregates are reference material and will not be inserted as current results.

### 10.1 Correctness evidence

| Check group | Outcome | Source/build revision | Log or artifact |
|---|---|---|---|
| Native topology, routing, failure and power suites | Not executed | Pending | Pending |
| Native CLI/JSON integration and replay | Not executed | Pending | Pending |
| Frontend helpers, production build and WebSocket integration | Not executed | Pending | Pending |
| Batch matrix, provenance and result/analysis validation | Not executed | Pending | Pending |

Add actual commands, outcomes and any regression fixes here once executed. Record missing prerequisites separately from assertion failures.

### 10.2 Pilot results and production decision

| Item | Measured result |
|---|---|
| Expected pilot runs | 156 planned |
| Accepted / failed / missing runs | Not executed |
| Runtime and output size by rate/layer/scenario | Not measured |
| Resource use and useful concurrency | Not measured |
| Generated/completed counts and drain duration | Not measured |
| Between-seed variability and undefined metrics | Not measured |
| Forecast for main sweep and selected timeout/jobs | Pending pilot measurements |
| Decision to proceed or revise settings | Pending pilot and approval scope |

Link the pilot manifest, complete run table, logs and diagnostics here. Any material revision must be reflected in sections 2–4 and the dated change record before an affected main run starts.

### 10.3 Main results and uncertainty

Reserve links here for the validated request sheet, per-run sheet, cell summary, analysis settings and plot directory. Raw tables remain separate artifacts; this document records their provenance and interpretation.

| Result bundle | Location | Validation/completeness |
|---|---|---|
| Batch specification and provenance manifest | Pending | Not executed |
| Per-request endpoints, timestamps, status and latency | Pending | Not executed |
| Per-run metrics, counts, settings and run errors | Pending | Not executed |
| Per-cell summaries, SD, 95% intervals and defined sample counts | Pending | Not executed |
| Paired hotspot-minus-uniform comparisons | Pending | Not executed |
| Figures and commands/settings to regenerate analysis | Pending | Not executed |

Organize the readable main tables by layer and fault scenario, with configured rate, observed defect fraction, planned/accepted repetitions, delivery success, completed-only latency, horizontal/spare activity, mean power/components and energy per completed flit. Report uncertainty and undefined/zero-completion counts. Label repetition-weighted and pooled quantities separately.

Mark incomplete cells explicitly; they must not silently become final comparisons. Require unique terminal request IDs, correct request/energy accounting and zero final in-flight requests for accepted full runs. Blank/null values remain undefined, rather than becoming zero. Every displayed value or figure must link back to its source result bundle and analysis version.

### 10.4 Figures and anomalies

Add the rate curves, paired differences, delivery-by-latency curves, spatial probability diagnostics and power/component figures described in section 6. Each caption should identify settings, sample counts, interval method, units and any missing cells.

Record unexpected behavior with its affected run IDs, supporting artifacts, investigation and disposition. Keep model predictions and explanations distinct from observed results.

## 11. Final experiment report — pending evidence

The final report will be written here once validated results are available. Populate the following subsections from the completed execution record and linked result bundles; do not supply numerical findings or conclusions in advance.

### 11.1 Research questions and actual methods

Pending. State the questions answered, actual frozen implementation/settings, fault geometry, traffic and layer exposure, seed/repetition counts, correctness gate, drain/accounting rules, metrics and analysis method. Explain any approved differences from this original proposal.

### 11.2 Findings

Pending. Answer the delivery, latency/overhead and modeled power/energy questions using the tables, uncertainty intervals and figures in section 10. Give sample/completeness counts and evidence links for each finding. Report a saturated delivery result as an observed model outcome, without converting it into a physical-reliability claim.

### 11.3 Interpretation and limitations

Pending. Discuss initial-only logical-channel defects, Gaussian normalization/clipping, hotspot/spare alignment, changing bottom-interface exposure, changing offered load per router, completed-only latency, idle-power scaling and finite-seed uncertainty. Identify unresolved operational failures or implementation concerns and their effect on the conclusions.

### 11.4 Conclusions and follow-up

Pending. State only conclusions supported by the executed study. Distinguish demonstrated implementation behavior from modeled architectural estimates. Propose extensions or revised experiments only when the evidence justifies them.

### 11.5 Reproduction and artifact index

Pending. Link the frozen source, batch specifications, provenance manifests, raw outputs, validated tables, figures and exact analysis commands. A reviewer should be able to trace every result and regenerate the report's tables/plots from the retained evidence.
