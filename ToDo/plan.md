# TSVRA Real-Time Web Visualization — Implementation Plan

## Context

TSVRA is a C++20 3D chip TSV routing simulator. Currently it outputs static CSV files after simulation completes. We are building a **real-time web visualization** where the user opens a web UI, configures simulation parameters, clicks "Run", and watches the simulation unfold live with interactive 3D/2D views.

The `visual-web/` directory (branch `feature/visual-web`) currently contains only the C++ simulation code — no web code exists yet.

---

## Architecture Overview

```
Browser (React + R3F)  ←WebSocket→  Node.js Server  ←stdout JSON→  C++ Simulator
         ↑                              ↑                              ↑
   Visualization               Spawn/manage process           Modified to emit
   + Controls                  + forward messages              JSON line batches
```

**Data flow:**
1. User configures params in web UI → WebSocket → Node.js → spawns `bin/tsvra --json-stream ...`
2. C++ writes JSON lines to stdout every N cycles (batch updates)
3. Node.js reads stdout line-by-line → forwards each JSON batch via WebSocket
4. React app receives batches → updates Zustand store → renders all views in real-time

---

## Tech Stack

| Layer | Technology | Package |
|-------|-----------|---------|
| Build | Vite 6 | `vite` |
| Frontend | React 19 + TypeScript | `react`, `react-dom` |
| 3D Rendering | Three.js + React Three Fiber | `three`, `@react-three/fiber`, `@react-three/drei` |
| Charts | Recharts | `recharts` |
| State | Zustand | `zustand` |
| Styling | Tailwind CSS 4 | `tailwindcss`, `@tailwindcss/vite` |
| Backend | Express + ws | `express`, `ws` |
| CSV Parsing | PapaParse (for optional file upload) | `papaparse` |

---

## C++ Modifications

### Files to modify:
- `include/simulator.hpp` / `src/simulator.cpp` — Add JSON streaming mode
- `include/statistics.hpp` / `src/statistics.cpp` — Add real-time metrics export
- `include/config.hpp` / `src/config.cpp` — Add `--json-stream` flag and `--batch-size` parameter
- `src/main.cpp` — Wire up JSON streaming mode

### New C++ output format (JSON lines to stdout):

**1. Init message** (sent once at simulation start):
```json
{"type":"init","config":{"gridSize":16,"numLayers":2,"gridFactor":4,"failureMode":"c","failureRate":1e-9,"verticalDelay":1,"horizontalDelay":1000,"totalCycles":10000000},"grid":[{"x":0,"y":0,"z":0,"redundant":true,"failed":false},...],"hotspotWeights":[[1.2,0.8,...],...],"initialFailures":[{"x":5,"y":3,"z":0},...]}
```

**2. Batch update** (sent every N cycles):
```json
{"type":"batch","cycle":10000,"events":{"newRequests":[{"id":30,"sx":3,"sy":3,"sz":0,"ex":3,"ey":3,"ez":1,"time":2393,"path":[[3,3,0],[3,3,1]]}],"completed":[{"id":30,"time":2395,"latency":2}],"failed":[{"id":99,"time":5000,"reason":"no_route"}],"newFailures":[{"x":7,"y":2,"z":1,"cycle":9500}],"reroutes":[{"requestId":45,"oldPath":[[...]],"newPath":[[...]],"redundantUsed":{"x":4,"y":0,"z":1}}]},"stats":{"pending":5,"transmitting":23,"completed":2487,"failed":0,"failedTSVs":3,"avgLatency":10642},"keyEvent":false}
```

**3. Done message** (sent when simulation completes):
```json
{"type":"done","summary":{"totalRequests":99883,"completed":99778,"failed":0,"successRate":99.89,"avgLatency":10642,"maxLatency":30003,"minLatency":1}}
```

The `keyEvent` flag is `true` when the batch contains a notable event (TSV failure, request failure, redundant TSV bypass/reroute). This drives the "auto" speed mode.

### Pause/Resume support:
- C++ reads single-char commands from stdin: `p` = pause, `r` = resume, `s` = stop, `n` = next step
- Node.js backend writes these characters to the C++ process stdin based on WebSocket commands from browser

---

## Simulation Control Modes

| Mode | Behavior |
|------|----------|
| **Step-by-step** | Simulation paused. User clicks "Next" to advance one batch. |
| **Slow** | ~500ms delay between batches |
| **Medium** | ~100ms delay between batches |
| **High** | ~10ms delay between batches (near max speed) |
| **Auto** | Runs at high speed; automatically slows to ~500ms when `keyEvent: true` (failures, reroutes) |

Speed control is implemented in the Node.js backend by controlling how fast it signals the C++ process to continue (or in C++ via sleep between batches with stdin commands to adjust).

---

## Project Structure

```
visual-web/
├── [existing C++ code: include/, src/, CMakeLists.txt, config.ini, docs/]
│
├── web/                                # All web code lives here
│   ├── package.json
│   ├── tsconfig.json
│   ├── vite.config.ts
│   ├── index.html
│   │
│   ├── server/                         # Node.js backend
│   │   ├── index.ts                    # Express + WebSocket server entry
│   │   ├── simulationManager.ts        # Spawn/manage C++ process, parse JSON stdout
│   │   └── wsHandler.ts                # WebSocket message routing (start/pause/resume/stop/step/speed)
│   │
│   └── src/                            # React frontend
│       ├── main.tsx                    # React root
│       ├── App.tsx                     # Layout: header + view switcher + control panel
│       │
│       ├── types/
│       │   └── simulation.ts          # Point, Request, TSV, GridConfig, BatchUpdate, etc.
│       │
│       ├── store/
│       │   └── simulationStore.ts     # Zustand: accumulated state from WebSocket batches
│       │
│       ├── services/
│       │   └── websocket.ts           # WebSocket client: connect, send commands, receive batches
│       │
│       ├── components/
│       │   ├── layout/
│       │   │   ├── Header.tsx         # Title + simulation status indicator
│       │   │   ├── ViewSwitcher.tsx   # Slider/tabs: Dashboard | 3D Grid | Heatmap | Failure | Quad
│       │   │   └── ControlPanel.tsx   # Config form + Start/Pause/Resume/Stop + Speed + Step
│       │   │
│       │   ├── config/
│       │   │   └── SimConfigForm.tsx  # Form: layers, grid_factor, failure_mode, failure_rate, cycles, etc.
│       │   │
│       │   ├── dashboard/
│       │   │   ├── DashboardView.tsx  # KPI cards + charts
│       │   │   ├── KPICard.tsx
│       │   │   ├── LatencyHistogram.tsx
│       │   │   ├── ThroughputChart.tsx
│       │   │   └── StatusPieChart.tsx
│       │   │
│       │   ├── grid3d/
│       │   │   ├── Grid3DView.tsx     # R3F Canvas + OrbitControls
│       │   │   ├── ChipLayer.tsx      # InstancedMesh for one layer of TSVs
│       │   │   ├── GridFrame.tsx      # 4×4 region boundary wireframes
│       │   │   ├── RequestPathLine.tsx # 3D tube/line showing routing path
│       │   │   ├── LayerLabel.tsx     # Z-axis layer labels
│       │   │   └── GridControls.tsx   # Color mode toggle, layer visibility, legend
│       │   │
│       │   ├── heatmap/
│       │   │   ├── HeatmapView.tsx    # R3F-based 2D overhead heatmap (orbit controls)
│       │   │   ├── HeatmapGrid.tsx    # Color-mapped rectangles for traffic density
│       │   │   ├── HeatmapLegend.tsx  # Color scale bar
│       │   │   └── RegionTooltip.tsx  # Hover tooltip with region stats
│       │   │
│       │   ├── failure/
│       │   │   ├── FailureView.tsx    # Failure analysis layout
│       │   │   ├── FailureMap.tsx     # Grid view highlighting failed TSVs
│       │   │   ├── RedundancyChart.tsx # Redundancy usage bar chart
│       │   │   └── FailureTimeline.tsx # When failures occurred over time
│       │   │
│       │   ├── quad/
│       │   │   └── QuadLayout.tsx     # 2×2 grid showing 4 views simultaneously
│       │   │
│       │   └── common/
│       │       ├── ColorScale.tsx
│       │       └── LoadingSpinner.tsx
│       │
│       ├── hooks/
│       │   ├── useSimulation.ts       # Hook wrapping WebSocket + store for components
│       │   └── useColorScale.ts       # Dynamic color mapping
│       │
│       ├── utils/
│       │   ├── colorScales.ts         # Viridis, inferno color functions
│       │   ├── gridGeometry.ts        # Redundant position calc, region boundaries
│       │   └── formatters.ts          # Number/cycle formatters
│       │
│       └── styles/
│           └── globals.css            # Tailwind imports
```

---

## View Layout

- **View Switcher** (slider/tabs at the top): 5 options — Dashboard, 3D Grid, Heatmap, Failure Analysis, Quad (2×2)
- **Single view mode**: Selected view fills the main area
- **Quad mode**: All 4 views in a 2×2 grid, each independently interactive
- All views with spatial content use **OrbitControls** for mouse drag rotation/zoom/pan

---

## Key Component Details

### ControlPanel (always visible)
- **Before simulation**: Shows SimConfigForm (num_layers, grid_factor, failure_mode, failure_rate, cycles, delays) + "Start" button
- **During simulation**: Shows cycle counter, progress bar, Pause/Resume/Stop buttons, speed selector (Slow/Medium/High/Auto), "Next Step" button (active in step mode)
- **After simulation**: Shows summary + "Reset" to configure new run

### Grid3DView (3D chip model)
- R3F Canvas with OrbitControls (left-drag rotate, scroll zoom, right-drag pan)
- Each layer rendered as InstancedMesh (16×16 = 256 instances per layer)
- TSV color modes: Type (normal=blue, redundant=gold) | Traffic (viridis heatmap) | Status (healthy=green, failed=red)
- Active request paths shown as animated 3D lines/tubes
- 4×4 region boundaries as subtle wireframe grid
- Layers separated vertically with adjustable spacing

### HeatmapView (2D overhead)
- Rendered in R3F for consistent orbit controls
- Flat plane with color-mapped rectangles per grid cell
- Traffic count per cell computed from request start/end coordinates
- Region boundaries overlaid
- Tooltip on hover with per-region statistics

### DashboardView (charts)
- KPI cards: Total Requests, Success Rate, Avg Latency, Failed TSVs — update in real-time
- LatencyHistogram: Recharts BarChart, rebinned as new completions arrive
- ThroughputChart: Recharts AreaChart, requests over time (rolling window)
- StatusPieChart: Completed vs Failed ratio

### FailureView
- FailureMap: Grid view (R3F) with failed TSVs highlighted red, redundant-failed as orange
- FailureTimeline: Recharts line chart showing cumulative failures over cycles
- RedundancyChart: Bar chart of redundancy usage per region

---

## Zustand Store Design

```typescript
interface SimulationStore {
  // Connection
  wsConnected: boolean;
  simStatus: 'idle' | 'configuring' | 'running' | 'paused' | 'stepping' | 'done';

  // Config
  config: SimConfig | null;          // Set from init message

  // Grid state (accumulated)
  tsvGrid: TSVState[][][];           // [z][y][x] — updated as failures arrive
  hotspotWeights: number[][];        // From init message

  // Requests (accumulated)
  activeRequests: Map<number, LiveRequest>;  // Currently transmitting
  completedRequests: CompletedRequest[];     // Finished
  failedRequests: FailedRequest[];           // Failed

  // Real-time stats
  currentCycle: number;
  stats: RunningStats;               // Updated each batch
  latencySamples: number[];          // For histogram
  throughputTimeline: TimePoint[];   // For throughput chart
  failureTimeline: FailureEvent[];   // For failure timeline

  // UI state
  activeView: 'dashboard' | 'grid3d' | 'heatmap' | 'failure' | 'quad';
  speedMode: 'slow' | 'medium' | 'high' | 'auto' | 'step';
  colorMode: 'type' | 'traffic' | 'status';

  // Actions
  applyInitMessage(msg: InitMessage): void;
  applyBatchUpdate(msg: BatchUpdate): void;
  applyDoneMessage(msg: DoneMessage): void;
  reset(): void;
}
```

---

## Implementation Phases (Build Order)

### Phase 1: C++ JSON Streaming
**Files**: `include/simulator.hpp`, `src/simulator.cpp`, `include/config.hpp`, `src/config.cpp`, `src/main.cpp`
1. Add `--json-stream` CLI flag and `--batch-size N` parameter to Config
2. Add JSON line output to Simulator: init message at start, batch every N cycles, done at end
3. Add stdin command reading for pause/resume/stop/step (non-blocking poll)
4. Include routing paths in request data
5. Include key event flag for auto-speed mode
6. Build and verify JSON output: `./bin/tsvra --json-stream --batch-size 10000 | head -5`

### Phase 2: Node.js Backend
**Files**: `web/server/index.ts`, `web/server/simulationManager.ts`, `web/server/wsHandler.ts`
1. Express server serving static files from Vite build
2. WebSocket endpoint at `/ws`
3. SimulationManager: spawn C++ process, read stdout line-by-line, parse JSON, forward to WS clients
4. Handle WS commands: `{type: "start", config: {...}}`, `{type: "pause"}`, `{type: "resume"}`, `{type: "stop"}`, `{type: "step"}`, `{type: "speed", mode: "auto"}`
5. Speed control: manage delay between forwarding batches (or signal C++ via stdin)

### Phase 3: Frontend Foundation
**Files**: `web/src/main.tsx`, `web/src/App.tsx`, `web/src/types/simulation.ts`, `web/src/store/simulationStore.ts`, `web/src/services/websocket.ts`, layout components, `web/src/components/config/SimConfigForm.tsx`, `web/src/components/layout/ControlPanel.tsx`
1. Vite + React + TypeScript + Tailwind scaffolding
2. TypeScript type definitions matching C++ JSON format
3. Zustand store with batch accumulation logic
4. WebSocket client service
5. App shell with ViewSwitcher and ControlPanel
6. SimConfigForm with all configurable parameters
7. Basic start/pause/resume/stop/step controls

### Phase 4: 3D Grid View
**Files**: `web/src/components/grid3d/*`, `web/src/utils/gridGeometry.ts`, `web/src/utils/colorScales.ts`
1. R3F Canvas with OrbitControls
2. ChipLayer using InstancedMesh with dynamic color buffer
3. GridFrame for 4×4 region wireframes
4. RequestPathLine for active routing paths (animated)
5. Color mode switching (type / traffic / status)
6. Layer labels and visibility toggles

### Phase 5: Statistics Dashboard
**Files**: `web/src/components/dashboard/*`, `web/src/utils/formatters.ts`
1. KPI cards with real-time updates
2. Latency histogram (rebins as data arrives)
3. Throughput chart (rolling window area chart)
4. Status pie/donut chart

### Phase 6: Heatmap View
**Files**: `web/src/components/heatmap/*`
1. R3F-based 2D heatmap with orbit controls
2. Traffic density computation from accumulated requests
3. Region boundary overlay
4. Color legend and tooltip

### Phase 7: Failure Analysis View
**Files**: `web/src/components/failure/*`
1. Failure map highlighting exact failure positions
2. Failure timeline chart
3. Redundancy usage per region chart

### Phase 8: Quad Layout + Polish
**Files**: `web/src/components/quad/QuadLayout.tsx`
1. 2×2 grid layout composing all 4 views
2. View switcher slider with 5 options
3. Responsive sizing
4. Loading states and error handling

---

## Verification Plan

1. **C++ streaming**: Run `./bin/tsvra --json-stream --batch-size 1000 2>/dev/null | head -20` — verify valid JSON lines with init + batch messages
2. **Backend**: Start server, connect via `wscat -c ws://localhost:3001/ws`, send `{"type":"start","config":{"numLayers":2,"gridFactor":2,"cycles":10000}}`, verify JSON batches arrive
3. **Frontend**: Open `http://localhost:5173`, configure small simulation (2 layers, factor=2, 10K cycles), click Start, verify:
   - 3D grid renders with correct TSV count
   - Requests appear and animate through paths
   - Dashboard charts update in real-time
   - Pause/Resume/Stop work
   - Speed modes change update rate
   - Auto mode slows on key events
4. **Quad view**: Switch to Quad layout, verify all 4 views render simultaneously
5. **Full run**: Run with default config (16×16×2, 10M cycles), verify performance doesn't degrade over time with ~100K accumulated requests
