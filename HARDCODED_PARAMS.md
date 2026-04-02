# TSVRA Hardcoded Parameters

A comprehensive inventory of hardcoded constants, magic numbers, and default values across the C++ simulator and Nuxt web frontend.

---

## C++ Simulator

### Default Configuration (`src/config.cpp`)

| Line | Parameter | Default | CLI Flag | Description |
|------|-----------|---------|----------|-------------|
| 11 | `num_layers_` | `2` | `--layers` | Number of chip layers |
| 12 | `grid_factor_` | `4` | `--grid-factor` | Grid multiplier (grid = 4 * factor) |
| 14 | `failure_rate_` | `1e-6` | `--failure-rate` | TSV failure probability |
| 15 | `vertical_delay_` | `1` | `--vertical-delay` | Layer-to-layer transmission delay |
| 16 | `horizontal_delay_` | `1000` | `--horizontal-delay` | Same-layer transmission delay |
| 17 | `simulation_cycles_` | `10000` | `--cycles` | Default simulation cycle count |
| 20 | `lambda1_` | `0.0` | `--lambda1` | Congestion weight (disabled) |
| 21 | `lambda2_` | `0.0` | `--lambda2` | Risk weight (disabled) |
| 23 | `reliability_min_` | `0.0` | `--reliability-min` | Min path reliability (unconstrained) |

### Global Constant (`include/config.hpp`)

| Line | Name | Value | Description |
|------|------|-------|-------------|
| 11 | `SIMULATION_CYCLES` | `std::pow(10, 7)` (10M) | Hard limit on simulation duration |

### Grid Architecture (`src/grid.cpp`)

| Line | Value | Description |
|------|-------|-------------|
| 150-153 | `x % 4`, `y % 4` == 0 or 3 | Redundant TSVs at corners of each 4x4 region |
| 91, 106 | `num_layers_ - 1` | Top layer has no TSVs (cannot fail) |

### Request Generation (`src/request_generator.cpp`)

| Line | Value | Description |
|------|-------|-------------|
| 25 | `0.5 + uniform(0,1)` | Hotspot weights range [0.5, 1.5] |
| 47 | `0.1` | Request probability per cycle (~10%) |
| 70 | `(0, 3)` | Random offset within 4x4 region |
| 99 | `num_layers_ - 1` | Requests always target top layer |

### Router / A* (`src/router.cpp`)

| Line | Value | Description |
|------|-------|-------------|
| 44 | `-log(1 - failure_rate)` or `1e18` | Single-node risk penalty (clamped) |
| 49 | `-log(reliability_min)` or `0.0` | Max cumulative risk threshold |
| 50 | `numeric_limits<double>::max()` | Infinite risk when constraint disabled |
| 146 | `hop_count + 2` | Redundant TSV bypass = 2 hops |
| 152 | `risk + 2.0 * phi_fail` | 2-node risk for redundant path |
| 236 | `6` neighbors reserved | 4 horizontal + 2 vertical |
| 239-241 | `dx={-1,1,0,0}`, `dy={0,0,-1,1}` | 4-directional movement |

### Simulator Loop (`src/simulator.cpp`)

| Line | Value | Description |
|------|-------|-------------|
| 194 | `cycle % 10 == 0` | Visual update every 10 cycles |
| 361 | `1000` us (1 ms) | Pause sleep to avoid busy loop |
| 392 | `total_cycles / 20` | Progress report at 5% intervals |

### Statistics (`src/statistics.cpp`)

| Line | Value | Description |
|------|-------|-------------|
| 17 | `uint64_t::max()` | Sentinel for uninitialized min latency |

---

## Web Frontend

### Speed Control / Lockstep Protocol (`composables/useWebSocket.ts`)

| Line | Constant | Value | Description |
|------|----------|-------|-------------|
| 12-18 | `ACK_BATCH` | high: 100, medium: 1, slow: 1, auto: 1, step: 1 | Cycles per ack batch |
| 20-26 | `ACK_DELAY` | high: 0ms, medium: 10ms, slow: 50ms, auto: 0ms, step: Infinity | Delay between acks |
| 48 | keyEvent delay | 500 ms | Auto-mode pause on key events |
| 64 | Reconnect timeout | 2000 ms | WebSocket reconnection delay |
| 96 | Log threshold | `cycle % 100` | Console log frequency |

### Simulation Defaults (`stores/simulation.ts`)

| Line | Parameter | Value | Description |
|------|-----------|-------|-------------|
| 12 | `numLayers` | `2` | Preview grid layers |
| 13 | `gridFactor` | `4` | Grid scaling factor |
| 14 | `failureMode` | `'c'` | Combined failure mode |
| 15 | `failureRate` | `0.000001` | Failure probability |
| 16 | `verticalDelay` | `1` | Inter-layer delay |
| 17 | `horizontalDelay` | `1000` | Intra-layer delay |
| 18 | `totalCycles` | `10000` | Simulation duration |
| 68 | `heatmapUpdateInterval` | `10` | Heatmap refresh period (cycles) |

### Data Limits (`stores/simulation.ts`)

| Line | Value | Description |
|------|-------|-------------|
| 135 | `200` | Max stderr history lines |
| 285 | `10000` | Max latency samples kept |

### Config Form Bounds (`components/config/SimConfigForm.vue`)

| Line | Parameter | Min | Max |
|------|-----------|-----|-----|
| 23 | `numLayers` | 2 | 10 |
| 24 | `gridFactor` | 1 | 16 |

### 3D Grid View (`components/grid3d/Grid3DView.vue`)

| Line | Value | Description |
|------|-------|-------------|
| 17 | `3` | Layer spacing (vertical distance) |
| 64 | `50` | Max request paths displayed |
| 95 | `size * 1.2` | Camera distance |
| 99 | `50` | Camera FOV (degrees) |
| 99 | `0.1 / 1000` | Camera near/far clipping |
| 110 | `0.1` | OrbitControls damping factor |
| 115 | `0.8` | Ambient light intensity |
| 117 | `1.0` | Primary directional light |
| 120 | `0.4` | Secondary directional light |
| 139 | `3` | Grid line width (px) |
| 203 | `-0.15` | Grid frame Y offset |
| 234 | `0.8, 0.3, 0.8` | TSV mesh scale (x, y, z) |
| 265 | `0.8` | Request path opacity |

### Heatmap View (`components/heatmap/HeatmapView.vue`)

| Line | Value | Description |
|------|-------|-------------|
| 16 | `3` | Layer spacing |
| 125 | `size * 1.2` | Camera distance |
| 131 | `50` / `0.1` / `1000` | Camera FOV / near / far |
| 142 | `0.1` | OrbitControls damping |
| 149 | `size - 1` | Heatmap mesh segments |
| 152 | `3` | Grid line width (px) |

### Failure Analysis (`components/failure/FailureView.vue`)

| Line | Value | Description |
|------|-------|-------------|
| 31 | `3` | Layer spacing |
| 62 | `200` | Timeline downsample threshold |
| 129 | `20` | Top failure regions shown |
| 214 | `0.8` / `1.0` / `0.4` | Ambient / primary / secondary light |
| 320 | `0.85, 0.3, 0.85` | TSV mesh scale |
| 325 | `1.0, 0.15, 0.15` | Failed TSV color (red) |
| 327 | `0.27, 0.53, 1.0` | Redundant TSV color (blue) |
| 329 | `0.15, 0.7, 0.3` | Normal TSV color (green) |

### Dashboard Charts (`components/dashboard/DashboardView.vue`)

| Line | Value | Description |
|------|-------|-------------|
| 56 | `20` bins | Latency histogram bin count |
| 97 | `100` points | Throughput chart display limit |
| 108 | `0` | Point radius (hidden) |
| 109 | `2` | Line border width |
| 110 | `0.3` | Bezier curve tension |

### Chart Colors (`components/dashboard/DashboardView.vue`)

| Value | Description |
|-------|-------------|
| `#1f2937` | Tooltip background |
| `#374151` | Tooltip border / grid lines |
| `#9ca3af` | Tick text color |
| `#3b82f6` | Bar chart blue |
| `#10b981` | Throughput green / completed |
| `#ef4444` | Failed (red) |

### TSV Color Scale (`composables/useColorScale.ts`)

| Line | RGB | Description |
|------|-----|-------------|
| 8 | `1, 0.2, 0.2` | Failed TSV (red) |
| 12 | `1, 0.84, 0` | Redundant TSV (gold) |
| 12 | `0.27, 0.53, 1` | Normal TSV (blue) |
| 18 | `0.27, 1, 0.27` | Healthy TSV (green) |

### Color Scales (`utils/colorScales.ts`)

| Line | Name | Stops | Description |
|------|------|-------|-------------|
| 2-14 | `VIRIDIS_STOPS` | 11 RGB tuples | Viridis perceptual colormap |
| 33-44 | `INFERNO_STOPS` | 10 RGB tuples | Inferno perceptual colormap |

### Grid Geometry (`utils/gridGeometry.ts`)

| Line | Value | Description |
|------|-------|-------------|
| 2-4 | `x % 4`, check 0 or 3 | Redundant TSV at 4x4 region corners |
| 8 | `Math.floor(x/4)` | Region index from position |
| 12 | `rx*4` to `rx*4+3` | 4-unit region bounds |

### Number Formatting (`utils/formatters.ts`)

| Line | Value | Description |
|------|-------|-------------|
| 2 | `1,000,000` | Threshold for 'M' suffix |
| 3 | `1,000` | Threshold for 'K' suffix |
| 12 | 2 decimal places | Percentage precision |
| 16 | 1 decimal place | Latency precision |

### Timeouts & Intervals (`pages/index.vue`)

| Line | Value | Description |
|------|-------|-------------|
| 26 | `5000` ms | Stale data warning threshold |
| 39 | `1000` ms | Stale check interval |

### Server-Side (`server/api/_ws.ts`)

| Line | Value | Description |
|------|-------|-------------|
| 104 | `cycle % 1000` | Server-side cycle logging frequency |

### UI Layout (Tailwind classes)

| File | Value | Description |
|------|-------|-------------|
| `ControlPanel.vue` | `w-72` (288px) | Sidebar width |
| `DashboardView.vue` | `grid-cols-4` | KPI card columns |
| `QuadLayout.vue` | `gap-1` | Quad view gap |
| `HeatmapView.vue` | `max-w-56` (224px) | Legend max width |

---

## Summary by Category

### Simulation Defaults (mirrored in C++ and web)

| Parameter | Value |
|-----------|-------|
| Layers | 2 |
| Grid factor | 4 (grid = 16x16) |
| Failure rate | 1e-6 |
| Vertical delay | 1 cycle |
| Horizontal delay | 1000 cycles |
| Simulation cycles | 10,000 |
| Failure mode | Combined ('c') |

### Grid Architecture

| Constant | Value |
|----------|-------|
| Region size | 4x4 TSV units |
| Redundant positions | 4 corners per region |
| Request target | Always top layer |
| Request probability | 10% per cycle |
| Hotspot weights | [0.5, 1.5] |

### Visualization

| Constant | Value |
|----------|-------|
| Layer spacing | 3 units |
| Camera FOV | 50 degrees |
| Camera near/far | 0.1 / 1000 |
| Damping | 0.1 |
| Lighting (A/P/S) | 0.8 / 1.0 / 0.4 |
| TSV box scale | ~0.8 x 0.3 x 0.8 |

### Performance Caps

| Limit | Value |
|-------|-------|
| Max request paths drawn | 50 |
| Stderr history | 200 lines |
| Latency samples | 10,000 |
| Throughput chart points | 100 |
| Timeline downsample | 200 points |
| Top failure regions | 20 |
| Histogram bins | 20 |
