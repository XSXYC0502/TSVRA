# Problem Setting

## 1. Background

In 3D integrated circuits (3D-ICs), multiple silicon dies are vertically stacked and interconnected through **Through-Silicon Vias (TSVs)**. TSVs are vertical electrical connections that pass through the silicon substrate, enabling high-bandwidth, low-latency communication between layers.

However, TSVs are susceptible to manufacturing defects and runtime degradation (e.g., electromigration, thermal stress, void formation). A failed TSV disrupts the signal path between layers. To ensure reliability, **redundant TSVs** are deployed as spare units that can substitute for failed ones.

TSVRA (**TSV Redundancy Architecture**) is a simulation system that models this 3D-IC environment to evaluate redundancy strategies and routing algorithms under various failure scenarios.

## 2. Physical Model

### 2.1 3D-IC Structure

The chip is modeled as a **3D grid of TSV nodes** organized into layers:

- **Grid dimensions**: N × N × L
  - N = 4 × `grid_factor` (default: grid_factor = 4, so N = 16)
  - L = `num_layers` (default: 4)
- **Coordinate system**: (x, y, z) where x, y ∈ [0, N-1] and z ∈ [0, L-1]
- **Total TSV count**: N² × L (default: 1024)

Each node in the grid represents a **per-layer TSV unit** at position (x, y, z). A TSV is a vertical connection between two adjacent layers — it does **not** span the entire chip from top to bottom. Failing a TSV at (x, y, z) only disrupts the connection at that specific layer; the same (x, y) position on other layers remains unaffected.

### 2.2 TSV Types

There are two types of TSVs:

| Type | Role | Placement |
|------|------|-----------|
| **Normal TSV** | Carries regular signal traffic between layers | All non-redundant positions |
| **Redundant TSV** | Spare unit, activated only when bypassing a failed TSV | At (1+3i, 1+3j) positions in `shared` layout; four corners per 4×4 region in `corner4` layout |

> **Implementation note**: The current router does not enforce reserve-only semantics for redundant TSVs. Redundant positions can be used as ordinary route nodes. This means spares may carry normal traffic in addition to serving as bypass targets.

### 2.3 Redundancy Grouping (Overlapping 4×4 Blocks with Shared Spare)

The grid uses **overlapping 4×4 blocks** with stride 3. Each block contains 16 TSV positions, and adjacent blocks share one row/column of overlap:

- Block (i, j) covers x ∈ [3i, 3i+3], y ∈ [3j, 3j+3]
- Adjacent blocks overlap by 1 column or 1 row: block (i, j) and block (i+1, j) share column x = 3i+3
- For a 16×16 grid: 5×5 = 25 blocks per layer (stride 3, indices 0..4)

> **Grid size constraint**: The stride-3 block scheme fully tiles the grid when N ≡ 1 (mod 3), which holds for the default N=16 (grid_factor=4) and N=4 (grid_factor=1). For other grid sizes, edge TSVs beyond the last complete block are assigned to the nearest block.

Each block has **1 redundant TSV** at position **(1+3i, 1+3j)** — this spare belongs exclusively to block (i, j):

- Redundant positions in a 16×16 grid: x, y ∈ {1, 4, 7, 10, 13}
- Total redundant TSVs per layer: 25
- Redundancy ratio: 25/256 ≈ **9.77%**

A failed normal TSV at an overlap boundary (e.g., position (3, 3)) belongs to up to 4 blocks and can therefore access up to 4 candidate spares. A failed TSV in the interior of a single block has exactly 1 candidate spare. This coverage asymmetry is the key property of the overlapping block model.

**Redundancy strategies** (selectable via `--redundancy`):

| Strategy | Redundant Positions | Spares/Layer (16×16) | Ratio | Search |
|----------|-------------------|---------------------|-------|--------|
| `shared` (default) | (1+3i, 1+3j) | 25 | 9.77% | Coverage-limited |
| `corner4` (legacy) | (mod4 ∈ {0,3}) × (mod4 ∈ {0,3}) | 64 | 25% | Global same-layer scan |
| `none` | None | 0 | 0% | No bypass |

> **Note**: The `corner4` layout preserves the original simulator behavior for backward compatibility. The `shared` layout implements the literature-correct block-group redundancy scheme (IEEE 2025).

### 2.4 Signal Flow Direction

All signal requests route from a source position (on any layer) **upward to the top layer** (z = L-1). This models the physical scenario where I/O pads or bonding interfaces reside on the top die layer, and signals from lower layers must reach them.

## 3. Movement and Delay Model

### 3.1 Movement Rules

From any TSV node, signals can move in **6 directions**:

- **4 horizontal moves** (within same layer): ±x, ±y
- **2 vertical moves** (between layers): ±z

### 3.2 Delay Model

The two movement types have fundamentally different physical characteristics:

| Movement | Delay | Physical Meaning |
|----------|-------|------------------|
| **Vertical** (layer change via TSV) | `vertical_delay` (default: 5) | TSV signal propagation delay — accounts for copper pillar, insulation, and coupling effects (IEICE 2015) |
| **Horizontal** (same-layer routing) | `horizontal_delay` (default: 500) | Intra-layer metal interconnect routing — signal processing and routing decisions. V:H ratio ≈ 100:1 (IEICE 2015) |

The 100:1 ratio reflects the physical reality that a TSV is a short vertical conductor, while horizontal routing involves metal traces, buffers, and switching logic.

### 3.3 TSV Occupancy

TSVs have finite transmission capacity. The simulator tracks occupancy via `available_at` timestamps. When a signal uses a TSV, that TSV becomes unavailable until the transmission completes. Subsequent signals must wait if they attempt to use an occupied TSV.

> **Implementation note**: The current model applies occupancy to all hops (both vertical TSV moves and horizontal routing). This creates contention-based queueing effects.

## 4. Failure Model

### 4.1 Two Concepts of Failure

The system distinguishes two types of failure:

1. **Hardware failure** — A TSV physically breaks or degrades, becoming permanently unusable. This is modeled by the `failure_rate` parameter applied to individual TSVs.

2. **Signal delivery failure** — A request cannot complete its transmission to the top layer (e.g., no viable path exists due to too many hardware failures). The request is marked as FAILED.

### 4.2 Hardware Failure Modes

Three failure modes control when TSVs can physically fail:

| Mode | Name | Description |
|------|------|-------------|
| **a** | Initial-only | TSVs fail only at simulation startup (manufacturing defects) |
| **b** | Runtime-only | TSVs fail dynamically during simulation cycles (wear-out, degradation) |
| **c** | Combined | Both initial and runtime failures |

**Failure distribution**: Each TSV independently fails with probability `failure_rate` (Bernoulli distribution). In runtime modes (b, c), this check is performed every simulation cycle for each non-failed TSV.

### 4.3 Failure Handling — Redundancy Bypass

When the routing algorithm encounters a failed TSV on a vertical move:

1. Determine which block(s) the failed TSV belongs to
2. Search for the **nearest available redundant TSV** among the spare(s) of those block(s)
3. Compute a bypass path: current position → redundant TSV → next intended hop
4. If no redundant TSV is available, the route must find an alternative path or the request fails

**Search semantics by layout**:
- `shared`: Search only the spare(s) of block(s) covering the failed coordinate (coverage-limited). A failed TSV at an overlap boundary may access spares from up to 4 blocks.
- `corner4`: Global same-layer scan of all redundant TSVs (legacy behavior, not coverage-limited).
- `none`: No bypass available — routing must find alternative paths entirely.

## 5. Request Model

### 5.1 Request Generation

Requests are generated stochastically each simulation cycle:

- **Generation probability**: ~10% per cycle (on average, 1 request per 10 cycles)
- **Source**: Random position on any layer, weighted by hotspot distribution
- **Destination**: Random position on the **top layer** (z = L-1), weighted by hotspot distribution

### 5.2 Hotspot Model (Workload Locality)

The grid is divided into (N/4) × (N/4) regions. Each region is assigned a **hotspot weight** ∈ [0.5, 1.5] (uniform random), representing workload locality — some chip areas (e.g., active cores, memory controllers) generate more traffic than others.

Source and destination positions are sampled via **weighted random selection** of regions, then uniform random selection within the chosen region.

### 5.3 Request Lifecycle

Each request progresses through the following states:

```
PENDING → TRANSMITTING → COMPLETED
                      ↘ FAILED
```

1. **PENDING**: Request created, waiting to be routed
2. **TRANSMITTING**: Path computed by A*, signal traversing hop-by-hop each cycle
3. **COMPLETED**: Signal reached the destination on the top layer
4. **FAILED**: No viable path found, or rerouting failed after mid-transmission TSV failure

### 5.4 Dynamic Rerouting

If a TSV fails while a request is mid-transmission (in TRANSMITTING state), the system attempts to **reroute from the current position** to the destination. This dynamic rerouting is a core feature — it models the chip's ability to adaptively recover from runtime failures.

If rerouting fails (no viable path), the request is marked FAILED.

## 6. Routing Algorithm

The system uses the **A* pathfinding algorithm** to compute routes through the 3D grid:

- **Heuristic**: Manhattan distance |Δx| + |Δy| + |Δz| (admissible, consistent)
- **Cost function**: Dynamic cost with congestion and reliability awareness (see 6.1)
- **Constraints**: Avoids failed TSVs; uses redundant TSV bypass when needed; enforces path length and reliability bounds (see 6.2)
- **Extensibility**: The heuristic function is pluggable via `set_heuristic()`

### 6.1 Dynamic Cost Function

The cost of traversing a node at time t is:

```
c(node, t) = c0(node) + λ1 · ccong(node, t) + λ2 · φ(pfail)
```

where:
- **c0(node)**: Base delay cost (`vertical_delay` or `horizontal_delay` depending on movement direction)
- **ccong(node, t)**: Congestion cost — the number of active transmitting paths currently passing through this node
- **φ(pfail) = −ln(1 − pfail)**: Risk penalty — maps the node's failure probability to an additive cost. This transforms the multiplicative reliability product into an additive sum
- **λ1**: Congestion weight (0 = disabled)
- **λ2**: Reliability risk weight (0 = disabled)

When λ1 = 0 and λ2 = 0 (defaults), the cost reduces to the original delay-only model.

### 6.2 Path Constraints

Two pruning constraints are applied during A* expansion:

**Maximum path length (L_max)**: Paths exceeding `max_path_length` hops are pruned. When set to 0 (default), no limit is enforced.

**Maximum horizontal distance (H_max)**: Paths exceeding `max_horizontal_distance` horizontal hops are pruned. During transmission, if rerouting would push an in-flight request beyond this limit, the request is terminated and marked failed. When set to 0 (default), no limit is enforced.

**Minimum reliability (R_min)**: The path reliability is defined as:

```
R(P) = ∏(1 − pfail) ≥ R_min
```

Taking the negative logarithm, this becomes an additive constraint:

```
∑ φ(pfail) ≤ −ln(R_min)
```

Paths whose accumulated risk sum exceeds this threshold are pruned. When R_min = 0.0 (default), no reliability constraint is enforced.

### Path Cost Calculation

For a path [p₀, p₁, ..., pₖ]:

```
total_cost = Σᵢ [c0(pᵢ, pᵢ₊₁) + λ1·ccong(pᵢ₊₁, t) + λ2·φ(pfail)]
```

where `c0(a, b)` = `vertical_delay` if same (x,y), `horizontal_delay` otherwise.

**Latency** of a completed request = `complete_time - generate_time`.

## 7. Simulation Loop

The simulation runs for a configurable number of cycles (default: 100,000). Each cycle executes four steps in order:

```
for each cycle:
    1. Generate new requests     (stochastic, ~1% probability)
    2. Update transmitting       (advance in-flight signals by one hop)
    3. Apply runtime failures    (modes b, c only)
    4. Process pending requests  (compute routes via A*)
```

## 8. Metrics and Output

The simulation collects and exports:

**Summary statistics** (`{prefix}_summary.csv`):
- Total / completed / failed request counts
- Success Rate (%): completed / total_generated (completion-by-horizon metric, includes in-flight requests in denominator)
- Terminal Delivery Rate (%): completed / (completed + failed) (excludes in-flight requests)
- Route Failure Rate (%): failed / total_generated
- In Flight At Horizon: count of requests still transmitting when simulation ends
- Average Horizontal Distance: average across terminal requests
- Latency: min, max, average (for completed requests only)
- TSV failure count (total and redundant)
- Redundant TSV usages: count of bypass operations
- Failed Vertical Encounters: count of failed TSV hits during routing
- Spare Found / Spare Unavailable: bypass diagnostic counters

**Per-request details** (`{prefix}_requests.csv`):
- Source and destination coordinates
- Generation time, completion time
- Latency, horizontal distance, final status

## 9. Configuration Parameters

| Parameter | Description | Default |
|-----------|-------------|---------|
| `num_layers` | Number of chip layers (L) | 4 |
| `grid_factor` | Grid size factor (N = 4 × grid_factor) | 4 |
| `failure_mode` | Hardware failure mode: a / b / c | a |
| `failure_rate` | Per-TSV failure probability | 1e-5 |
| `vertical_delay` | Delay for vertical (inter-layer) movement | 5 |
| `horizontal_delay` | Delay for horizontal (intra-layer) movement | 500 |
| `simulation_cycles` | Total simulation cycles | 100,000 |
| `output_prefix` | Output CSV filename prefix | tsvra_output |
| `random_seed` | RNG seed (0 = random device) | 0 |
| `lambda1` | Congestion weight (λ1) | 0.0 (disabled) |
| `lambda2` | Reliability risk weight (λ2) | 0.0 (disabled) |
| `max_path_length` | Maximum path hops (L_max, 0=unlimited) | 0 |
| `max_horizontal_distance` | Maximum per-request horizontal hops (H_max, 0=unlimited) | 0 |
| `reliability_min` | Minimum path reliability (R_min, 0=no constraint) | 0.0 |
| `redundancy` | Redundancy layout: shared / corner4 / none | shared |
| `failure_model` | Failure distribution: uniform / clustered | uniform |
| `cluster_strength` | Clustering strength [0.0, 1.0] | 0.8 |
| `cluster_radius` | Clustering radius (Manhattan distance) | 4 |

### Legacy Defaults (for backward-compatibility comparison)

| Parameter | Legacy Value | Calibrated Value | Source |
|-----------|-------------|-----------------|--------|
| `num_layers` | 2 | 4 | IEICE 2012 |
| `failure_rate` | 1e-6 | 1e-5 | Electronic Measurement Technology 2025 |
| `vertical_delay` | 1 | 5 | IEICE 2015 |
| `horizontal_delay` | 1000 | 500 | IEICE 2015 |
| `simulation_cycles` | 10,000 | 100,000 | Statistical significance |
| `redundancy` | corner4 | shared | IEEE 2025 |
