## 3.2.3 Topological Design of the Redundant TSV Layout

The redundant layout must balance repair coverage against area and path overhead. The team used the following multi-objective cost function
\[
\min Cost=\alpha R+(1-\alpha)E_{norm}
\]
where \(R\) is failure risk and \(E_{norm}\) is normalized path overhead. Based on this criterion, the team compared uniform, region-grouped, and honeycomb-style layouts, and selected the region-grouped scheme (Shared Spare) as the default implementation.

**Shared Spare layout implementation.** The \(N \times N\) array is partitioned into overlapping 4×4 sub-blocks using a sliding step of 3. Each block places one shared redundant TSV at the fixed coordinate \((1+3b_i,\;1+3b_j)\). In the default 16×16 array, this yields a 5×5 block grid with 25 redundant TSVs per layer, for a redundancy ratio of about 9.77%. The overlap allows boundary TSVs to be covered by multiple redundant candidates under clustered faults. During routing, the `region_to_spares_` mapping table is used to quickly locate redundant TSVs available to the current region.

**Comparison baselines.** The platform also preserves the Legacy Corner-4 layout, where each 4×4 region places one redundant TSV at each corner, along with a no-redundancy baseline. The `redundancy_layout` configuration parameter switches among them so repair rate, latency, and resource utilization can be compared within one framework.

## 3.4 Simulation Platform Demo and Preliminary Validation

The team completed a simulation-platform demo covering the full workflow from parameter configuration, array modeling, fault injection, and path search to statistics export and visualization. The architecture is shown below:

```
Browser (Vue 3 SPA) ←WebSocket→ Nitro Server ←stdio JSON→ C++ Process (bin/tsvra)
```

The lower-layer C++ simulation core handles modeling and computation, while the Web frontend handles parameter interaction and real-time visualization. The two communicate over stdio using a line-delimited JSON protocol.

### 3.4.1 Core Simulator Framework Development

**The C++ simulation core** (C++20 / CMake) contains six modules:

| Module | Class | Responsibility |
|------|-----|------|
| Configuration management | `Config` | Parses layer count, array size, fault mode/rate, delay parameters, redundancy layout, and related settings |
| Grid modeling | `Grid` | Initializes the 3D array `grid_[z][y][x]` and maintains redundancy, failure, and occupancy state |
| Routing solver | `Router` | Runs A\* pathfinding in 3D, with failed-node bypass and congestion/risk-aware weighted cost |
| Request generation | `RequestGenerator` | Samples regions from hotspot weights and generates requests with about 10% probability per cycle |
| Simulation scheduling | `Simulator` | Drives the discrete clock and coordinates module execution |
| Statistics output | `Statistics` | Aggregates latency, success rate, fault counts, redundant usage, and related metrics |

**Cycle scheduling flow.** Each simulation cycle executes four steps in sequence:
1. `generate_new_requests()` — generate communication requests by hotspot-weighted random sampling
2. `update_transmitting_requests()` — advance in-flight requests along their paths
3. `handle_runtime_failures()` — inject runtime faults and trigger rerouting for in-flight requests
4. `process_pending_requests()` — run A\* routing for pending requests

**A\* routing cost model.** The single-step cost is
\[
c = c_{\text{move}} + \lambda_1 \cdot \text{usage\_count} + \lambda_2 \cdot \varphi(p_{\text{fail}}),\quad \varphi(p) = -\ln(1-p)
\]
where \(c_{\text{move}}\) is the vertical delay (default 5) or horizontal delay (default 500), and \(\lambda_1\) and \(\lambda_2\) control congestion and risk sensitivity respectively. Routing is also constrained by the maximum hop count \(L_{\max}\) and minimum reliability \(R_{\min}\). When a failed TSV is encountered, the algorithm automatically searches the corresponding region for an available redundant TSV to use as a bypass.

**C++ ↔ Web communication protocol.** C++ runs in `--json-stream` mode and writes JSON line by line to stdout: `init` once, then `cycle` per cycle, and finally `done` at completion. The server forwards these messages to the browser through Nitro WebSocket. Control signals are sent as single characters over stdin (`p`/`r`/`s`/`n`), and C++ reads them with non-blocking `poll()`.

**Lockstep speed control.** C++ pauses immediately after startup. The browser sends an `{ type: "ack", count: N }` message, which the server converts into N `n` step commands written to C++ stdin. Different speed levels (`step`, `slow`, `medium`, `high`, `auto`) are distinguished by batch size and the delay between acknowledgments.

**The Web visualization frontend** (Nuxt 4 / Vue 3 / Pinia / Three.js / Chart.js) provides four view types: Dashboard (KPI cards plus charts), 3D Grid (rendered with Three.js InstancedMesh), Heatmap (per-layer traffic density), Failure Analysis (fault map plus timeline), and a Quad four-in-one layout. The Pinia store uses `shallowRef` and `triggerRef` for large datasets such as the grid, active-request map, and cumulative traffic matrix to avoid deep reactivity overhead.

### 3.4.2 Preliminary Simulation Tests and Result Analysis

The tests used the default configuration: 4 layers, a 16×16 array (`grid_factor=4`), the Shared Spare redundancy layout, and a fixed random seed.

**Baseline scenario** (initial failures only, failure rate \(10^{-5}\), 5000 cycles): 542 requests were generated, 56 were completed, 0 failed, 486 remained in flight, and the average latency was 2157.34 cycles. This validated the correctness of request generation, path planning, transmission advancement, and the statistics pipeline.

**Stress scenario** (combined fault mode, failure rate \(10^{-4}\)): 539 requests were generated, 48 were completed, and the run accumulated 287 failed TSVs plus 29 failed redundant TSVs. Even at high fault intensity, the platform continued to inject faults, update state, and recompute routes successfully.

These initial tests show that the platform has completed the key transition from theoretical model to engineering implementation. Next steps include larger-scale array simulations and algorithm comparison experiments.

## 4.3 Technical Roadmap Optimization and Refinement

Future work follows a closed loop of modeling, algorithm design, platform validation, and feedback-driven iteration:

1. **Align the fault model, cost function, and evaluation metrics**: ensure that \(\lambda_1\), \(\lambda_2\), \(L_{\max}\), and \(R_{\min}\) match the experimental evaluation framework.
2. **Integrate A\* with reinforcement learning**: connect an RL module to the existing simulation platform to improve adaptive routing under dynamic faults and complex traffic.
3. **Build a standardized experiment matrix**: vary array size, fault mode/rate, redundancy layout, and random seed to create a reproducible comparison framework.
4. **Enhance the platform and visualization stack**: extend support for key-event replay and multi-scenario comparison views to support papers and project review.
