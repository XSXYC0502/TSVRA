import { defineStore } from 'pinia';
import { ref, shallowRef } from 'vue';
import type {
  SimConfig, TSVState, LiveRequest, CompletedRequest, FailedRequest,
  RunningStats, FailureEvent, TimePoint, InitMessage, BatchUpdate,
  DoneMessage, SimStatus, ViewMode, SpeedMode, ColorMode
} from '~/types/simulation';

export const useSimulationStore = defineStore('simulation', () => {
  // Connection
  const wsConnected = ref(false);
  const simStatus = ref<SimStatus>('idle');

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

  // Actions
  function setWsConnected(connected: boolean) {
    wsConnected.value = connected;
  }

  function setSimStatus(status: SimStatus) {
    simStatus.value = status;
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
        newGrid[tsv.z][tsv.y][tsv.x].redundant = tsv.redundant;
        newGrid[tsv.z][tsv.y][tsv.x].failed = tsv.failed;
      }
    }

    // Track initial failure positions
    const positions = initialFailures.map(f => ({ x: f.x, y: f.y, z: f.z }));

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
    latencySamples.value = [];
    throughputTimeline.value = [];
    failureTimeline.value = [];
    summary.value = null;
    recentCompleted.value = [];
    recentFailed.value = [];
    stats.value = null;
  }

  function applyBatchUpdate(msg: BatchUpdate) {
    const { cycle, events, stats: msgStats } = msg;
    const newActiveRequests = new Map(activeRequests.value);

    // Add new requests
    for (const req of events.newRequests) {
      newActiveRequests.set(req.id, req);
    }

    // Remove completed
    for (const req of events.completed) {
      newActiveRequests.delete(req.id);
    }

    // Remove failed
    for (const req of events.failed) {
      newActiveRequests.delete(req.id);
    }

    // Update grid failures
    let newGrid = tsvGrid.value;
    if (events.newFailures.length > 0 && newGrid) {
      newGrid = newGrid.map(layer => layer.map(row => row.map(tsv => ({ ...tsv }))));
      for (const f of events.newFailures) {
        if (newGrid[f.z]?.[f.y]?.[f.x]) {
          newGrid[f.z][f.y][f.x].failed = true;
        }
      }
      tsvGrid.value = newGrid;
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
  }

  function applyDoneMessage(msg: DoneMessage) {
    simStatus.value = 'done';
    summary.value = msg.summary;
  }

  function reset() {
    wsConnected.value = wsConnected.value; // preserve connection state
    simStatus.value = 'idle';
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
    setActiveView,
    setSpeedMode,
    setColorMode,
    applyInitMessage,
    applyBatchUpdate,
    applyDoneMessage,
    reset,
  };
});
