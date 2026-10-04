# Web Visualization

Real-time web-based visualization for the TSVRA simulator. Users configure simulation parameters in a browser UI, start the simulation, and watch it unfold live with interactive 3D/2D views and real-time statistics.

## Architecture

```
Browser (Nuxt 4 SPA / Vue 3)  ←WebSocket→  Nitro Server  ←stdout JSON→  C++ Simulator
```

**Data flow:**
1. User configures params in web UI → WebSocket → Nitro server → spawns `bin/tsvra --json-stream ...`
2. C++ writes one JSON line per simulation cycle to stdout
3. Nitro server reads stdout line-by-line → forwards each JSON message via WebSocket
4. Vue app receives messages → sends ack credits to control pacing → updates Pinia store → renders all views in real-time

**Tech stack:** Nuxt 4.3 (SPA mode) | Vue 3 | Pinia | Raw Three.js (InstancedMesh) | Chart.js + vue-chartjs | Tailwind CSS 4 | Nitro WebSocket (crossws)

## Setup

```bash
# Prerequisites: Node.js 20+, C++ project built (bin/tsvra must exist)

# Install web dependencies
cd web-nuxt
npm install

# Development
npm run dev          # Starts Nuxt dev server with Nitro WebSocket: http://localhost:3000

# Production
npm run build        # Build to .output/
npm run preview      # Preview production build
```

The Nitro server handles both the SPA and WebSocket connections — no separate backend server needed.

## C++ JSON Streaming Protocol (CLI API)

This protocol is the integration point for external hardware design tools. Any tool can spawn the C++ binary and consume its JSON output.

### CLI Flags

| Flag | Description | Default |
|------|-------------|---------|
| `--json-stream` | Enable JSON line output to stdout (disables CSV) | off |
| `--layers N` | Number of chip layers | 2 |
| `--grid-factor N` | Grid factor (grid size = 4 × N) | 4 |
| `--failure-mode a\|b\|c` | Failure mode: initial/runtime/combined | c |
| `--failure-rate F` | TSV failure probability | 0.001 |
| `--vertical-delay N` | Cost for vertical (inter-layer) moves | 1 |
| `--horizontal-delay N` | Cost for horizontal (same-layer) moves | 1000 |
| `--cycles N` | Total simulation cycles | 100000 |
| `--seed N` | Random seed (0 = random) | 0 |

### Messages (stdout, one JSON object per line)

**Init** (sent once at simulation start):
```json
{
  "type": "init",
  "config": { "gridSize": 16, "numLayers": 2, "gridFactor": 4, ... },
  "grid": [{ "x": 0, "y": 0, "z": 0, "redundant": true, "failed": false }, ...],
  "hotspotWeights": [[1.2, 0.8, ...], ...],
  "initialFailures": [{ "x": 5, "y": 3, "z": 0 }, ...]
}
```

**Cycle** (sent every simulation cycle):
```json
{
  "type": "cycle",
  "cycle": 500,
  "events": {
    "newRequests": [{ "id": 30, "sx": 3, "sy": 3, "sz": 0, "ex": 3, "ey": 3, "ez": 1, "time": 500, "path": [[3,3,0],[3,3,1]] }],
    "completed": [{ "id": 30, "time": 502, "latency": 2 }],
    "failed": [{ "id": 99, "time": 500, "reason": "no_route" }],
    "newFailures": [{ "x": 7, "y": 2, "z": 1, "cycle": 500 }],
    "reroutes": [{ "requestId": 45, "oldPath": [...], "newPath": [...], "redundantUsed": { "x": 4, "y": 0, "z": 1 } }]
  },
  "stats": { "pending": 5, "transmitting": 23, "completed": 487, "failed": 0, "failedTSVs": 3, "avgLatency": 1042, "redundantUsages": 1 },
  "keyEvent": false
}
```

**Done** (sent at completion):
```json
{
  "type": "done",
  "summary": { "totalRequests": 99883, "completed": 99778, "failed": 0, "successRate": 99.89, "avgLatency": 10642, "maxLatency": 30003, "minLatency": 1 }
}
```

### stdin Commands (Lockstep Protocol)

The C++ process is started in **paused** mode. The Nitro server controls pacing by sending `n` (step) commands — one per cycle. Single-character commands are read via non-blocking poll:
- `p` — Pause simulation (clears remaining step credits)
- `r` — Resume free-running mode (unused in lockstep)
- `s` — Stop simulation (graceful, produces `done` message)
- `n` — Advance one cycle then pause (the lockstep primitive)

### keyEvent Flag

Set to `true` when the cycle contains notable events (TSV failure, request failure, reroute). Drives the "auto" speed mode — the frontend automatically inserts a 500ms delay to highlight important events.

### Usage Examples

**Simple run with JSON output:**
```bash
./bin/tsvra --json-stream --layers 2 --grid-factor 4 --cycles 10000
```

**Pipe to analysis tool:**
```bash
./bin/tsvra --json-stream --cycles 50000 | python3 analyze.py
```

**Validate output:**
```bash
./bin/tsvra --json-stream --cycles 5000 --layers 2 --grid-factor 2 2>/dev/null \
  | while IFS= read -r line; do echo "$line" | python3 -m json.tool > /dev/null && echo "OK" || echo "FAIL"; done
```

**External tool integration pattern:**
```python
import subprocess, json

proc = subprocess.Popen(
    ['./bin/tsvra', '--json-stream', '--layers', '3', '--cycles', '50000'],
    stdout=subprocess.PIPE, stdin=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
)

for line in proc.stdout:
    msg = json.loads(line)
    if msg['type'] == 'init':
        grid_size = msg['config']['gridSize']
    elif msg['type'] == 'cycle':
        print(f"Cycle {msg['cycle']}: {msg['stats']['completed']} completed")
    elif msg['type'] == 'done':
        print(f"Final success rate: {msg['summary']['successRate']}%")
        break
```

## WebSocket Protocol (Browser ↔ Nitro Server)

### Server → Client Messages

| Type | Description |
|------|-------------|
| `{ type: "status", binaryAvailable, binaryPath }` | Sent on connect: binary health check |
| `{ type: "init", ... }` | Forwarded C++ init message |
| `{ type: "cycle", ... }` | Forwarded C++ cycle message |
| `{ type: "done", ... }` | Forwarded C++ done message |
| `{ type: "error", code, message }` | Error: `binary_not_found`, `spawn_failed` |
| `{ type: "stderr", line }` | C++ stderr output (debug info) |
| `{ type: "process_exit", code }` | C++ process exited |

### Client → Server Commands

| Command | Action |
|---------|--------|
| `{ type: "start", config: {...} }` | Spawn C++ process with config; C++ is paused immediately (lockstep) |
| `{ type: "ack", count: N }` | Grant N step credits — server sends N `n` commands to C++ stdin |
| `{ type: "pause" }` | Set server paused state; send `p` to C++ stdin |
| `{ type: "resume" }` | Clear server paused state; browser sends ack to kick-start |
| `{ type: "stop" }` | Kill C++ process |
| `{ type: "speed" }` | Flush old C++ step credits (sends `p` to stdin); browser follows with new ack |

### Speed Control (Lockstep Ack Protocol)

The browser controls simulation pacing via a **lockstep ack protocol**. C++ is always paused; the browser sends `ack` messages granting step credits. Two parameters per mode:

| Mode | Batch (ack count) | Delay between acks |
|------|-------------------|-------------------|
| Step | 1 (manual only) | ∞ (wait for user click) |
| Slow | 1 | 50ms |
| Medium | 1 | 10ms |
| High | 100 | 0ms (immediate) |
| Auto | 1 | 0ms; 500ms on `keyEvent: true` |

When switching speed, the browser sends `{ type: "speed" }` (which tells the server to send `p` to flush C++ credits), then a new ack with the target mode's batch size.

## Nitro Server

### Files

| File | Purpose |
|------|---------|
| `web-nuxt/server/api/_ws.ts` | WebSocket handler: message routing, lockstep ack→stdin translation |
| `web-nuxt/server/utils/simulationManager.ts` | Spawns C++ process, reads JSON stdout, exposes command methods |

## Frontend (Vue 3 / Nuxt 4)

### Project Structure

```
web-nuxt/
├── pages/index.vue                     # Main layout with error banners and view routing
├── types/simulation.ts                 # TypeScript types matching C++ JSON format
├── stores/simulation.ts                # Pinia store: accumulated state from WebSocket batches
├── composables/useWebSocket.ts         # WebSocket client composable
├── components/
│   ├── layout/
│   │   ├── TheHeader.vue               # Title + connection indicator + status badge
│   │   ├── ViewSwitcher.vue            # Tab buttons: Dashboard | 3D Grid | Heatmap | Failure | Quad
│   │   └── ControlPanel.vue            # Config form + controls + stats
│   ├── config/SimConfigForm.vue        # Simulation parameter form
│   ├── dashboard/DashboardView.vue     # KPI cards + latency histogram + throughput chart
│   ├── grid3d/Grid3DView.vue           # 3D chip model (raw Three.js InstancedMesh)
│   ├── heatmap/HeatmapView.vue         # 2D traffic density heatmap
│   ├── failure/FailureView.vue         # Failure analysis (map + timeline)
│   ├── quad/QuadLayout.vue             # 2×2 grid showing all 4 views
│   └── common/                         # ColorScale, LoadingSpinner
├── server/
│   ├── api/_ws.ts                      # Nitro WebSocket handler
│   └── utils/simulationManager.ts      # C++ process manager
└── nuxt.config.ts                      # Nuxt config (SPA mode, WebSocket enabled)
```

### Views

**Dashboard** — KPI cards (Total Requests, Success Rate, Avg Latency, Failed TSVs), latency histogram, throughput chart, completed/failed pie chart.

**3D Grid** — Raw Three.js canvas with OrbitControls. Each chip layer as InstancedMesh. Three color modes: Type (normal=blue, redundant=gold), Traffic (viridis heatmap), Status (healthy=green, failed=red). Active request paths rendered as 3D lines. 4×4 region boundary wireframes.

**Heatmap** — Per-layer traffic density with smooth vertex-color interpolation. Top layer shows request density to the edges (wire interconnects); other layers show TSV transmission traffic. Region zone boundaries overlaid. Falls back to hotspot weights when no traffic data.

**Failure Analysis** — 3D failure map highlighting failed TSVs, cumulative failure timeline chart.

**Quad** — 2×2 grid showing Dashboard, 3D Grid, Heatmap, and Failure views simultaneously.

### State Management (Pinia)

The store accumulates data from WebSocket cycle messages:
- 3D grid array `tsvGrid[z][y][x]` updated with failures (in-place mutation + `triggerRef` for performance)
- Active requests tracked in a Map, removed on completion/failure
- Latency samples (capped at 10000) for histogram
- Throughput timeline and failure timeline for charts
- Running stats updated each cycle
- Error state from server (binary not found, spawn failures)
- Stderr log from C++ process

### Error Handling

- **Binary health check**: Server sends `status` message on WebSocket connect; UI disables Start if binary unavailable
- **Error banner**: Red banner with error code and message; dismiss button
- **Loading indicator**: Spinner shown between Start click and first data
- **Stale data warning**: "Waiting for data..." if no cycle received for >5 seconds while running
- **Stderr panel**: Collapsible debug panel showing C++ stderr output

### Simulation Lifecycle

1. **Idle** — Config form visible, user sets parameters
2. **Running** — Stats display, pause/resume/stop/step controls visible
3. **Paused** — Simulation paused, resume or step available
4. **Done** — Final summary displayed, reset button to return to idle
