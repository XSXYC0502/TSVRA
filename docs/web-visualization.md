# Web visualization and streaming

Browser → WebSocket → Nitro → `bin/tsvra --json-stream`. Nitro forwards JSON lines and controls pacing over stdin. Launch validation/argument serialization are shared by form and server; protocol types are in `web-nuxt/types/simulation.ts`.

Build C++, then run `npm ci` and `npm run dev` inside `web-nuxt`. `npm run build` produces the production app. All four views share simulation state.

## Native protocol

Stdout is JSON only; diagnostics go to stderr. JSON starts paused while an interactive controller keeps stdin open. Closing stdin permits free running for batch consumers.

| Message | Contents |
|---|---|
| `init` | `config`, grid, initial failures, traffic `hotspotWeights`, `failureHotspots`, `failureProbabilities`, `powerModel`, exact `powerParameters` |
| `cycle` | Cycle index, request/completion/failure/reroute events, running statistics and `stats.power` |
| `done` | Summary, actual seed, horizon/end in-flight counts and `summary.power` |

Fault maps use lower-layer interface z; top routing nodes are not channels. `failureProbabilities` contains `{x,y,z,initial,runtime}`. Traffic weights and fault hotspots are separate.

Power includes `elapsedCycles`, `elapsedSeconds`, actual router/link counters, component powers in mW, `totalMw`, `intervalTotalMw`, component energies and `totalEnergyPj`. `energyPerCompletedFlitPj` is null if no request completes. Cumulative mean and window power use simulation time, including drain; playback time never enters the calculation.

| Stdin | Action |
|---|---|
| `p` | Pause and clear credits |
| `n` | One cycle then pause; repeated characters grant a batch |
| `r` | Run freely |
| `s` | Graceful stop and `done` with partial energy/accounting |

Noninteractive example:

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

## Browser controls

The browser sends `start`, `pause`, `resume`, `stop`, `speed` and bounded `ack` credits. Speed changes clear previous credits. Already executed cycles still update state while paused. Chart history resets per run and is bounded.

The form keeps vertical/horizontal defaults 1/1000, adds router delay 15, exposes shared/corner4/none redundancy and separates failure timing from uniform/hotspot distribution. Optional manual centers are `x,y,z` triples. The server validates the configuration before spawning.
