# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

TSVRA (TSV Redundancy Architecture) is a 3D chip Through-Silicon Via routing simulation system written in C++20. It models multi-layer chip structures with TSV transmission, failure handling, and redundant routing mechanisms. The project is research-oriented, used for evaluating TSV redundancy strategies and routing algorithm performance.

## Build Commands

```bash
# Full build (from project root)
mkdir -p build && cd build && cmake .. && make

# Rebuild after changes (from build/)
cd build && make

# Clean rebuild
cd build && make clean && make
```

The executable is output to `bin/tsvra`. The build uses a specific GCC 16 compiler at `/usr/local/gcc-16-20251109/bin/g++` — this is hardcoded in CMakeLists.txt due to stdlib pthread bugs in other GCC 16 builds.

## Running

```bash
# Default run
./bin/tsvra

# With config file
./bin/tsvra --config config.ini

# Common flags: --layers, --grid-factor, --failure-mode <a|b|c>, --failure-rate, --cycles, --seed, --output
```

Output: two CSV files — `{prefix}_summary.csv` (aggregate stats) and `{prefix}_requests.csv` (per-request details).

## Architecture

The simulation follows a cycle-based loop orchestrated by `Simulator`:

```
main.cpp → Config → Simulator → { Grid, Router, RequestGenerator, Statistics }
```

**Core components** (each has a header in `include/` and implementation in `src/`):

- **Config** — Loads from `config.ini` or CLI args. Owns all simulation parameters.
- **Grid** — 3D vector `grid_[z][y][x]` of TSV units. Size: `(4*grid_factor)² × num_layers`. Each 4×4 region has 4 corner redundant TSVs. Handles initial and runtime failure injection.
- **Router** — A* pathfinding through the 3D grid. Heuristic is pluggable via `set_heuristic()`. Costs: `vertical_delay` for layer changes, `horizontal_delay` for same-layer moves.
- **RequestGenerator** — Hotspot-weighted random sampling of 4×4 TSV regions. Each region gets a weight in [0.5, 1.5]; requests are generated per-cycle with ~1% probability.
- **Simulator** — Per-cycle loop: generate requests → update transmitting → inject runtime failures → route pending requests. Reports progress at 5% intervals.
- **Statistics** — Collects latency/success/failure metrics and exports to CSV.

**Key data structures** (in `request.hpp`):
- `Point` — 3D coordinate (x, y, z)
- `Request` — Routing request with status (PENDING/TRANSMITTING/COMPLETED/FAILED)

**Three failure modes**: (a) initial-only, (b) runtime-only, (c) combined.

## Conventions

- Language: C++20. Uses `std::optional`, `std::unique_ptr`, lambdas, structured bindings.
- No external dependencies beyond the C++ standard library.
- No test framework currently in place.
- Documentation and comments are in Chinese.
- `-Werror` is intentionally disabled due to GCC 16.0.0 stdlib bugs (see CMakeLists.txt comments).
- `-fpermissive` is required for the same reason.
