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
  - L = `num_layers` (default: 2)
- **Coordinate system**: (x, y, z) where x, y ∈ [0, N-1] and z ∈ [0, L-1]
- **Total TSV count**: N² × L (default: 512)

Each node in the grid represents a TSV unit at a specific (x, y) position on layer z.

### 2.2 TSV Types

There are two types of TSVs:

| Type | Role | Placement |
|------|------|-----------|
| **Normal TSV** | Carries regular signal traffic between layers | All non-corner positions in each 4×4 region |
| **Redundant TSV** | Spare unit, activated only when bypassing a failed TSV | Four corner positions of each 4×4 region |

Redundant TSVs do **not** carry normal traffic. They are reserved exclusively as spares.

### 2.3 Redundancy Grouping (4×4 Regions)

The grid is partitioned into non-overlapping **4×4 regions** (each containing 16 TSV positions per layer):

- Region (i, j) covers x ∈ [4i, 4i+3], y ∈ [4j, 4j+3]
- For a 16×16 grid: 4×4 = 16 regions per layer
- Each region has **4 redundant TSVs** at its corners: (4i, 4j), (4i+3, 4j), (4i, 4j+3), (4i+3, 4j+3)

Corner TSVs are **shared** between adjacent regions (e.g., position (3, 0) is a corner for both region (0,0) and adjacent regions), enabling cross-region redundancy coverage.

> **Note**: The current 4×4 corner-based redundancy is one specific strategy. The simulator is designed to evaluate and compare different redundancy placement strategies in future development.

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
| **Vertical** (layer change via TSV) | `vertical_delay` (default: 1) | TSV acts as a direct wire/tunnel — signal passes through with minimal delay |
| **Horizontal** (same-layer routing) | `horizontal_delay` (default: 1000) | Intra-layer metal interconnect routing, including signal processing and routing decisions — orders of magnitude slower |

The 1000:1 ratio reflects the physical reality that a TSV is essentially a short vertical conductor, while horizontal routing involves metal traces, buffers, and switching logic.

### 3.3 TSV Sharing

TSVs transmit signals extremely fast relative to the overall transmission time. Multiple signals can share the same TSV simultaneously without side effects. TSV contention is **not** a bottleneck in this model.

> **Implementation note**: The code tracks `available_at` timestamps for occupancy, but physically, TSV sharing is assumed to be conflict-free.

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

When the routing algorithm encounters a failed TSV:

1. Search for the **nearest available redundant TSV** on the **same layer** (by Manhattan distance)
2. Compute a bypass path: current position → redundant TSV → next intended hop
3. If no redundant TSV is available on that layer, the route must find an alternative path or the request fails

## 5. Request Model

### 5.1 Request Generation

Requests are generated stochastically each simulation cycle:

- **Generation probability**: ~1% per cycle (on average, 1 request per 100 cycles)
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

The simulation runs for a configurable number of cycles (default: 10,000,000). Each cycle executes four steps in order:

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
- Success rate
- Latency: min, max, average
- TSV failure count

**Per-request details** (`{prefix}_requests.csv`):
- Source and destination coordinates
- Generation time, completion time
- Latency, final status

## 9. Configuration Parameters

| Parameter | Description | Default |
|-----------|-------------|---------|
| `num_layers` | Number of chip layers (L) | 2 |
| `grid_factor` | Grid size factor (N = 4 × grid_factor) | 4 |
| `failure_mode` | Hardware failure mode: a / b / c | a |
| `failure_rate` | Per-TSV failure probability | 1e-6 |
| `vertical_delay` | Delay for vertical (inter-layer) movement | 1 |
| `horizontal_delay` | Delay for horizontal (intra-layer) movement | 1000 |
| `simulation_cycles` | Total simulation cycles | 100,000 |
| `output_prefix` | Output CSV filename prefix | tsvra_output |
| `random_seed` | RNG seed (0 = random device) | 0 |
| `lambda1` | Congestion weight (λ1) | 0.0 (disabled) |
| `lambda2` | Reliability risk weight (λ2) | 0.0 (disabled) |
| `max_path_length` | Maximum path hops (L_max, 0=unlimited) | 0 |
| `reliability_min` | Minimum path reliability (R_min, 0=no constraint) | 0.0 |
