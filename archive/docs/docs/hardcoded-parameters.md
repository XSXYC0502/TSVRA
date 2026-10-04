# Hardcoded Parameters

This document catalogs all hardcoded parameters in the TSVRA simulator that need to be validated against published literature. These values are currently embedded in the source code as constants or default values and should be calibrated through references to relevant papers.

## Status Legend

- **Needs reference**: No literature basis yet — must be determined from papers
- **Configurable**: Exposed via `config.ini` / CLI, but the default value needs justification
- **Structural**: Defines the model structure; changing requires code modification

---

## 1. Grid Structure

| Parameter | Value | Location | Status | Description |
|-----------|-------|----------|--------|-------------|
| Region size | 4×4 | `grid.cpp:149-152`, `request_generator.cpp:11,70` | Structural | Each TSV group is a 4×4 block. The multiplier `4` in `N = 4 × grid_factor` is hardcoded. |
| Redundant TSV positions | 4 corners of each 4×4 region | `grid.cpp:146-153` | Structural | Redundancy placement rule: `(x%4==0 \|\| x%4==3) && (y%4==0 \|\| y%4==3)` |
| Redundant TSVs per region | 4 | `grid.cpp:146-153` | Structural | Fixed at 4 corners per 4×4 block |
| Grid size formula | N = 4 × grid_factor | `config.hpp:40` | Structural | The base multiplier 4 ties grid size to region size |

## 2. Delay Model

| Parameter | Default | Location | Status | Description |
|-----------|---------|----------|--------|-------------|
| `vertical_delay` | 1 | `config.cpp:15` | Configurable, needs reference | TSV inter-layer transmission delay (cycles). Represents a near-instantaneous wire/tunnel. |
| `horizontal_delay` | 1000 | `config.cpp:16` | Configurable, needs reference | Intra-layer metal routing delay (cycles). Includes signal processing and routing logic. |
| Delay ratio (horizontal:vertical) | 1000:1 | `config.cpp:15-16` | Needs reference | The ratio between intra-layer routing cost and TSV transmission cost. Core assumption of the model. |

## 3. Failure Model

| Parameter | Default | Location | Status | Description |
|-----------|---------|----------|--------|-------------|
| `failure_rate` | 1e-3 | `config.cpp:14` | Configurable, needs reference | Per-TSV Bernoulli failure probability. Comment in code says "adjusted from 1e-6 to 1e-3 for simulation". The physically realistic value needs literature support. |
| Failure distribution | Bernoulli (per-TSV, per-cycle) | `grid.cpp:88,102` | Structural, needs reference | Each TSV fails independently with fixed probability. Is this the right distribution? (vs. Weibull, exponential, etc.) |
| Failure permanence | Permanent (irreversible) | `grid.cpp:93-94,109-110` | Structural | Once failed, a TSV never recovers. |
| Runtime failure check frequency | Every cycle | `simulator.cpp:74-77,92-93` | Structural, needs reference | Runtime failures are checked every single cycle. Should this be less frequent? |

## 4. Request Generation

| Parameter | Value | Location | Status | Description |
|-----------|-------|----------|--------|-------------|
| `request_probability` | 0.01 (1%) | `request_generator.cpp:47` | Hardcoded, needs reference | Probability of generating a new request each cycle. Not exposed in config. |
| Hotspot weight range | [0.5, 1.5] | `request_generator.cpp:25` | Hardcoded, needs reference | Each region gets weight `0.5 + Uniform(0,1)`. Range and distribution are hardcoded. |
| Hotspot distribution | Uniform | `request_generator.cpp:25` | Hardcoded, needs reference | Weights drawn from uniform distribution. Real workloads may follow different patterns (Zipf, Gaussian, etc.). |
| Destination layer | Always top layer (z = L-1) | `request_generator.cpp:98-99` | Structural | All requests route to the top layer (I/O pads). |
| Source layer | Uniform random across all layers | `request_generator.cpp:92` | Hardcoded | Source layer chosen uniformly. Real traffic may have layer-dependent generation rates. |
| In-region position sampling | Uniform within 4×4 block | `request_generator.cpp:70` | Hardcoded | Position within a region is uniformly random (offset ∈ [0,3] in both x and y). |

## 5. Routing Algorithm

| Parameter | Value | Location | Status | Description |
|-----------|-------|----------|--------|-------------|
| Heuristic function | Manhattan distance | `router.cpp:19,22-24` | Default (pluggable) | L1 norm: \|Δx\| + \|Δy\| + \|Δz\|. Admissible but may not account for the asymmetric delay ratio. |
| Redundant TSV search scope | Same layer only | `grid.cpp:70` | Structural | Bypass only considers redundant TSVs on the same z-layer as the failed TSV. |
| Redundant TSV selection | Nearest by Manhattan distance | `grid.cpp:60-84` | Hardcoded | Always picks the closest available redundant TSV. No load-balancing or look-ahead. |
| Movement directions | 6 (±x, ±y, ±z) | `router.cpp:181-203` | Structural | Full 3D grid connectivity. |
| `lambda1` default | 0.0 | `config.cpp` | Configurable, needs reference | Congestion weight. 0 disables congestion-aware routing. |
| `lambda2` default | 0.0 | `config.cpp` | Configurable, needs reference | Reliability risk weight. 0 disables risk-aware routing. |
| `max_path_length` default | 0 | `config.cpp` | Configurable | Max path hops. 0 = unlimited. |
| `reliability_min` default | 0.0 | `config.cpp` | Configurable, needs reference | Min path reliability threshold. |
| Risk penalty function | φ(p) = −ln(1−p) | `router.cpp` | Structural, needs reference | Maps failure probability to additive cost. |

## 6. Simulation Control

| Parameter | Default | Location | Status | Description |
|-----------|---------|----------|--------|-------------|
| `simulation_cycles` | 100,000 | `config.cpp:17` | Configurable | Default cycle count. `config.hpp:11` also defines a global constant `SIMULATION_CYCLES = 10^7` (unused by default constructor). |
| `num_layers` | 2 | `config.cpp:11` | Configurable, needs reference | Number of chip layers. Real 3D-ICs may have 2–8+ layers. |
| `grid_factor` | 4 (→ 16×16 grid) | `config.cpp:12` | Configurable | Grid dimension factor. |
| `random_seed` | 0 (random device) | `config.cpp:19` | Configurable | 0 means non-deterministic seeding. |
| Progress report interval | every 5% | `simulator.cpp:49` | Hardcoded | `total_cycles / 20` |

## 7. Validation Constraints

These are hardcoded in `config.cpp:117-143`:

| Constraint | Value | Notes |
|------------|-------|-------|
| Minimum layers | 2 | Cannot simulate single-layer chip |
| Minimum grid_factor | 1 | Minimum 4×4 grid |
| failure_rate range | [0.0, 1.0] | Probability bounds |
| Minimum delay | > 0 | Both vertical and horizontal |
| Minimum cycles | > 0 | Must run at least 1 cycle |

---

## Priority for Literature Review

**High priority** — these fundamentally shape the simulation results:
1. Delay ratio (horizontal:vertical) — the 1000:1 assumption
2. Failure rate — realistic per-TSV failure probability
3. Failure distribution — Bernoulli vs. more realistic reliability models
4. Request generation rate — the 0.01 probability

**Medium priority** — affect accuracy but less sensitive:
5. Hotspot weight range and distribution
6. Number of layers (default)
7. Region size (4×4 vs. other groupings)

**Lower priority** — structural choices for future work:
8. Redundant TSV placement strategy (4 corners)
9. Redundant TSV selection algorithm (nearest Manhattan)
10. Heuristic function (Manhattan distance with symmetric weighting)
