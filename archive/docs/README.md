# TSVRA - TSV Routing Algorithm Simulator

## Overview

TSVRA (TSV Routing Algorithm) is a 3D chip TSV (Through-Silicon Via) routing simulation system. It models TSV transmission, failure handling, and redundant routing mechanisms in a multi-layer chip structure.

## Key Features

- **Multi-layer TSV grid model**: Supports configurable multi-layer TSV grids (default: 2 layers)
- **Redundant TSV mechanism**: The four corners of each 4x4 region are treated as redundant TSVs
- **Three failure modes**:
  - Mode a: initial failures only
  - Mode b: runtime dynamic failures only
  - Mode c: mixed failures (initial + runtime)
- **A* routing algorithm**: Supports an extensible heuristic interface
- **Hotspot simulation**: Weighted request generation to model realistic access patterns
- **Detailed statistics output**: Command-line summaries plus CSV export

## Requirements

- C++20兼容编译器（GCC 10+, Clang 11+, 或 Visual Studio 2022 / MSVC）
- CMake 3.20+

## Build

```bash
# 配置
cmake -S . -B build

# 编译（Linux / macOS / Windows 通用）
cmake --build build --config Release

# Executables will be generated under bin/
```

## Usage
Windows + MSVC:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
```

If you prefer not to use presets, the equivalent configure command is:

```powershell
cmake -S . -B build/windows-msvc -G "Visual Studio 17 2022" -A x64
```

If CMake reports `No CMAKE_CXX_COMPILER could be found` at `CMakeLists.txt:3`, that means MSVC was not discoverable from the current shell. Using the preset above avoids relying on an already-initialized compiler environment.

## 使用方法

### Basic Usage

```bash
./bin/tsvra
```

### Using a Config File

### Command-Line Arguments
Windows PowerShell / CMD:

```powershell
.\bin\tsvra.exe --config config.ini
```

### 命令行参数

```bash
./bin/tsvra [options]

Options:
  --config <file>           Load configuration from a file
  --layers <n>              Number of layers (default: 2)
  --grid-factor <n>         Grid factor (default: 4, grid size = 4*n)
  --failure-mode <a|b|c>    Failure mode (default: a)
  --failure-rate <rate>     Failure rate (default: 1e-6)
  --vertical-delay <n>      Vertical delay (default: 1)
  --horizontal-delay <n>    Horizontal delay (default: 1000)
  --max-horizontal-distance <n> Max horizontal hops H_max (default: 0 = unlimited)
  --output <prefix>         Output file prefix (default: tsvra_output)
  --seed <n>                Random seed (default: 0 = random)
  -h, --help                Show help
```

### Examples

```bash
# Use a 16x16 grid (grid-factor=4)
./bin/tsvra --grid-factor 4

# Runtime failure mode
./bin/tsvra --failure-mode b --failure-rate 1e-5

# Custom output prefix
./bin/tsvra --output experiment1

# Fixed random seed for reproducible results
./bin/tsvra --seed 12345
```

## Configuration Parameters

| Parameter | Description | Default |
|------|------|--------|
| `num_layers` | Number of chip layers | 2 |
| `grid_factor` | Grid factor (grid size = 4 × grid_factor) | 4 (16x16) |
| `failure_mode` | Failure mode (a/b/c) | a |
| `failure_rate` | TSV failure probability | 1e-6 |
| `vertical_delay` | Vertical transmission delay | 1 |
| `horizontal_delay` | Horizontal transmission delay | 1000 |
| `max_horizontal_distance` | Maximum per-request horizontal hops (0 = unlimited) | 0 |
| `simulation_cycles` | Number of simulation cycles | 100000 |
| `output_prefix` | Output file prefix | tsvra_output |
| `random_seed` | Random seed (0 = random) | 0 |

## Output Files

The simulator generates two CSV files:

1. **`{prefix}_summary.csv`**: Summary statistics
   - Total, completed, and failed request counts
   - Success rate
   - Average horizontal distance across terminal requests
   - Average, maximum, and minimum latency
   - TSV failure statistics

2. **`{prefix}_requests.csv`**: Detailed request records
   - Start and destination coordinates for each request
   - Generation time, completion time, latency, and horizontal distance
   - Final status (completed/failed)

## Project Structure

```
TSVRA/
├── CMakeLists.txt          # CMake build configuration
├── README.md               # This file
├── config.ini              # Example configuration file
├── include/                # Header files
│   ├── config.hpp          # Configuration management
│   ├── tsv.hpp             # TSV unit
│   ├── grid.hpp            # TSV grid
│   ├── router.hpp          # A* routing algorithm
│   ├── request.hpp         # Request definition
│   ├── request_generator.hpp # Request generator
│   ├── simulator.hpp       # Simulation engine
│   └── statistics.hpp      # Statistics output
└── src/                    # Source files
    ├── config.cpp
    ├── tsv.cpp
    ├── grid.cpp
    ├── router.cpp
    ├── request_generator.cpp
    ├── simulator.cpp
    ├── statistics.cpp
    └── main.cpp            # Program entry point
```

## Core Algorithms

### TSV Grid Layout

- Grid size: N × N × L (N = 4 × grid_factor, L = num_layers)
- Redundant TSV positions: the four corners of each 4x4 region
- Coordinate system: (x, y, z), where z is the layer index

### A* Routing

- **Heuristic**: Manhattan distance (extensible)
- **Movement rules**:
  - Horizontal movement (same layer): delay = horizontal_delay
  - Vertical movement (inter-layer TSV): delay = vertical_delay
- **Routing strategy**:
  - Avoid failed TSVs
  - Consider TSV occupancy time
  - Prefer the shortest available path

### Failure Model

- **Mode a (initial failures)**: Random TSV failures at simulation startup
- **Mode b (runtime failures)**: Dynamic failures introduced during simulation
- **Mode c (mixed failures)**: Combination of a and b

### Request Generation

- **Hotspot regions**: 4x4 TSV blocks are the base unit, each with a random heat value
- **Generation strategy**: Weighted random sampling based on heat values
- **Request frequency**: Roughly one request every 100 cycles on average (adjustable)

## Compiler Warnings

The project uses strict compiler options to keep code quality high:

- `-Wall -Wextra -Wpedantic`: enable standard warnings
- `-Werror`: treat warnings as errors
- Additional warning flags are listed in `CMakeLists.txt`

## Extensibility

### Custom Heuristics

```cpp
// Set a custom heuristic in Router
router.set_heuristic([](const Point& a, const Point& b) {
    // Custom distance calculation
    return custom_distance(a, b);
});
```

### Adjusting the Request Generation Rate

Modify `request_probability` in `request_generator.cpp`:

```cpp
double request_probability = 0.01; // 1% probability per cycle
```

## License

[Add your license information here]

## Authors

[Add author information here]

## Acknowledgments

This project is used for research and performance evaluation of 3D-chip TSV routing algorithms.
