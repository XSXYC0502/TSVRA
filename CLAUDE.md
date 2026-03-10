# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

TSVRA (TSV Redundancy Architecture) is a 3D chip Through-Silicon Via routing simulation system. It has two parts: a C++20 cycle-based simulator and a Nuxt 4 web frontend for real-time visualization. This worktree (`visual-web`, branch `feature/nuxt4-migration`) contains the web visualization work.

## Build & Run

```bash
# C++ simulator (from project root)
mkdir -p build && cd build && cmake .. && make
# Binary: bin/tsvra

# Web frontend (from web-nuxt/)
cd web-nuxt && npm install
npm run dev          # Dev server at http://localhost:3000
npm run build        # Production build to .output/
npm run preview      # Preview production build
```

The C++ build uses GCC 16 at `/usr/local/gcc-16-20251109/bin/g++` (hardcoded in CMakeLists.txt). `-fpermissive` and no `-Werror` are required due to GCC 16 stdlib bugs.

The web frontend requires `bin/tsvra` to exist — it spawns the binary as a child process. Build C++ first.

## Architecture: C++ ↔ Web Integration

This is the most important architectural boundary in the project:

```
Browser (Vue 3 SPA)  ←WebSocket→  Nitro Server  ←stdio→  C++ Process (bin/tsvra)
```

**How it works:**

1. User clicks Start in browser → WebSocket sends `{ type: "start", config: {...} }` to Nitro server
2. `simulationManager.ts` spawns `bin/tsvra --json-stream --layers N --grid-factor N ...` as a child process
3. C++ writes one JSON object per line to **stdout** — one `init` message, then one `cycle` message per simulation cycle, then one `done` message
4. Nitro reads stdout line-by-line via `readline`, parses JSON, forwards to browser via WebSocket
5. Browser controls (pause/resume/stop/step) send WebSocket commands → Nitro writes single chars to C++ **stdin** (`p`/`r`/`s`/`n`)
6. C++ polls stdin with non-blocking `poll()` between cycles

**Key integration files:**

| File | Role |
|------|------|
| `src/simulator.cpp` | C++ side: `emit_init_json()`, `emit_cycle_json()`, `emit_done_json()`, `poll_stdin_commands()` |
| `include/simulator.hpp` | JSON event structs: `JsonNewRequest`, `JsonCompleted`, `JsonFailed`, `JsonNewFailure`, `JsonReroute` |
| `web-nuxt/server/utils/simulationManager.ts` | Spawns C++ process, reads stdout JSON, sends stdin commands |
| `web-nuxt/server/api/_ws.ts` | WebSocket handler: routes client commands, controls message forwarding speed |
| `web-nuxt/composables/useWebSocket.ts` | Browser-side WebSocket client |
| `web-nuxt/stores/simulation.ts` | Pinia store: accumulates state from WebSocket messages |
| `web-nuxt/types/simulation.ts` | TypeScript types matching the C++ JSON format |

**Speed control (lockstep ack protocol):** C++ is immediately paused after spawn. The browser controls pacing by sending `{ type: "ack", count: N }` messages — the server translates each ack into N `n` (step) commands to C++ stdin. Speed modes (step/slow/medium/high/auto) differ in batch size and delay between acks. Switching speed sends `{ type: "speed" }` which flushes old C++ step credits via `p` command, then a new ack kicks off the new rate. In "auto" mode, the browser inserts a 500ms delay when `keyEvent: true`.

## C++ JSON Streaming Protocol

When run with `--json-stream`, the C++ binary communicates via stdio:

**stdout (C++ → server):** One JSON object per line:
- `{ "type": "init", "config": {...}, "grid": [...], "hotspotWeights": [...], "initialFailures": [...] }` — once at start
- `{ "type": "cycle", "cycle": N, "events": { "newRequests": [...], "completed": [...], "failed": [...], "newFailures": [...], "reroutes": [...] }, "stats": {...}, "keyEvent": bool }` — every cycle
- `{ "type": "done", "summary": {...} }` — at completion

**stdin (server → C++):** Single characters, non-blocking polled:
- `p` pause, `r` resume, `s` stop, `n` step (advance one cycle then pause)

**stderr:** Human-readable Chinese debug output (forwarded to browser as `{ type: "stderr", line }`)

## C++ Simulation Core

Cycle-based loop: `main.cpp → Config → Simulator → { Grid, Router, RequestGenerator, Statistics }`

- **Grid** — 3D array `grid_[z][y][x]` of TSV units. Size: `(4*grid_factor)² × num_layers`. Each 4×4 region has 4 corner redundant TSVs.
- **Router** — A* pathfinding. Costs: `vertical_delay` (layer changes), `horizontal_delay` (same-layer). Heuristic pluggable via `set_heuristic()`.
- **RequestGenerator** — Hotspot-weighted random sampling. ~1% probability per cycle per region.
- **Simulator** — Per-cycle: generate requests → update transmitting → inject runtime failures → route pending. In `--json-stream` mode, also accumulates events into `cycle_*` vectors and emits JSON.
- **Three failure modes:** (a) initial-only, (b) runtime-only, (c) combined.

## Web Frontend

**Tech stack:** Nuxt 4.3 (SPA mode, `ssr: false`) | Vue 3 | Pinia | Raw Three.js (InstancedMesh) | Chart.js + vue-chartjs | Tailwind CSS 4

**Views:** Dashboard (KPI cards + charts), 3D Grid (Three.js), Heatmap (per-layer traffic density — top layer = edge request density, other layers = TSV traffic), Failure Analysis (failure map + timeline), Quad (2×2 of all views).

**Pinia store** (`stores/simulation.ts`): Accumulates all state from WebSocket. Uses `shallowRef` + `triggerRef` for large data (3D grid array, active requests Map, cumulative traffic) to avoid deep reactivity overhead.

**Binary health check:** On WebSocket connect, server checks if `bin/tsvra` exists and reports to client. UI disables Start if binary unavailable.

## Conventions

- C++20. No external deps beyond stdlib. Comments in Chinese.
- Web: TypeScript, Vue 3 Composition API, Pinia stores.
- No test framework for either C++ or web.
- `nuxt.config.ts` has `experimental.websocket: true` in both Nuxt and Nitro config — required for the WebSocket handler.
