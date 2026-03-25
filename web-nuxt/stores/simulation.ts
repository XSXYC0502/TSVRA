import { defineStore } from 'pinia';
import { ref, shallowRef, triggerRef } from 'vue';
import type {
  SimConfig, TSVState, LiveRequest, CompletedRequest, FailedRequest,
  RunningStats, FailureEvent, TimePoint, InitMessage, CycleUpdate,
  DoneMessage, SimStatus, ViewMode, SpeedMode, ColorMode
} from '~/types/simulation';

export const useSimulationStore = defineStore('simulation', () => {
  // Connection
  const wsConnected = ref(false);
  const simStatus = ref<SimStatus>('idle');

  // Binary status (from server health check)
  const binaryAvailable = ref<boolean | null>(null);
  const binaryPath = ref<string | null>(null);

  // Error state
  const error = ref<{ code: string; message: string } | null>(null);

  // Stderr log
  const stderrLines = ref<string[]>([]);

  // Config
  const config = ref<SimConfig | null>(null);

  // Grid state - use shallowRef for large 3D array
  const tsvGrid = shallowRef<TSVState[][][] | null>(null);
  const hotspotWeights = ref<number[][] | null>(null);

  // Requests - use shallowRef for Map
  const activeRequests = shallowRef<Map<number, LiveRequest>>(new Map());
  const completedCount = ref(0);
  const failedCount = ref(0);
  const recentCompleted = ref<CompletedRequest[]>([]);
  const recentFailed = ref<FailedRequest[]>([]);

  // Stats
  const currentCycle = ref(0);
  const totalCycles = ref(0);
  const stats = ref<RunningStats | null>(null);
  const latencySamples = ref<number[]>([]);
  const throughputTimeline = ref<TimePoint[]>([]);
  const failureTimeline = ref<FailureEvent[]>([]);
  const failedTSVPositions = ref<{ x: number; y: number; z: number }[]>([]);

  // Summary (when done)
  const summary = ref<DoneMessage['summary'] | null>(null);

  // UI
  const activeView = ref<ViewMode>('dashboard');
  const speedMode = ref<SpeedMode>('high');
  const colorMode = ref<ColorMode>('type');

  // Stale data tracking
  const lastCycleTime = ref<number>(0);

  // Reactivity trigger: incremented on every cycle update
  const cycleVersion = ref(0);

  // Last cycle update message (for checking updateVisuals flag)
  const lastCycleUpdate = ref<CycleUpdate | null>(null);

  // Cumulative traffic: 3D array [z][y][x] tracking total path usage per layer
  const cumulativeTraffic = shallowRef<number[][][] | null>(null);

  // Reroute display paths: persist for 1000 cycles after request completes/fails
  const rerouteDisplayPaths = shallowRef<Map<number, { path: [number, number, number][]; expireCycle: number }>>(new Map());

  // Heatmap update interval (in cycles)
  const heatmapUpdateInterval = ref(10);

  // Initialize preview grid from form defaults (before any simulation runs)
  function initializePreview(gridFactor = 4, numLayers = 2, redundancyLayout: 'shared' | 'none' = 'shared') {
    const gridSize = 4 * gridFactor;
    const previewConfig: SimConfig = {
      gridSize,
      numLayers,
      gridFactor,
      failureMode: 'c',
      failureRate: 0.000001,
      verticalDelay: 1,
      horizontalDelay: 1000,
      totalCycles: 10000,
      redundancy: { layout: redundancyLayout, sparesPerLayer: 0, redundancyRatio: 0 },
    };

    const newGrid: TSVState[][][] = [];
    for (let z = 0; z < numLayers; z++) {
      newGrid[z] = [];
      for (let y = 0; y < gridSize; y++) {
        newGrid[z][y] = [];
        for (let x = 0; x < gridSize; x++) {
          let redundant = false;
          if (redundancyLayout === 'shared' && gridFactor > 1) {
            const stride = gridFactor - 1;
            redundant = x >= 1 && y >= 1 && (x - 1) % stride === 0 && (y - 1) % stride === 0;
          }
          newGrid[z][y][x] = { x, y, z, redundant, failed: false, usageCount: 0 };
        }
      }
    }

    const trafficGrid: number[][][] = [];
    for (let z = 0; z < numLayers; z++) {
      trafficGrid[z] = [];
      for (let y = 0; y < gridSize; y++) {
        trafficGrid[z][y] = new Array(gridSize).fill(0);
      }
    }

    config.value = previewConfig;
    tsvGrid.value = newGrid;
    cumulativeTraffic.value = trafficGrid;
    hotspotWeights.value = null;
  }

  // Actions
  function setWsConnected(connected: boolean) {
    wsConnected.value = connected;
  }

  function setSimStatus(status: SimStatus) {
    simStatus.value = status;
  }

  function setBinaryStatus(available: boolean, path: string | null) {
    binaryAvailable.value = available;
    binaryPath.value = path;
  }

  function setError(err: { code: string; message: string }) {
    error.value = err;
    simStatus.value = 'idle';
  }

  function clearError() {
    error.value = null;
  }

  function addStderrLine(line: string) {
    stderrLines.value = [...stderrLines.value.slice(-200), line];
  }

  function setActiveView(view: ViewMode) {
    activeView.value = view;
  }

  function setSpeedMode(mode: SpeedMode) {
    speedMode.value = mode;
  }

  function setColorMode(mode: ColorMode) {
    colorMode.value = mode;
  }

  function setHeatmapUpdateInterval(n: number) {
    heatmapUpdateInterval.value = Math.max(1, Math.round(n));
  }

  function applyInitMessage(msg: InitMessage) {
    error.value = null;
    lastCycleTime.value = Date.now();
    const { config: msgConfig, grid, hotspotWeights: hsWeights, initialFailures } = msg;

    // Build 3D grid array
    const newGrid: TSVState[][][] = [];
    for (let z = 0; z < msgConfig.numLayers; z++) {
      newGrid[z] = [];
      for (let y = 0; y < msgConfig.gridSize; y++) {
        newGrid[z][y] = [];
        for (let x = 0; x < msgConfig.gridSize; x++) {
          newGrid[z][y][x] = { x, y, z, redundant: false, failed: false, usageCount: 0 };
        }
      }
    }

    // Apply grid data
    for (const tsv of grid) {
      if (newGrid[tsv.z]?.[tsv.y]?.[tsv.x]) {
        newGrid[tsv.z]![tsv.y]![tsv.x]!.redundant = tsv.redundant;
        newGrid[tsv.z]![tsv.y]![tsv.x]!.failed = tsv.failed;
      }
    }

    // TSV connects layer z to z+1 — propagate failure to upper layer for visualization
    for (let z = 0; z < msgConfig.numLayers - 1; z++) {
      for (let y = 0; y < msgConfig.gridSize; y++) {
        for (let x = 0; x < msgConfig.gridSize; x++) {
          if (newGrid[z]![y]![x]!.failed) {
            newGrid[z + 1]![y]![x]!.failed = true;
          }
        }
      }
    }

    // Track initial failure positions
    const positions = initialFailures.map(f => ({ x: f.x, y: f.y, z: f.z }));

    // Initialize cumulative traffic as 3D zero-array [z][y][x]
    const trafficGrid: number[][][] = [];
    for (let z = 0; z < msgConfig.numLayers; z++) {
      trafficGrid[z] = [];
      for (let y = 0; y < msgConfig.gridSize; y++) {
        trafficGrid[z][y] = new Array(msgConfig.gridSize).fill(0);
      }
    }
    cumulativeTraffic.value = trafficGrid;

    config.value = msgConfig;
    tsvGrid.value = newGrid;
    hotspotWeights.value = hsWeights;
    failedTSVPositions.value = positions;
    totalCycles.value = msgConfig.totalCycles;
    simStatus.value = speedMode.value === 'step' ? 'stepping' : 'running';
    activeRequests.value = new Map();
    rerouteDisplayPaths.value = new Map();
    completedCount.value = 0;
    failedCount.value = 0;
    currentCycle.value = 0;
    cycleVersion.value = 0;
    latencySamples.value = [];
    throughputTimeline.value = [];
    failureTimeline.value = [];
    summary.value = null;
    recentCompleted.value = [];
    recentFailed.value = [];
    stats.value = null;
  }

  function applyCycleUpdate(msg: CycleUpdate) {
    lastCycleTime.value = Date.now();
    lastCycleUpdate.value = msg;
    const { cycle, events, stats: msgStats } = msg;
    const newActiveRequests = new Map(activeRequests.value);

    // Add new requests
    for (const req of events.newRequests) {
      newActiveRequests.set(req.id, req);
    }

    // Accumulate path coords into cumulativeTraffic (per-layer)
    if (cumulativeTraffic.value) {
      const traffic = cumulativeTraffic.value;
      const layers = traffic.length;
      const rows = layers > 0 ? traffic[0]!.length : 0;
      const cols = rows > 0 ? traffic[0]![0]!.length : 0;
      for (const req of events.newRequests) {
        if (req.path) {
          for (const [x, y, z] of req.path) {
            if (z >= 0 && z < layers && y >= 0 && y < rows && x >= 0 && x < cols) {
              traffic[z]![y]![x] = (traffic[z]![y]![x] ?? 0) + 1;
            }
          }
        }
      }
      triggerRef(cumulativeTraffic);
    }

    // Mark rerouted requests and update their paths (BEFORE removal so reroute+complete in same cycle works)
    for (const rr of events.reroutes) {
      const req = newActiveRequests.get(rr.requestId);
      if (req) {
        req.rerouted = true;
        req.path = rr.newPath;
      }
    }

    // Manage reroute display paths (persist 1000 cycles after completion/failure)
    const newReroutePaths = new Map(rerouteDisplayPaths.value);
    // Add new reroutes (expireCycle = Infinity while request is still active)
    for (const rr of events.reroutes) {
      newReroutePaths.set(rr.requestId, { path: rr.newPath, expireCycle: Infinity });
    }
    // Set expiry for completed/failed rerouted requests
    for (const req of events.completed) {
      if (newReroutePaths.has(req.id)) {
        newReroutePaths.get(req.id)!.expireCycle = cycle + 1000;
      }
    }
    for (const req of events.failed) {
      if (newReroutePaths.has(req.id)) {
        newReroutePaths.get(req.id)!.expireCycle = cycle + 1000;
      }
    }
    // Prune expired entries
    for (const [id, entry] of newReroutePaths) {
      if (cycle > entry.expireCycle) {
        newReroutePaths.delete(id);
      }
    }
    rerouteDisplayPaths.value = newReroutePaths;

    // Remove completed
    for (const req of events.completed) {
      newActiveRequests.delete(req.id);
    }

    // Remove failed
    for (const req of events.failed) {
      newActiveRequests.delete(req.id);
    }

    // Update grid failures — mutate in-place instead of deep-copying
    // TSV connects layer z to z+1, so mark both layers as failed
    if (events.newFailures.length > 0 && tsvGrid.value) {
      for (const f of events.newFailures) {
        const cell = tsvGrid.value[f.z]?.[f.y]?.[f.x];
        if (cell) cell.failed = true;
        const cellAbove = tsvGrid.value[f.z + 1]?.[f.y]?.[f.x];
        if (cellAbove) cellAbove.failed = true;
      }
      triggerRef(tsvGrid);
    }

    // Latency samples
    const newSamples = [...latencySamples.value, ...events.completed.map(r => r.latency)];
    latencySamples.value = newSamples.length > 10000 ? newSamples.slice(-10000) : newSamples;

    // Throughput timeline
    throughputTimeline.value = [...throughputTimeline.value, { cycle, value: msgStats.completed }];

    // Failure timeline
    failureTimeline.value = [...failureTimeline.value, ...events.newFailures];

    // Failed positions
    failedTSVPositions.value = [...failedTSVPositions.value, ...events.newFailures.map(f => ({ x: f.x, y: f.y, z: f.z }))];

    currentCycle.value = cycle;
    activeRequests.value = newActiveRequests;
    completedCount.value = msgStats.completed;
    failedCount.value = msgStats.failed;
    recentCompleted.value = events.completed;
    recentFailed.value = events.failed;
    stats.value = msgStats;
    cycleVersion.value++;
  }

  function applyDoneMessage(msg: DoneMessage) {
    simStatus.value = 'done';
    summary.value = msg.summary;
  }

  function reset() {
    wsConnected.value = wsConnected.value; // preserve connection state
    simStatus.value = 'idle';
    error.value = null;
    stderrLines.value = [];
    lastCycleTime.value = 0;
    cycleVersion.value = 0;
    lastCycleUpdate.value = null;
    initializePreview();
    activeRequests.value = new Map();
    rerouteDisplayPaths.value = new Map();
    completedCount.value = 0;
    failedCount.value = 0;
    recentCompleted.value = [];
    recentFailed.value = [];
    currentCycle.value = 0;
    totalCycles.value = 0;
    stats.value = null;
    latencySamples.value = [];
    throughputTimeline.value = [];
    failureTimeline.value = [];
    failedTSVPositions.value = [];
    summary.value = null;
    activeView.value = 'dashboard';
    speedMode.value = 'high';
    colorMode.value = 'type';
  }

  // Initialize preview grid on store creation
  initializePreview();

  return {
    // State
    wsConnected,
    simStatus,
    binaryAvailable,
    binaryPath,
    error,
    stderrLines,
    lastCycleTime,
    cycleVersion,
    lastCycleUpdate,
    cumulativeTraffic,
    config,
    tsvGrid,
    hotspotWeights,
    activeRequests,
    rerouteDisplayPaths,
    completedCount,
    failedCount,
    recentCompleted,
    recentFailed,
    currentCycle,
    totalCycles,
    stats,
    latencySamples,
    throughputTimeline,
    failureTimeline,
    failedTSVPositions,
    summary,
    activeView,
    speedMode,
    colorMode,
    heatmapUpdateInterval,

    // Actions
    setWsConnected,
    setSimStatus,
    setBinaryStatus,
    setError,
    clearError,
    addStderrLine,
    setActiveView,
    setSpeedMode,
    setColorMode,
    setHeatmapUpdateInterval,
    applyInitMessage,
    applyCycleUpdate,
    applyDoneMessage,
    reset,
    initializePreview,
  };
});
