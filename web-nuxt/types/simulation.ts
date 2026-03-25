// 3D coordinate
export interface Point {
  x: number;
  y: number;
  z: number;
}

// Configuration
export interface SimConfig {
  gridSize: number;
  numLayers: number;
  gridFactor: number;
  failureMode: 'a' | 'b' | 'c';
  failureRate: number;
  verticalDelay: number;
  horizontalDelay: number;
  totalCycles: number;
  redundancy?: { layout: 'shared' | 'corner4' | 'none'; sparesPerLayer: number; redundancyRatio: number };
  failureModel?: { type: 'uniform' | 'clustered'; clusterStrength: number; clusterRadius: number };
}

// TSV state
export interface TSVState {
  x: number;
  y: number;
  z: number;
  redundant: boolean;
  failed: boolean;
  usageCount: number;
}

// Request types
export interface LiveRequest {
  id: number;
  sx: number; sy: number; sz: number;
  ex: number; ey: number; ez: number;
  time: number;
  path: [number, number, number][];
  rerouted?: boolean;
}

export interface CompletedRequest {
  id: number;
  time: number;
  latency: number;
}

export interface FailedRequest {
  id: number;
  time: number;
  reason: string;
}

export interface RerouteEvent {
  requestId: number;
  oldPath: [number, number, number][];
  newPath: [number, number, number][];
  redundantUsed: Point;
}

export interface FailureEvent {
  x: number;
  y: number;
  z: number;
  cycle: number;
}

// Messages from C++
export interface InitMessage {
  type: 'init';
  config: SimConfig;
  grid: { x: number; y: number; z: number; redundant: boolean; failed: boolean }[];
  hotspotWeights: number[][];
  initialFailures: Point[];
}

export interface CycleUpdate {
  type: 'cycle';
  cycle: number;
  updateVisuals: boolean;
  events: {
    newRequests: LiveRequest[];
    completed: CompletedRequest[];
    failed: FailedRequest[];
    newFailures: FailureEvent[];
    reroutes: RerouteEvent[];
  };
  stats: RunningStats;
  keyEvent: boolean;
}

export interface DoneMessage {
  type: 'done';
  summary: {
    totalRequests: number;
    completed: number;
    failed: number;
    successRate: number;
    avgLatency: number;
    maxLatency: number;
    minLatency: number;
  };
}

export interface RunningStats {
  pending: number;
  transmitting: number;
  totalRequests: number;
  completed: number;
  failed: number;
  failedTSVs: number;
  avgLatency: number;
  redundantUsages: number;
}

export interface TimePoint {
  cycle: number;
  value: number;
}

export type SimStatus = 'idle' | 'configuring' | 'running' | 'paused' | 'stepping' | 'done';
export type ViewMode = 'dashboard' | 'grid3d' | 'heatmap' | 'failure' | 'quad';
export type SpeedMode = 'slow' | 'medium' | 'high' | 'auto' | 'step';
export type ColorMode = 'type' | 'traffic' | 'status';
