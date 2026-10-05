# Web visualization and streaming

The browser configures and controls the native simulator through one Nitro WebSocket endpoint:

```text
Vue views / Pinia ↔ WebSocket /api/_ws ↔ Nitro ↔ stdio ↔ bin/tsvra --json-stream
```

Nitro forwards native JSON lines and controls pacing over stdin. The browser's playback speed changes how quickly cycles execute, while power and energy use simulated time. All views share the same store. [architecture.md](architecture.md) maps the implementation modules and rendering responsibilities.

## Setup

Build the C++ project first using the [root README](../README.md), then run these commands from `web-nuxt/`:

```bash
npm ci
npm run dev
```

The development server serves the SPA and WebSocket endpoint together. `npm run build` produces `.output/`; `npm run preview` runs the production preview. [`simulationManager.ts`](../web-nuxt/server/utils/simulationManager.ts) searches common binary locations, including `tsvra.exe` and MSVC configuration folders. `TSVRA_BIN` can select the executable and `TSVRA_ROOT` can select its working directory. Keep `power_model.ini` available in that directory, or specify an explicit `powerConfig` path in launch settings.

The browser form and WebSocket server use [`shared/simulationConfig.ts`](../web-nuxt/shared/simulationConfig.ts) for validation. The manager passes the resulting configuration as an argument array to the executable. Native configuration parsing and validation still apply.

## Launch settings

Browser defaults come from `defaultLaunchConfig()`. They differ from native defaults: the browser uses two layers, combined failures, initial/runtime probabilities of `0.000001`, vertical/horizontal/router delays of `1/1000/15`, and a 10,000-cycle generation horizon. Both use a grid factor of 4, shared redundancy, uniform failure distribution, request probability 0.5 and seed 0 (random). Native defaults can be inspected with `./bin/tsvra --print-defaults`.

| Launch field | Native option | Meaning |
|---|---|---|
| `numLayers`, `gridFactor` | `--layers`, `--grid-factor` | Routing layers; grid width is `4 * gridFactor`. |
| `failureMode` | `--failure-mode` | `a`: initial only, `b`: runtime only, `c`: both. |
| `initialFailureRate`, `runtimeFailureRate` | `--initial-failure-rate`, `--runtime-failure-rate` | Startup and conditional per-cycle channel failure probabilities, separately. |
| `verticalDelay`, `horizontalDelay`, `routerDelay` | Corresponding `--*-delay` options | Simulated link/router delay per actual hop. |
| `cycles`, `seed` | `--cycles`, `--seed` | Generation horizon and seed; `init`/`done` record the actual seed. |
| `redundancy` | `--redundancy` | `shared`, `corner4` or `none`. |
| `requestProbability` | `--request-probability` | One global Bernoulli request trial per generation cycle. |
| `failureModel.type` | `--failure-model` | `uniform` or `hotspot`; independent of traffic weights. |
| `failureModel.hotspotCount`, `.hotspotRadius`, `.hotspotMultiplier` | `--hotspot-count`, `--hotspot-radius`, `--hotspot-multiplier` | Fixed Gaussian hotspot settings. |
| `failureModel.centers` | `--hotspot-centers` | Optional explicit centers; serialized as `x,y,z;x,y,z`. |
| `powerConfig` | `--power-config` | Optional path to the active physical coefficient file. |
| `lambda1`, `lambda2`, `maxPathLength`, `maxHorizontalDistance`, `reliabilityMin` | Corresponding native options | Optional routing weights and limits supported by the launch API. |

The server accepts 2–10 layers and grid factors up to 16. Shared redundancy requires a factor of at least 3; other layouts permit 1. Counts must be integers and numeric values finite. Probabilities are in `[0,1]`. Manual center coordinates satisfy `0 <= x,y < gridSize` and `0 <= z < numLayers - 1`; if supplied, their count must match `hotspotCount`. Native CLI ranges can be broader than browser ranges. The shared validator is the full source of browser limits.

## Native JSON-line protocol

[`src/simulator.cpp`](../src/simulator.cpp) emits one JSON object per stdout line. Diagnostics go to stderr. Stream mode uses the same generation/drain loop as the CLI, with cycles indexed from 0. The configured horizon ends request generation; cycle messages can continue through drain until all requests terminate. JSON mode does not write the normal batch CSV/configuration exports.

### Initialization

One `init` message precedes all cycles:

| Field | Contents |
|---|---|
| `config` | `gridSize`, `numLayers`, `gridFactor`, `failureMode`, legacy `failureRate`, split failure rates, three delays, request probability, actual seed, `totalCycles`, redundancy metadata and failure-model settings with actual centers. |
| `grid` | Flattened `{x,y,z,redundant,failed}` routing-node array covering all layers. |
| `hotspotWeights` | Traffic region weights as a row/column matrix. |
| `initialFailures` | Failed vertical channel coordinates `{x,y,z}`. |
| `failureHotspots` | Actual failure hotspot centers. |
| `failureProbabilities` | Channel records `{x,y,z,initial,runtime}` for the lower interfaces only. |
| `powerModel` | Technology label, voltage, frequency, flit bits, toggle probability, calibration status and parameter source. |
| `powerParameters` | Exact physical coefficient/provenance object used by this run. |

Failures identify a channel by its lower-layer `z`: it connects that layer to `z + 1`. Top-layer nodes in `grid` are not channels. The browser marks both endpoints for display without doubling native failure counts. Traffic weights and fault hotspots are separate data.

### Per-cycle messages

A `cycle` message has `cycle`, `updateVisuals`, `events`, `stats` and `keyEvent`. `updateVisuals` is true every 10 cycles, starting at cycle 0; the store applies every message regardless of this rendering hint.

| `events` array | Record fields |
|---|---|
| `newRequests` | `id`, `sx`, `sy`, `sz`, `ex`, `ey`, `ez`, `time`, `path` |
| `completed` | `id`, `time`, `latency` |
| `failed` | `id`, `time`, `reason` |
| `newFailures` | `x`, `y`, `z`, `cycle` |
| `reroutes` | `requestId`, `oldPath`, `newPath`, `redundantUsed: {x,y,z}` |

Paths are arrays of `[x,y,z]` triples. A new request's path can be empty if routing fails. Running `stats` contains `pending`, `transmitting`, `totalRequests`, `completed`, `failed`, `failedTSVs`, `avgLatency`, `redundantUsages` and `power`. `keyEvent` is true for a channel failure, request failure or reroute; the browser's auto speed delays the next cycle to highlight these events.

These are illustrative event records using the actual field names:

```json
{
  "newRequests": [{"id": 30, "sx": 3, "sy": 3, "sz": 0, "ex": 3, "ey": 3, "ez": 1, "time": 500, "path": [[3,3,0],[3,3,1]]}],
  "completed": [{"id": 29, "time": 500, "latency": 16}],
  "failed": [{"id": 99, "time": 500, "reason": "no_route"}],
  "newFailures": [{"x": 7, "y": 2, "z": 0, "cycle": 500}],
  "reroutes": [{"requestId": 45, "oldPath": [[0,1,0],[0,1,1]], "newPath": [[0,1,0],[1,1,0],[1,1,1],[0,1,1]], "redundantUsed": {"x": 1, "y": 1, "z": 0}}]
}
```

### Completion and power

One `done` message contains `summary`, including request totals, success rate, latency extrema/mean, redundancy/failure counters, bypass diagnostic counters, `actualSeed`, `inFlightAtHorizon`, `inFlightAtEnd`, `terminalDeliveryRate`, `routeFailureRate` and `power`. Rates are percentages. `successRate` divides completed by all generated requests; terminal delivery rate divides completed by completed plus failed. A graceful stop can leave requests in flight, so these rates can differ.

Both `stats.power` and `summary.power` use these fields:

| Fields | Meaning/units |
|---|---|
| `elapsedCycles`, `elapsedSeconds` | Executed simulation time, including drain. |
| `routerEvents`, `horizontalHops`, `verticalHops`, `redundantVerticalHops` | Actual launched activity; redundant hops are a subset of vertical hops. |
| `routerStaticMw`, `routerDynamicMw`, `horizontalMw`, `verticalMw`, `totalMw` | Cumulative average component and total power, mW. |
| `intervalTotalMw` | Average total power since the native 100-cycle window boundary. |
| `routerEnergyPj`, `horizontalEnergyPj`, `verticalEnergyPj`, `totalEnergyPj` | Accumulated energy, pJ. |
| `energyPerCompletedFlitPj` | Total energy divided by completed requests, or `null` with no completions. |

The frontend derives interval component powers from energy differences between received snapshots using `elapsedSeconds`. These chart intervals can differ from the native 100-cycle window. See [power-model.md](power-model.md) for physical assumptions and calibration.

### Stdin controls

JSON mode starts paused while an interactive controller keeps stdin open. A dedicated reader thread queues input characters; the simulation consumes them between cycles. Closing stdin permits free running after queued commands/step credits are consumed, for ordinary batch consumers.

| Character | Action |
|---|---|
| `p` | Pause and clear unused credits. |
| `n` | Execute one cycle then pause; repeated characters grant a batch. |
| `r` | Run freely until another control or completion. |
| `s` | Graceful stop and emit `done` with partial statistics/energy. |

For a small noninteractive run from the repository root, this example drains stdout/stderr and explicitly requests free running:

```python
import json
import subprocess

run = subprocess.run(
    ["./bin/tsvra", "--json-stream", "--cycles", "40", "--seed", "42"],
    input="r", text=True, capture_output=True, check=True,
)
messages = [json.loads(line) for line in run.stdout.splitlines()]
print(messages[-1]["summary"]["power"])
```

Long-running integrations should consume stdout and stderr continuously. Keep stdin open for interactive credits, or close it/send `r` for free running. Leaving an open stdin without credits after `init` leaves the process paused.

## WebSocket protocol

[`server/api/_ws.ts`](../web-nuxt/server/api/_ws.ts) serves `/api/_ws`; each connection owns its own simulator process.

| Server → browser message | Meaning |
|---|---|
| `{type:"status", binaryAvailable, binaryPath}` | Binary lookup result on connection; the form disables Start when unavailable. |
| `init`, `cycle`, `done` | Native messages forwarded without dropping cycles during pause. |
| `{type:"error", code, message}` | Validation/launch errors: `invalid_message`, `invalid_config`, `invalid_ack`, `binary_not_found` or `spawn_failed`. |
| `{type:"stderr", line}` | Native diagnostics; despite its name, `line` may contain a stderr chunk with multiple lines. |
| `{type:"process_exit", code}` | Process exit status; normally follows `done`. |

| Browser → server command | Action |
|---|---|
| `{type:"start", config:{...}}` | Validate settings, replace any existing process for this peer, launch JSON mode and send `p`. Missing settings receive browser defaults. |
| `{type:"ack", count:N}` | Grant 1–100 cycle credits; omitted `count` means 1. Ignored when server-paused or finished. |
| `{type:"pause"}` | Mark server paused and send `p`. |
| `{type:"resume"}` | Permit future credits; browser supplies the next ack rather than invoking native free running. |
| `{type:"speed"}` | Send `p` to clear outstanding native credits. |
| `{type:"stop"}` | Send `s`; the browser enters `stopping` until the partial `done` message arrives. |

A valid minimal launch is `{"type":"start","config":{"cycles":40,"seed":42}}`; the server fills all other launch fields from browser defaults. A direct WebSocket client must then send ack credits after receiving `init`. Native stdin commands are not themselves WebSocket messages.

### Playback credits and lifecycle

[`useWebSocket.ts`](../web-nuxt/composables/useWebSocket.ts) sends the first batch after `init`, including one initial cycle in step mode. Subsequent automatic batches are sent after the current batch is processed:

| Speed | Cycles per ack | Delay before next ack |
|---|---|---|
| Step | 1 | Manual Next Step after the initial cycle. |
| Slow | 1 | 50 ms. |
| Medium | 1 | 10 ms. |
| High | 100 | None. |
| Auto | 1 | None; 500 ms after `keyEvent: true`. |

Pause clears scheduled acks and native credits. Resume supplies the current mode's batch except in step mode. Next Step sends one ack and sends `resume` first if server-paused. Changing speed clears old credits and starts the new batch unless switching to step mode. A cycle already executed before pause still updates the store.

Normal completion and Stop produce a summary; Reset clears run data and restores the preview grid. Restarting or disconnecting uses process termination, which does not promise a `done` summary. The browser reconnects after a disconnected socket, but the old run is not reattached. Error/loading banners and a five-second stale-data warning appear in the main page; diagnostics are available in its collapsible stderr panel.

## Verification

From `web-nuxt/`, `npm test` checks shared configuration, store transitions and power calculations. After building the native executable and running `npm run build`, `npm run test:integration` launches the production server and checks validation, exact credits, pause/resume/speed, graceful Stop, relaunch settings and simulation-time power. Native CTest/Python integration coverage also checks streaming controls and CLI/JSON equivalence.

Frontend message interfaces live in [`types/simulation.ts`](../web-nuxt/types/simulation.ts); the emitters in `src/simulator.cpp` are authoritative for the full native payload, including exact power parameters and extra terminal counters forwarded by the server.
