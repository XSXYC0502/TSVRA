# Project report draft

This draft describes the implemented platform. Insert newly reproduced experiment results where indicated before using it as a final research report. The source documents are [problem setting](problem-setting.md), [architecture](architecture.md), [failure model](failure-model.md), and [power model](power-model.md).

## Redundant TSV layout

TSVRA models a multilayer routing grid with logical vertical channels between adjacent layers. It offers three redundancy layouts: shared spares assigned to overlapping regions, a corner4 baseline, and a baseline without spares. With the default grid factor of 4, the shared layout uses overlapping 4 × 4 regions with stride 3. A 16 × 16 interface has 25 shared spare channels, approximately 9.77% of its 256 channels. The corner4 layout has 64 spare channels per interface. These counts apply to each of the `num_layers - 1` vertical interfaces; top-layer routing nodes add no vertical channels.

The shared layout associates each spare with a region and lets failed channels at overlaps access several region candidates. This provides a basis for investigating repair coverage and path overhead. The implementation supports these comparisons, but the layout alone does not demonstrate a reliability, area, or power advantage. The current code does not implement a honeycomb layout or an optimizer for a combined area/energy objective.

## Simulation platform

The platform consists of a C++20 simulator and a Nuxt/Vue browser application. The browser sends configuration and playback commands over WebSocket to a Nitro server, which starts the native process and forwards its JSON stream. Simulation behavior is shared between normal command-line execution and JSON streaming.

| Module | Implemented role |
| --- | --- |
| `Config` | Parse and validate topology, traffic, failures, routing constraints, timing, and power configuration |
| `Grid` / `TSV` | Represent routing nodes, logical channels, spares, resource occupancy, fixed probability maps, and scheduled failures |
| `Router` | Search routes with congestion/risk penalties, spare bypasses, and path/reliability constraints |
| `RequestGenerator` | Generate individual 64 bit flits with a configurable global per-cycle probability and spatial traffic weights |
| `Simulator` | Coordinate generation, transmission, failures, rerouting, drain, streaming, and hop energy events |
| `Statistics` / `PowerModel` | Export delivery, latency, failures, diagnostics, actual event counts, and modeled power/energy |

During the generation horizon, each cycle generates traffic, updates transmissions, applies due runtime failures when enabled, and processes pending requests. After generation ends, the same loop drains remaining traffic while runtime faults and idle power remain active. A graceful user stop can end a run with requests still in flight.

Every launched hop includes link delay plus router service time; contention adds resource waits. New routing and rerouting retain one cycle of dispatch time. Congestion and risk penalties guide route selection, while physical arrival time is tracked separately. Failure probabilities apply to logical 64 bit vertical channels. Uniform and hotspot distributions are independent of traffic hotspot weights, so traffic concentration does not automatically change a channel's failure probability.

The power estimate sums continuous router idle energy and the router/link energy of actual launched hops, including work that later fails and work during drain. The current simulator loads editable coefficients from `code/config/power_model.ini`; its router reference is calibrated offline from DSENT evidence. The retained [synthetic power-prediction package](power-model-final/README_final_plan.md) supplies an independent training/prediction workflow and is not invoked by the simulator.

## Visualization and controls

The browser provides a dashboard, a 3D grid, a traffic heatmap, a failure view, and a quad layout. All views consume one simulation store. The 3D display uses instanced meshes, while the store caps event and chart histories without discarding cumulative accounting. The dashboard distinguishes cumulative average power from interval power and uses simulated elapsed time for both.

Playback controls grant native cycle credits over stdin. Pause clears pending credits; step grants one cycle; speed modes change the browser's credit batch size and delay. Playback speed affects wall-clock presentation, rather than native link/router delays or simulated energy. The native command reader supports Windows and POSIX platforms.

## Validation and experiment results

The repository contains checks for routing and redundancy invariants, request accounting and drain, probability normalization and runtime lifetimes, routing risk and physical timing, numerical power accounting, native CLI/JSON consistency, and browser state/launch validation. A separate web integration script exercises the real server/native link. These checks establish implementation behavior; they do not validate the model against silicon measurements.

Run the existing checks from the repository root:

```bash
cmake -S code/backend -B code/build -DCMAKE_BUILD_TYPE=Release
cmake --build code/build --parallel
ctest --test-dir code/build --output-on-failure
npm --prefix code/frontend ci
npm --prefix code/frontend test
npm --prefix code/frontend run build
npm --prefix code/frontend run test:integration
```

For a reproducible comparison, edit `code/config/experiments.example.json` and run:

```bash
python3 code/scripts/run_experiments.py --config code/config/experiments.example.json --jobs 4
```

Before reporting numerical conclusions, retain the batch specification, source revision, effective per-run configurations, actual seeds, raw exports, logs, and aggregation method. State the traffic horizon and whether results include drain. Report delivery success and completed-flit latency alongside hop counts, bypass outcomes, failures, and energy; treat energy per completed flit as undefined when no flit completes. Matched seeds do not guarantee identical request traces across redundancy layouts, because endpoint rejection changes traffic RNG consumption; report that limitation unless a controlled workload is introduced.

The [historical reports](historical-results/README.md) preserve earlier aggregate tables, but their incomplete metadata prevents using them as current validation results. This draft deliberately leaves layout rankings and statistical conclusions to a newly documented experiment.

## Follow-up work

Priority follow-up work is a controlled multi-seed redundancy comparison, uncertainty analysis, better source/toolchain provenance, automated cross-platform checks, and physical calibration when suitable measurements become available. Reinforcement learning and additional layouts remain possible future extensions. See the [roadmap](roadmap.md) for current implementation status and proposed work.
