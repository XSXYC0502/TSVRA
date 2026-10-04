# TSVRA

C++20 simulator for TSV routing, redundancy and spatial failures in a multilayer chip, with Nuxt visualization and an explicit architecture power estimate.

## Build and run

Run from the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./bin/tsvra --cycles 1000 --seed 42 --output output/demo
```

Windows: [run guide](HOW_TO_RUN_PROJECT_UNDER_WINDOWS.md). `./bin/tsvra --help` lists options; `--print-defaults` prints native defaults.

```bash
cd web-nuxt
npm ci
npm run dev
```

Open Nuxt's printed address. Build C++ first. The webpage has pause/step/speed controls, dashboard, 3D grid, traffic heatmap and failure view.

From `web-nuxt`, `npm test` checks launch validation and state accounting, `npm run build` checks the production build, and `npm run test:integration` exercises the real WebSocket/native process link.

## Timing and traffic

Link delays retain their original settings. Every actual hop adds **15 router cycles**, configurable with `--router-delay`.

| Setting | Native defaults | `config.ini` / web link defaults |
|---|---:|---:|
| Vertical link | 5 | 1 |
| Horizontal link | 500 | 1000 |
| Router per hop | 15 | 15 |

Native vertical/horizontal hops take 20/515 cycles before contention. `--config config.ini` selects the existing example profile; CLI arguments override file settings. Link delays are architectural costs retained at the user's request, not measured 45 nm timings. The power file maps one cycle to 1 ns by default. Playback speed does not change this time.

The original scheduler dispatches a newly routed or rerouted flit on the next cycle. That one-cycle dispatch time is retained and included in route arrival/risk estimates; subsequent hops start on the previous hop's arrival cycle.

One Request is one **64 bit flit**. A packet may contain several flits; packet segmentation is outside this model. Traffic uses one global Bernoulli trial per cycle (`--request-probability`, default 0.5) and spatial weights independent of failure hotspots.

## Failures and routing

`--failure-model uniform` gives spatially uniform independent probabilities. `--failure-model hotspot` gives fixed Gaussian high-risk regions, seeded or manually located:

```bash
./bin/tsvra --cycles 1000 --seed 42 --failure-mode c \
  --initial-failure-rate 0.05 --runtime-failure-rate 1e-6 \
  --failure-model hotspot --hotspot-radius 3 --hotspot-multiplier 5 \
  --hotspot-centers '5,5,0;10,10,1' --output output/hotspot
```

Timing is separate from distribution: `a` initial only, `b` runtime only, `c` both. Initial probability is a defect probability; runtime probability is conditional failure probability **per cycle** for a surviving channel. `--failure-rate` is the compatibility option for both. The old `clustered` name aliases `hotspot`.

Each vertical edge z↔z+1 uses one logical 64 bit TSV channel stored at **lower z**. The top layer has routing nodes, no additional vertical channels. Channel probabilities are not individual physical-via probabilities.

Hotspot probabilities are normalized over the original channels to preserve the configured mean, clipped to [0,1], and frozen throughout a run. Runtime failures use geometric lifetimes. Traffic, hotspot locations, initial faults and runtime faults have separate RNG streams. Exports record the actual seed, including when `--seed 0` requests a random seed.

A* supports congestion/risk weights, path and horizontal-distance limits, and a reliability threshold. `--redundancy shared|corner4|none` selects spares. Path penalties affect selection; physical arrival time contains link/router delays and resource waits. Requests drain after the generation horizon, with runtime failures still active.

See [failure equations and limitations](docs/failure-model.md) for normalization, lifetime sampling and routing risk.

## Power and outputs

[power_model.ini](power_model.ini) contains editable, versioned 45 nm reference coefficients. The model sums continuous router idle energy and **actually launched** router/link events; no training is required. See [equations and calibration](docs/power-model.md) for sources and applicability.

| File suffix | Contents |
|---|---|
| `_summary.csv` | Delivery, latency, failures, hop counts, mean power and energy |
| `_requests.csv` | Terminal requests and coordinates/times/status |
| `_power.csv` | Power/energy every 100 cycles and at completion |
| `_config.json` | Settings, actual seed and exact power parameters |

Energy includes failed requests' launched hops, in-flight work and drain time. Energy per completed flit is empty/JSON null when none completes. Router idle energy remains with no traffic. `--json-stream` sends the same statistics to the webpage; see [the protocol](docs/web-visualization.md).

## Batch experiments

Edit [experiments/example.json](experiments/example.json), then run:

```bash
python3 experiments.py --config experiments/example.json --jobs 4
```

The runner uses the Python standard library, preserves per-run outputs and aggregates reproducible comparisons. `--jobs 1` runs sequentially.

## Editing the code

- `config`, `grid`, `router`: parameters, channels/spares and A*.
- `request_generator`, `simulator`: traffic and the common CLI/web loop.
- `power_model`, `statistics`: energy formulas and exports.
- `web-nuxt`: typed configuration, WebSocket bridge and visualizations.
- `tests`: routing/accounting, spatial failures and numerical power checks.
- `archive`: original data, legacy scripts and reports; see [archive notes](archive/README.md).

Keep parameters in configuration files, units in field names and hardware assumptions in `docs/power-model.md`. Historical results and bytecode caches are outside the active runtime path.
