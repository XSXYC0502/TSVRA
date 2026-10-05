# TSVRA

C++20 simulator for TSV routing, redundancy and spatial failures in a multilayer chip, with Nuxt visualization and an explicit architecture power estimate.

## Build and run

Run from the repository root:

```bash
cmake -S code/backend -B code/build -DCMAKE_BUILD_TYPE=Release
cmake --build code/build --parallel
ctest --test-dir code/build --output-on-failure
./code/build/bin/tsvra --cycles 1000 --seed 42 --output code/output/demo
```

Windows: [run guide](docs/windows.md). `./code/build/bin/tsvra --help` lists options; `--print-defaults` prints native defaults.

```bash
cd code/frontend
npm ci
npm run dev
```

Open Nuxt's printed address. Build C++ first. The webpage has pause/step/speed controls, dashboard, 3D grid, traffic heatmap and failure view.

From `code/frontend`, `npm test` checks launch validation and state accounting, `npm run build` checks the production build, and `npm run test:integration` exercises the real WebSocket/native process link.

## Timing and traffic

Link delays retain their original settings. Every actual hop adds **15 router cycles**, configurable with `--router-delay`.

| Setting | Native defaults | `code/config/config.ini` / web link defaults |
|---|---:|---:|
| Vertical link | 5 | 1 |
| Horizontal link | 500 | 1000 |
| Router per hop | 15 | 15 |

Native vertical/horizontal hops take 20/515 cycles before contention. `--config code/config/config.ini` selects the existing example profile; CLI arguments override file settings. Link delays are architectural costs retained at the user's request, not measured 45 nm timings. The power file maps one cycle to 1 ns by default. Playback speed does not change this time.

The original scheduler dispatches a newly routed or rerouted flit on the next cycle. That one-cycle dispatch time is retained and included in route arrival/risk estimates; subsequent hops start on the previous hop's arrival cycle.

One Request is one **64 bit flit**. A packet may contain several flits; packet segmentation is outside this model. Traffic uses one global Bernoulli trial per cycle (`--request-probability`, default 0.5) and spatial weights independent of failure hotspots.

## Failures and routing

`--failure-model uniform` gives spatially uniform independent probabilities. `--failure-model hotspot` gives fixed Gaussian high-risk regions, seeded or manually located:

```bash
./code/build/bin/tsvra --cycles 1000 --seed 42 --failure-mode c \
  --initial-failure-rate 0.05 --runtime-failure-rate 1e-6 \
  --failure-model hotspot --hotspot-radius 3 --hotspot-multiplier 5 \
  --hotspot-centers '5,5,0;10,10,1' --output code/output/hotspot
```

Timing is separate from distribution: `a` initial only, `b` runtime only, `c` both. Initial probability is a defect probability; runtime probability is conditional failure probability **per cycle** for a surviving channel. `--failure-rate` is the compatibility option for both. The old `clustered` name aliases `hotspot`.

Each vertical edge z↔z+1 uses one logical 64 bit TSV channel stored at **lower z**. The top layer has routing nodes, no additional vertical channels. Channel probabilities are not individual physical-via probabilities.

Hotspot probabilities are normalized over the original channels to preserve the configured mean, clipped to [0,1], and frozen throughout a run. Runtime failures use geometric lifetimes. Traffic, hotspot locations, initial faults and runtime faults have separate RNG streams. Exports record the actual seed, including when `--seed 0` requests a random seed.

A* supports congestion/risk weights, path and horizontal-distance limits, and a reliability threshold. `--redundancy shared|corner4|none` selects spares. Path penalties affect selection; physical arrival time contains link/router delays and resource waits. Requests drain after the generation horizon, with runtime failures still active.

See [failure equations and limitations](docs/failure-model.md) for normalization, lifetime sampling and routing risk.

## Power and outputs

[code/config/power_model.ini](code/config/power_model.ini) contains editable, versioned 45 nm reference coefficients. The model sums continuous router idle energy and **actually launched** router/link events; no training is required. See [equations and calibration](docs/power-model.md) for sources and applicability.

| File suffix | Contents |
|---|---|
| `_summary.csv` | Delivery, latency, failures, hop counts, mean power and energy |
| `_requests.csv` | Terminal requests and coordinates/times/status |
| `_power.csv` | Power/energy every 100 cycles and at completion |
| `_config.json` | Settings, actual seed and exact power parameters |

Energy includes failed requests' launched hops, in-flight work and drain time. Energy per completed flit is empty/JSON null when none completes. Router idle energy remains with no traffic. `--json-stream` sends the same statistics to the webpage; see [the protocol](docs/web-visualization.md).

## Batch experiments

Edit [code/config/experiments.example.json](code/config/experiments.example.json), then run:

```bash
python3 code/scripts/run_experiments.py --config code/config/experiments.example.json --jobs 4
```

The runner uses the Python standard library, preserves per-run outputs and aggregates reproducible comparisons. `--jobs 1` runs sequentially.

## Editing the code

| Location | Contents |
| --- | --- |
| `code/backend/` | C++ source, headers, native tests, CMake project and Windows presets |
| `code/frontend/` | Nuxt application, typed configuration, WebSocket bridge and visualizations |
| `code/config/` | Simulation, power, batch experiment and DSENT calibration settings |
| `code/scripts/` | Python batch runner, offline calibration and captured calibration evidence |
| `code/build/` | Generated native build files and `bin/` executables; ignored by Git |
| `code/output/` | Generated run/batch outputs; ignored by Git |
| `docs/` | Model/architecture references, report draft, roadmap and retained research materials |

Run native and batch commands from the repository root. Run frontend commands from `code/frontend/` or use `npm --prefix code/frontend ...`. Configuration and explicit relative file paths use the native process's working directory. The frontend discovers the repository root and supplies the default power file explicitly. Native output defaults to `code/output/tsvra_output`.

The [documentation index](docs/README.md) provides the detailed references. Git/environment metadata remains separate from the project directories.

Keep parameters in configuration files, units in field names and hardware assumptions in `docs/power-model.md`. The [retained power-prediction package](docs/power-model-final/README_final_plan.md) and [historical reports](docs/historical-results/README.md) are documented separately from the active runtime model. Bytecode caches are outside the active runtime path.
