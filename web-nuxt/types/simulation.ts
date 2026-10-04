// 3D coordinate
export interface Point {
  x: number;
  y: number;
  z: number;
}

export type FailureMode = 'a' | 'b' | 'c';
export type RedundancyLayout = 'shared' | 'corner4' | 'none';

export interface FailureModel {
  type: 'uniform' | 'hotspot';
  hotspotCount: number;
  hotspotRadius: number;
  hotspotMultiplier: number;
  centers: Point[];
}

// Validated launch settings shared by the form, WebSocket, and server.
export interface LaunchConfig {
  numLayers: number;
  gridFactor: number;
  failureMode: FailureMode;
  initialFailureRate: number;
  runtimeFailureRate: number;
  verticalDelay: number;
  horizontalDelay: number;
  routerDelay: number;
  cycles: number;
  seed: number;
  redundancy: RedundancyLayout;
  failureModel: FailureModel;
  requestProbability: number;
  powerConfig?: string;
  lambda1?: number;
  lambda2?: number;
  maxPathLength?: number;
  maxHorizontalDistance?: number;
  reliabilityMin?: number;
}

// Configuration
export interface SimConfig {
  gridSize: number;
  numLayers: number;
  gridFactor: number;
  failureMode: FailureMode;
  failureRate?: number;
  initialFailureRate: number;
  runtimeFailureRate: number;
  verticalDelay: number;
  horizontalDelay: number;
  routerDelay: number;
  requestProbability: number;
  actualSeed?: number;
  totalCycles: number;
  redundancy?: { layout: RedundancyLayout; sparesPerLayer: number; redundancyRatio: number };
  failureModel: FailureModel;
}

export interface PowerStats {
  elapsedCycles: number;
  elapsedSeconds: number;
  routerEvents: number;
  horizontalHops: number;
  verticalHops: number;
  redundantVerticalHops: number;
  routerStaticMw: number;
  routerDynamicMw: number;
  horizontalMw: number;
  verticalMw: number;
  totalMw: number;
  totalEnergyPj: number;
  routerEnergyPj: number;
  horizontalEnergyPj: number;
  verticalEnergyPj: number;
  intervalTotalMw: number;
  energyPerCompletedFlitPj: number | null;
}

export interface PowerModel {
  technologyNm: number;
  technology?: string;
  voltageV: number;
  frequencyHz: number;
  flitBits: number;
  toggleProbability: number;
  calibrationStatus: string;
  parameterSource: string;
}

export interface FailureProbability extends Point {
  initial: number;
  runtime: number;
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
  failureHotspots?: Point[];
  failureProbabilities?: FailureProbability[];
  powerModel?: PowerModel;
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
    power?: PowerStats;
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
  power?: PowerStats;
}

export interface TimePoint {
  cycle: number;
  value: number;
}

export interface PowerSample {
  elapsedSeconds: number;
  average: { routerStatic: number; routerDynamic: number; horizontal: number; vertical: number; total: number };
  interval: { routerStatic: number; routerDynamic: number; horizontal: number; vertical: number; total: number };
  energyPj: number;
}

export type SimStatus = 'idle' | 'configuring' | 'running' | 'paused' | 'stepping' | 'stopping' | 'done';
export type ViewMode = 'dashboard' | 'grid3d' | 'heatmap' | 'failure' | 'quad';
export type SpeedMode = 'slow' | 'medium' | 'high' | 'auto' | 'step';
export type ColorMode = 'type' | 'traffic' | 'status';
