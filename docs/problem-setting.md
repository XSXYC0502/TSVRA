# Problem setting and simulator semantics

TSVRA compares routing and spare-channel layouts in a multilayer grid under permanent vertical-channel faults. This document describes the current C++ implementation. The grid, delays, traffic and failure probabilities are architectural simulation choices; the model does not establish physical timing or manufacturing yield for a particular chip.

For build and launch commands, see the [project README](../README.md). Detailed fault equations are in [failure-model.md](failure-model.md), energy assumptions in [power-model.md](power-model.md), and browser controls and streaming in [web-visualization.md](web-visualization.md).

## Grid and vertical channels

Let `g = grid_factor`, `N = 4g`, and `L = num_layers`. Coordinates satisfy `0 <= x,y < N` and `0 <= z < L`; valid configurations have at least two layers.

Every coordinate represents an equivalent routing node. A vertical edge connects `(x,y,z)` and `(x,y,z+1)` through one logical 64-bit TSV channel stored at **the lower z**, in either direction. The top layer contains routing nodes but has no extra vertical channels. Consequently:

| Quantity | Formula | Native default: N=16, L=4 |
| --- | --- | ---: |
| Routing nodes / equivalent hardware routers | `N²L` | 1,024 |
| Logical vertical channels | `N²(L-1)` | 768 |
| Channels per inter-layer interface | `N²` | 256 |

A channel failure disables its vertical edge in both directions. Horizontal routing through its coordinate remains possible. Fault probabilities refer to the logical channel; the separate power model supplies the physical signal-via and equivalent router assumptions.

## Redundancy layouts and bypasses

`--redundancy shared|corner4|none` selects which existing channel positions are spares. Spares replace ordinary-channel positions within the same grid population; choosing a layout does not add routing nodes or channels.

| Layout | Spare placement and search | Spares per interface at N=16 |
| --- | --- | ---: |
| `shared` | One spare per overlapping block; search blocks covering the failed channel | 25 (9.77%) |
| `corner4` | Four corners of each separate 4×4 region; search all spares on the failed channel's interface | 64 (25%) |
| `none` | No designated spares; A* can still seek another healthy ordinary vertical edge | 0 |

For `shared`, the implementation uses **block width `g` and stride `g-1`**. Shared redundancy requires `g >= 3`. On each interface, block `(i,j)` covers the in-grid positions in `[i(g-1), i(g-1)+g-1] × [j(g-1), j(g-1)+g-1]`, with its spare at `(1+i(g-1), 1+j(g-1))`. The number of blocks along an axis is `floor((N-2)/(g-1)) + 1`.

With the native `g=4`, these are overlapping 4×4 blocks with stride 3: five blocks per axis and spare x/y coordinates `{1,4,7,10,13}`. A channel at an overlap can belong to up to four blocks; an interior channel belongs to one. Other grid factors change the shared block size and stride, while the traffic regions and `corner4` regions remain 4×4.

When A* considers a failed vertical edge, it asks the selected layout for the nearest healthy spare that is available at the candidate's arrival time. Distance is horizontal Manhattan distance. The bypass moves horizontally to that spare, crosses the same interface, and moves horizontally back to the original edge's intended destination. Horizontal segments are expanded into adjacent steps, x before y; every resulting hop receives its own timing, congestion and constraint checks.

Spare vertical edges are reserved for bypasses, with an exception when the proposed crossing reaches a route endpoint. Horizontal paths may pass through spare coordinates. The spare search chooses one nearest available candidate; it does not enumerate all alternative spares if that candidate's expanded detour fails a later routing check. A* can still explore other neighboring edges.

## Traffic and request states

One `Request` represents one **64-bit flit**. Requests are generated with a single global Bernoulli trial per cycle, controlled by `request_probability` (native default **0.5**). This is a network-wide rate, not a separate probability at each router.

The generator divides every layer into 4×4 traffic regions. It assigns each region a fixed random weight in `[0.5,1.5]` during initialization. Source and destination regions are sampled using these weights, then coordinates within the chosen region are sampled while avoiding the layout's designated spare positions. These traffic weights use their own random stream and are independent of the failure hotspots.

Sources are sampled uniformly over layers `0..L-2`; destinations are always on the top layer `L-1`. The router can move both upward and downward, so the top-layer destination does not impose a monotonic upward path.

Failure distribution changes do not consume the traffic RNG stream. Changing redundancy layout can change it indirectly: rejecting spare endpoints takes different numbers of draws. Consequently, matching seeds across layouts does not guarantee identical request counts or endpoint sequences; exact workload matching would require an additional replay or layout-independent generation mechanism.

```text
PENDING -> TRANSMITTING -> COMPLETED
        \-> FAILED       \-> FAILED
```

Pending requests receive one routing attempt during that cycle. A successful route enters transmission; a request with no route becomes failed. Transmission proceeds through adjacent horizontal or vertical hops. Each request's current position is its last reached node, including while it waits for or traverses the next hop.

## Timing and contention

Each actual hop takes its link delay plus `router_delay`:

| Setting | Native defaults | Example config / browser link profile |
| --- | ---: | ---: |
| `vertical_delay` | 5 cycles | 1 cycle |
| `horizontal_delay` | 500 cycles | 1,000 cycles |
| `router_delay` | 15 cycles | 15 cycles |

Thus native vertical/horizontal hops take 20/515 cycles before waiting. These cycle costs are not measured 45 nm interconnect delays. The selected power parameters map cycles to seconds; their reference frequency is 1 GHz. Browser playback speed changes wall-clock presentation only.

A newly routed or rerouted flit dispatches on the next simulation cycle. Later hops can start on the preceding hop's arrival cycle. The simulator reserves a resource until the next hop's arrival; an occupied resource delays departure:

```text
departure = max(earliest_departure, resource.available_at)
arrival   = departure + link_delay + router_delay
```

A vertical hop uses the lower-z channel object as its resource. A horizontal hop uses the destination node's object. On lower layers these uses share the same `available_at` timestamp, so the current abstraction can couple horizontal-node and vertical-channel contention. Top-layer horizontal nodes also have occupancy timestamps. This is resource serialization rather than a detailed model of router buffers, virtual channels or separate directional link queues.

Route search observes current occupancy and estimates arrival times, but it does not reserve a complete path. Actual reservations occur hop by hop. Other flits can therefore change the waiting time after planning, and the final completion timestamp comes from actual transmission.

## A* objective and constraints

The router has six adjacent neighbors: `±x`, `±y`, `±z`, limited to the grid. The default heuristic is Manhattan distance weighted by the corresponding link-plus-router delay. Nonnegative congestion and risk penalties are added to the routing objective, while physical arrival time is tracked separately.

For each expanded hop, the objective adds its physical elapsed time, including waits, plus:

```text
lambda1 * resource.usage_count + lambda2 * added_risk
```

`usage_count` counts occurrences in active transmitting paths, including nodes not yet reached; it is a path-load proxy. Both weights default to zero. Setting either weight changes route preference, not the hop's service duration or physical timestamp.

Risk applies to vertical crossings. In runtime modes it uses the channel's conditional per-cycle probability and exposure from planning through predicted crossing completion, including dispatch, preceding hops and waits. Initial-only mode uses initial defect probability as a design-risk proxy. See [failure-model.md](failure-model.md) for the precise log-survival equations and approximation limits.

| Constraint | Implemented scope | Disabled value |
| --- | --- | --- |
| `max_path_length` | Actual adjacent hops in one routing attempt, including expanded bypasses; rerouting starts a new remaining-path calculation | 0 |
| `max_horizontal_distance` | Completed horizontal hops plus the candidate remaining route's horizontal hops; retained across reroutes | 0 |
| `reliability_min` | Estimated reliability of the route being planned; prunes when accumulated log-risk exceeds `-log(reliability_min)` | 0.0 |

A* retains multiple labels at a node when cost, arrival time or active constraints leave different possible futures. This preserves alternatives that a single best-cost label would discard.

## Failures and rerouting

Fault timing and spatial distribution are independent choices:

- `failure_mode=a`: initial defects only.
- `failure_mode=b`: runtime failures only.
- `failure_mode=c`: initial and runtime failures.
- `failure_model=uniform|hotspot`: uniform probabilities or fixed Gaussian regions. `clustered` is a compatibility spelling for `hotspot`.

`initial_failure_rate` is a defect probability at initialization. `runtime_failure_rate` is conditional failure probability per cycle for a surviving channel. `failure_rate` is the compatibility setting for both; explicit stage-specific settings override their corresponding value. Initial and runtime maps are frozen, normalized over the original channels, and include spares. Runtime first-failure times are sampled as geometric lifetimes. Failures are permanent for a run.

When a request's next vertical channel is failed, including during a reserved or launched hop, the simulator cancels that crossing and attempts a new route from its last reached node. Completed horizontal distance is retained. If rerouting fails, the request terminates as failed. Energy of an already launched hop remains charged. The model does not recompute a route merely because its congestion estimate changes.

## Cycle order and drain

For cycle `t`, the native simulator and browser-backed native process execute the same stages:

1. Generate at most one request if `t < simulation_cycles`.
2. Advance transmitting requests and handle observed failed next channels.
3. Apply runtime failures due at `t`, if enabled.
4. Route pending requests.

This ordering places due fault events after that cycle's transmission stage: a hop that completes in stage 2 has completed before a fault applied in stage 3. A newly failed channel affects later transmission updates and new routes.

`simulation_cycles` is the **traffic-generation horizon**. After it, the simulation continues without new traffic until all pending/transmitting requests complete or fail. Runtime faults, hop events, router idle energy and power traces continue during this drain. A browser stop can end a run early with remaining requests and partial accounting.

## Outputs and metric meanings

Normal CLI execution writes `<prefix>_summary.csv`, `<prefix>_requests.csv`, `<prefix>_power.csv`, and `<prefix>_config.json`. JSON streaming supplies corresponding events and statistics to the browser. The configuration export records effective settings, the actual seed, actual hotspot centers, and exact power parameters.

| Metric | Meaning |
| --- | --- |
| Success Rate (%) | `100 * completed / generated`, at final termination including drain |
| Terminal Delivery Rate (%) | `100 * completed / (completed + failed)` |
| Route Failure Rate (%) | `100 * failed / generated`; includes initial route and transmission/reroute failures |
| In Flight At Horizon | Pending plus transmitting requests immediately after the generation horizon |
| In Flight At End | Pending plus transmitting requests at actual termination; zero after a normal complete drain |
| Average / min / max latency | `complete_time - generate_time`, for completed flits only; includes waits and rerouting |
| Average Horizontal Distance | Completed horizontal hops averaged over completed and failed requests |
| Failed TSVs / Failed Redundant TSVs | Permanently failed logical channels at actual termination |
| Redundant TSV Usages | Actual launches through spare vertical channels, including launches by requests that later fail |
| Failed Vertical Encounters / Spare Found / Spare Unavailable | A* search diagnostics, including candidate edges not ultimately used |

With a normal complete drain, generated requests equal completed plus failed, and success rate equals terminal delivery rate. The success metric does not count completions specifically at the generation horizon. CSV request details contain terminal requests only; failed requests use `N/A` for completion time and latency. CSV rates are omitted when their denominator is zero, and latency summaries are omitted when nothing completes.

Power counters count actually launched hops, including unsuccessful traffic and drain. Router events equal horizontal plus vertical launches; spare vertical launches are a subset of vertical launches. Mean power, cumulative energy, interval power and energy per completed flit follow [power-model.md](power-model.md).

## Native configuration defaults

`./bin/tsvra --print-defaults` reports the effective native configuration. A file is loaded only with `--config`; command-line values then override file values. [config.ini](../config.ini) is an editable example profile with its own choices, and the browser form also supplies a profile. Their values should not be treated as C++ constructor defaults.

| Parameter | Native default |
| --- | --- |
| `num_layers`, `grid_factor` | 4, 4 |
| `failure_mode`, `failure_model` | `a`, `uniform` |
| `failure_rate`, `initial_failure_rate`, `runtime_failure_rate` | 1e-5 each |
| `redundancy` | `shared` |
| `vertical_delay`, `horizontal_delay`, `router_delay` | 5, 500, 15 cycles |
| `request_probability` | 0.5 |
| `simulation_cycles` | 100,000 generation cycles |
| `random_seed` | 0: choose and export an actual random seed |
| `lambda1`, `lambda2` | 0.0, 0.0 |
| `max_path_length`, `max_horizontal_distance` | 0, 0 |
| `reliability_min` | 0.0 |
| `hotspot_count`, `hotspot_radius`, `hotspot_multiplier` | 1, 4 grid cells, 5 |
| `hotspot_centers` | Empty: use seeded automatic centers in hotspot mode |
| `power_config`, `output_prefix` | `power_model.ini`, `tsvra_output` |

Implementation references: [grid.cpp](../src/grid.cpp), [request_generator.cpp](../src/request_generator.cpp), [router.cpp](../src/router.cpp), [simulator.cpp](../src/simulator.cpp), [statistics.cpp](../src/statistics.cpp), and [config.cpp](../src/config.cpp). The topology, bypass timing, constraints, risk, drain and CLI/JSON accounting are exercised by the [native tests](../tests/test_main.cpp), [failure tests](../tests/failure_tests.cpp) and [integration tests](../tests/integration_tests.py).
