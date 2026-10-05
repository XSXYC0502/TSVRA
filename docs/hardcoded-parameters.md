# Configuration, assumptions, and implementation limits

This reference replaces the two older hardcoded-parameter inventories. Values below were checked against the current C++ simulator and Nuxt frontend. Configurable defaults are experiment settings; their presence in the code does not establish a physical measurement or a literature calibration.

## Configuration profiles

The native constructor, checked-in example file, and web launch form use different profiles. The simulator reads `code/config/config.ini` only when passed `--config code/config/config.ini`. Configuration files are loaded before CLI overrides, regardless of the argument order. The web server builds explicit CLI arguments and does not load `code/config/config.ini`.

| Setting | Native defaults (`code/backend/src/config.cpp`) | Current `code/config/config.ini` | Web defaults (`code/frontend/shared/simulationConfig.ts`) |
| --- | --- | --- | --- |
| Layers | 4 | 2 | 2 |
| Grid factor / side length | 4 / 16 | 4 / 16 | 4 / 16 |
| Failure timing | `a` (initial only) | `c` (initial + runtime) | `c` |
| Initial failure probability | `1e-5` | `1e-9` via `failure_rate` | `1e-6` |
| Runtime conditional failure probability per cycle | `1e-5` | `1e-9` via `failure_rate` | `1e-6` |
| Vertical / horizontal link delay | 5 / 500 cycles | 1 / 1000 cycles | 1 / 1000 cycles |
| Router delay per actual hop | 15 cycles | 15 cycles | 15 cycles |
| Global request probability per cycle | 0.5 | 0.5 | 0.5 |
| Traffic generation horizon | 100,000 cycles | 10,000,000 cycles | 10,000 cycles |
| Seed | 0 (random) | 0 | 0 |
| Redundancy | `shared` | Inherits `shared` | `shared` |
| Failure distribution | `uniform` | `uniform` | `uniform` |
| Hotspot count / radius / multiplier | 1 / 4 / 5 | 1 / 4 / 5 | 1 / 4 / 5 |
| `lambda1` / `lambda2` | 0 / 0 | Inherit 0 / 0 | Omitted; inherit 0 / 0 |
| Path / horizontal distance / reliability constraints | All disabled (0) | All disabled (0) | Omitted; inherit 0 |
| Power parameter file | `code/config/power_model.ini` | `code/config/power_model.ini` | Omitted; server supplies its absolute path |

The example file remains editable, so its values can diverge from this snapshot. Inspect the effective configuration directly:

```bash
./code/build/bin/tsvra --print-defaults
./code/build/bin/tsvra --config code/config/config.ini --print-defaults
```

`--print-defaults` prints the effective settings after file and CLI overrides. Runs also export `<prefix>_config.json`, including the actual seed, actual failure hotspot centers, and exact power parameters. The web receives these effective settings in its `init` message.

## Editable simulation controls

Set these in an INI file or through their CLI flags. `Config::set_option`, `Config::parse_command_line`, and `Config::validate` in [code/backend/src/config.cpp](../code/backend/src/config.cpp) define accepted values. For batch experiments, use the same INI-style names in `code/config/experiments.example.json`.

| INI key | CLI flag | Meaning / native constraints |
| --- | --- | --- |
| `num_layers` | `--layers` | At least 2 layers. |
| `grid_factor` | `--grid-factor` | At least 1; `shared` requires at least 3. Side length is `4 × grid_factor`. |
| `redundancy` | `--redundancy` | `shared`, `corner4`, or `none`; placement and coverage rules are described below. |
| `failure_mode` | `--failure-mode` | `a`: initial only; `b`: runtime only; `c`: both. |
| `failure_rate` | `--failure-rate` | Compatibility value setting both probabilities unless that probability was explicitly set separately. |
| `initial_failure_rate`, `runtime_failure_rate` | `--initial-failure-rate`, `--runtime-failure-rate` | Separate probabilities in `[0,1]`; override the corresponding compatibility value. Runtime probability is conditional on the channel still being healthy. |
| `failure_model` | `--failure-model` | `uniform` or `hotspot`. `clustered` remains an alias for `hotspot`. |
| `hotspot_count` | `--hotspot-count` | At least 1 when centers are selected automatically. |
| `hotspot_radius` | `--hotspot-radius` | Finite Gaussian sigma in grid cells, greater than 0. |
| `hotspot_multiplier` | `--hotspot-multiplier` | Finite peak relative weight, at least 1, before probability normalization. |
| `hotspot_centers` | `--hotspot-centers` | `x,y,z;x,y,z`; `x,y` are inside the grid and `z` is the lower layer of an interface, `0 ≤ z < layers − 1`. |
| `vertical_delay`, `horizontal_delay` | `--vertical-delay`, `--horizontal-delay` | Positive integer link delays in cycles. |
| `router_delay` | `--router-delay` | Nonnegative integer added to every actual hop, including detours. Its sum with each link delay must fit the 64-bit clock. |
| `request_probability` | `--request-probability` | `[0,1]`; one global Bernoulli trial per cycle, allowing at most one new flit per cycle. |
| `simulation_cycles` | `--cycles` | Positive integer traffic horizon; requests then drain to completion or failure. |
| `random_seed` | `--seed` | Unsigned 32-bit integer; 0 chooses and records a random actual seed. |
| `lambda1`, `lambda2` | `--lambda1`, `--lambda2` | Finite nonnegative congestion and risk weights; 0 disables the corresponding extra cost. |
| `max_path_length` | `--max-path-length` | Unsigned 32-bit hop limit for a route search; 0 disables it. |
| `max_horizontal_distance` | `--max-horizontal-distance` | Unsigned 32-bit limit on accumulated horizontal distance, including detours and previous travel before a reroute; 0 disables it. |
| `reliability_min` | `--reliability-min` | `[0,1]`; 0 disables the route reliability threshold. |
| `power_config` | `--power-config` | Nonempty path to the power parameter INI. |
| `output_prefix` | `--output` | Nonempty prefix for CLI exports; native default is `code/output/tsvra_output`. |

Legacy `cluster_strength` and `cluster_radius` still parse. Strength maps to `hotspot_multiplier = 1 + 5 × strength`, and radius maps to `hotspot_radius`. Prefer the hotspot names; these aliases do not implement an evolving failure cluster.

Native layer/grid bounds protect coordinate types (`layers ≤ 2,147,483,647`, `grid_factor ≤ 536,870,911`), while delays and cycles use unsigned 64-bit integers. These are numeric bounds, not feasible workload sizes or memory guarantees. There is no global 10-million-cycle limit in the current implementation.

## Structural assumptions requiring code changes

| Assumption | Current behavior | Relevant source |
| --- | --- | --- |
| Grid and traffic regions | Grid side is `4 × grid_factor`. Traffic source/destination regions are always 4×4, independently of the selected spare layout. | `code/backend/include/config.hpp`, `code/backend/src/request_generator.cpp`, frontend grid/heatmap helpers |
| Vertical channels | One bidirectional channel per `(x,y,lower_z)`, joining layers `lower_z` and `lower_z + 1`. Channel count is `side² × (layers − 1)`; the top-layer grid points do not add another interface. | `code/backend/src/grid.cpp`, `code/backend/src/simulator.cpp` |
| Shared spares | Overlapping square blocks have side length `grid_factor` and stride `grid_factor − 1`. Spare locations are `(1 + stride × bi, 1 + stride × bj)` on each interface. A bypass selects the nearest available spare in blocks covering the failed channel on that interface. This is 4×4 with stride 3 only when the factor is 4. | `Grid::build_shared_spare_sites`, `Grid::select_spare` |
| Legacy spares | `corner4` marks the four corners of each fixed 4×4 region. Its bypass search scans available spares across the same interface. `none` disables spares. | `Grid::build_legacy_corner4_sites`, `Grid::find_bypass_spare` |
| Traffic spatial weights | Each fixed 4×4 region gets a seeded `0.5 + Uniform(0,1)` weight. Both endpoints sample this fixed region-weight distribution. These traffic hotspots are separate from failure hotspots. | `code/backend/src/request_generator.cpp` |
| Traffic endpoints | Source layer is uniform over `0 … layers − 2`; destination is always the top layer. Within a sampled 4×4 region, redundant positions are rejected, with a 100-attempt limit and a fixed fallback. | `code/backend/src/request_generator.cpp` |
| Failure process | Initial Bernoulli faults and permanent runtime faults on inter-layer channels. Runtime geometric lifetimes are sampled once and processed from an event queue, rather than resampling every channel every cycle. Hotspot probability maps stay fixed and are mean-normalized with saturation handling. | `code/backend/src/grid.cpp`; [failure model](failure-model.md) |
| Routing connectivity | Four horizontal neighbors plus the adjacent layers. The default A* heuristic weights horizontal/vertical Manhattan distance by the corresponding link delay plus router delay. | `code/backend/src/router.cpp` |
| Spare bypass path | Detours expand into actual horizontal and vertical hops, with their real delays and distance counts. Spare vertical channels are reserved for bypasses except permitted endpoint crossings. | `Router::route`, `Router::expand_path` |
| Risk model | Additive `−ln(1−p)` channel hazards; runtime risk includes predicted time exposure. Risk weighting and threshold are configurable, but this probability model is implemented in code. | `Router::route` |
| Run horizon | Traffic stops at the configured horizon; both native and web runs continue draining in-flight requests. Final elapsed cycles can exceed the horizon. | `Simulator::run` |

Changing region size, spare placement, endpoints, or channel semantics requires updating both the native implementation and the preview/visualization code. The failure-region chart also aggregates by fixed 4×4 traffic regions; those bins are not the shared-spare coverage blocks.

## Power parameters and circuit assumptions

[code/config/power_model.ini](../code/config/power_model.ini) contains editable coefficients and provenance for the active explicit model. The default operating reference is 1 GHz, 1 V, a 64-bit flit, and 64 physical signal TSVs per vertical channel. Router population is `layers × side²`. The equivalent router reference has 6 ports, 8 virtual channels, and 2 buffers per virtual channel; these are calibration assumptions, not simulated queues.

The loader currently requires `flit_bits = physical_signal_tsvs = 64`; widening a flit requires code changes and new calibration. Frequency changes the cycle-to-seconds mapping; voltage changes capacitive link energy. Neither automatically re-characterizes router coefficients. TSV width/height fields record geometry provenance and do not automatically derive capacitance. See [the power model reference](power-model.md) and `code/scripts/power_calibration/` for the complete coefficients and captured DSENT evidence. [power-model-final/](power-model-final/) preserves the earlier power planning/data package and is separate from runtime configuration.

## Web limits and presentation settings

Web validation lives in [shared/simulationConfig.ts](../code/frontend/shared/simulationConfig.ts) and is applied by the form and server. It rejects strings, nonfinite numbers, and fractional integer counts. Layers are limited to 2–10 and factor to 1–16 (3–16 for `shared`), so the largest accepted side is 64. Link delays are 1–`10^9`, router delay is 0–`10^9`, and traffic horizon is 1–`10^9` cycles. Seed is 0–`2^32−1`. Hotspot count is 1–1000; positive radius and multiplier at least 1 are bounded by `10^9`. Manual center count must match the selected count.

These are web launch bounds; edit the shared validator and form together to change them. The web validator also accepts optional routing controls and `powerConfig`, which the current form does not expose. Simulator discovery and working-directory overrides are `TSVRA_BIN` and `TSVRA_ROOT` in `server/utils/simulationManager.ts`.

| Limit or setting | Current value / scope | Where to edit |
| --- | --- | --- |
| Lockstep acknowledgements | High: 100 cycles/batch, no delay. Medium: 1 / 10 ms. Slow: 1 / 50 ms. Auto: 1 / 0 ms, or 500 ms after a key event. Step: manual single-cycle credits. | `composables/useWebSocket.ts` |
| Server acknowledgement bounds | Integer 1–100; sends that many `n` credits to native stdin. | `server/api/_ws.ts` |
| Reconnect delay | 2000 ms. | `composables/useWebSocket.ts` |
| Stale data banner | More than 5000 ms without a cycle while running; checked every 1000 ms. | `pages/index.vue` |
| Native visual update hint | Every 10 cycles; cycle/event messages still arrive every executed cycle. | `Simulator::emit_cycle_json` |
| Heatmap refresh interval | Defaults to 10 cycles; editable in the form, rounded and clamped to at least 1. | `stores/simulation.ts`, `components/heatmap/HeatmapView.vue` |
| Native progress / power sampling | Progress every 100 cycles before the horizon, plus its final cycle; power trace every 100 cycles plus the final sample. Paused control loop sleeps 1 ms. | `Simulator::run` |
| Chart/event history | Last 500 entries for throughput, power, recent failure events, and failure-count history. Complete failed-channel positions remain available for spatial counts. | `shared/power.ts`, `stores/simulation.ts` |
| Latency histogram samples | Last 10,000 completed-request latencies; dashboard uses 20 bins. Aggregate statistics retain full-run totals. | `stores/simulation.ts`, `components/dashboard/DashboardView.vue` |
| Stderr buffer | Previous last 200 chunks plus the new chunk: at most 201 entries; a chunk can contain multiple lines. | `stores/simulation.ts` |
| Throughput chart | Displays last 100 retained points; values are cumulative completed requests. | `components/dashboard/DashboardView.vue` |
| Failure chart | Downsamples histories longer than 200 points; always includes the last point. Region bar chart shows top 20 fixed 4×4 regions. | `components/failure/FailureView.vue` |
| Reroute path display | First 50 retained reroute paths; completed/failed reroutes expire after 1000 more cycles. This is a display cap, not a routing limit. | `components/grid3d/Grid3DView.vue`, `stores/simulation.ts` |
| 3D presentation | Layer spacing 3; camera FOV 50°, clipping 0.1–1000, orbit damping 0.1. Units are visual coordinates, not physical chip dimensions. | Grid, heatmap, and failure view components |

Chart history truncation, redraw scheduling, and acknowledgement delays affect display and wall-clock execution speed. They do not alter the configured delay model, simulated seconds, or cumulative native statistics.
