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

  // Cumulative traffic: 2D array [y][x] tracking total path usage across all cycles
  const cumulativeTraffic = shallowRef<number[][] | null>(null);

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

    // Track initial failure positions
    const positions = initialFailures.map(f => ({ x: f.x, y: f.y, z: f.z }));

    // Initialize cumulative traffic as 2D zero-array
    const trafficGrid: number[][] = [];
    for (let y = 0; y < msgConfig.gridSize; y++) {
      trafficGrid[y] = new Array(msgConfig.gridSize).fill(0);
    }
    cumulativeTraffic.value = trafficGrid;

    config.value = msgConfig;
    tsvGrid.value = newGrid;
    hotspotWeights.value = hsWeights;
    failedTSVPositions.value = positions;
    totalCycles.value = msgConfig.totalCycles;
    simStatus.value = 'running';
    activeRequests.value = new Map();
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
    const { cycle, events, stats: msgStats } = msg;
    const newActiveRequests = new Map(activeRequests.value);

    // Add new requests
    for (const req of events.newRequests) {
      newActiveRequests.set(req.id, req);
    }

    // Accumulate path coords into cumulativeTraffic
    if (cumulativeTraffic.value) {
      const traffic = cumulativeTraffic.value;
      const rows = traffic.length;
      const cols = rows > 0 ? traffic[0]!.length : 0;
      for (const req of events.newRequests) {
        if (req.path) {
          for (const [x, y] of req.path) {
            if (y >= 0 && y < rows && x >= 0 && x < cols) {
              traffic[y]![x] = (traffic[y]![x] ?? 0) + 1;
            }
          }
        }
      }
      triggerRef(cumulativeTraffic);
    }

    // Remove completed
    for (const req of events.completed) {
      newActiveRequests.delete(req.id);
    }

    // Remove failed
    for (const req of events.failed) {
      newActiveRequests.delete(req.id);
    }

    // Update grid failures — mutate in-place instead of deep-copying
    if (events.newFailures.length > 0 && tsvGrid.value) {
      for (const f of events.newFailures) {
        const cell = tsvGrid.value[f.z]?.[f.y]?.[f.x];
        if (cell) cell.failed = true;
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
    cumulativeTraffic.value = null;
    config.value = null;
    tsvGrid.value = null;
    hotspotWeights.value = null;
    activeRequests.value = new Map();
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
    cumulativeTraffic,
    config,
    tsvGrid,
    hotspotWeights,
    activeRequests,
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
    applyInitMessage,
    applyCycleUpdate,
    applyDoneMessage,
    reset,
  };
});
