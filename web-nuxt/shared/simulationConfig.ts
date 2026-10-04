import type { FailureMode, LaunchConfig, Point, RedundancyLayout } from '../types/simulation';

export function defaultLaunchConfig(): LaunchConfig {
  return {
    numLayers: 2,
    gridFactor: 4,
    failureMode: 'c',
    initialFailureRate: 0.000001,
    runtimeFailureRate: 0.000001,
    verticalDelay: 1,
    horizontalDelay: 1000,
    routerDelay: 15,
    cycles: 10000,
    seed: 0,
    redundancy: 'shared',
    requestProbability: 0.5,
    failureModel: { type: 'uniform', hotspotCount: 1, hotspotRadius: 4, hotspotMultiplier: 5, centers: [] },
  };
}

export function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

export type ConfigValidation =
  | { ok: true; config: LaunchConfig }
  | { ok: false; errors: string[] };

// No numeric coercion here: strings, NaN, infinity and fractional counts are invalid.
export function validateLaunchConfig(input: unknown): ConfigValidation {
  if (!isRecord(input)) return { ok: false, errors: ['Configuration must be an object.'] };
  const defaults = defaultLaunchConfig();
  const errors: string[] = [];

  function number(value: unknown, label: string, fallback: number, min: number, max: number, integer = false): number {
    if (value === undefined) return fallback;
    if (typeof value !== 'number' || !Number.isFinite(value) || value < min || value > max || (integer && !Number.isInteger(value))) {
      errors.push(`${label} must be ${integer ? 'an integer' : 'a finite number'} between ${min} and ${max}.`);
      return fallback;
    }
    return value;
  }

  function choice<T extends string>(value: unknown, label: string, choices: readonly T[], fallback: T): T {
    if (value === undefined) return fallback;
    const match = choices.find(option => option === value);
    if (match === undefined) errors.push(`${label} must be ${choices.join(', ')}.`);
    return match ?? fallback;
  }

  const redundancy = choice<RedundancyLayout>(input.redundancy, 'Redundancy layout', ['shared', 'corner4', 'none'], defaults.redundancy);
  const numLayers = number(input.numLayers, 'Layers', defaults.numLayers, 2, 10, true);
  const gridFactor = number(input.gridFactor, 'Grid factor', defaults.gridFactor, redundancy === 'shared' ? 3 : 1, 16, true);
  const failureRate = input.failureRate === undefined ? undefined : number(input.failureRate, 'Failure rate', defaults.runtimeFailureRate, 0, 1);
  const modelInput = input.failureModel;
  if (modelInput !== undefined && !isRecord(modelInput)) errors.push('Failure distribution must be an object.');
  const model = isRecord(modelInput) ? modelInput : {};
  const hotspotCount = number(model.hotspotCount, 'Hotspot count', defaults.failureModel.hotspotCount, 1, 1000, true);
  const centers: Point[] = [];
  if (model.centers !== undefined) {
    if (!Array.isArray(model.centers)) {
      errors.push('Hotspot centers must be an array of x, y, z coordinates.');
    } else {
      for (const [index, center] of model.centers.entries()) {
        if (!isRecord(center)) {
          errors.push(`Hotspot center ${index + 1} must contain x, y, z coordinates.`);
          continue;
        }
        centers.push({
          x: number(center.x ?? null, `Center ${index + 1} x`, 0, 0, gridFactor * 4 - 1, true),
          y: number(center.y ?? null, `Center ${index + 1} y`, 0, 0, gridFactor * 4 - 1, true),
          z: number(center.z ?? null, `Center ${index + 1} z (lower layer)`, 0, 0, numLayers - 2, true),
        });
      }
      if (centers.length > 0 && centers.length !== hotspotCount) errors.push('Manual center count must match hotspot count.');
    }
  }

  const config: LaunchConfig = {
    numLayers,
    gridFactor,
    failureMode: choice<FailureMode>(input.failureMode, 'Failure timing', ['a', 'b', 'c'], defaults.failureMode),
    initialFailureRate: number(input.initialFailureRate, 'Initial failure probability', failureRate ?? defaults.initialFailureRate, 0, 1),
    runtimeFailureRate: number(input.runtimeFailureRate, 'Runtime failure probability', failureRate ?? defaults.runtimeFailureRate, 0, 1),
    verticalDelay: number(input.verticalDelay, 'Vertical delay', defaults.verticalDelay, 1, 1000000000, true),
    horizontalDelay: number(input.horizontalDelay, 'Horizontal delay', defaults.horizontalDelay, 1, 1000000000, true),
    routerDelay: number(input.routerDelay, 'Router delay', defaults.routerDelay, 0, 1000000000, true),
    cycles: number(input.cycles, 'Cycles', defaults.cycles, 1, 1000000000, true),
    seed: number(input.seed, 'Seed', defaults.seed, 0, 4294967295, true),
    redundancy,
    requestProbability: number(input.requestProbability, 'Request probability', defaults.requestProbability, 0, 1),
    failureModel: {
      type: choice(model.type, 'Failure distribution', ['uniform', 'hotspot'], defaults.failureModel.type),
      hotspotCount,
      hotspotRadius: number(model.hotspotRadius, 'Hotspot radius', defaults.failureModel.hotspotRadius, Number.MIN_VALUE, 1000000000),
      hotspotMultiplier: number(model.hotspotMultiplier, 'Hotspot multiplier', defaults.failureModel.hotspotMultiplier, 1, 1000000000),
      centers,
    },
  };

  if (input.powerConfig !== undefined) {
    if (typeof input.powerConfig !== 'string' || input.powerConfig.includes('\0')) {
      errors.push('Power configuration must be a file path.');
    } else if (input.powerConfig.trim()) {
      config.powerConfig = input.powerConfig.trim();
    }
  }
  if (input.lambda1 !== undefined) config.lambda1 = number(input.lambda1, 'Lambda 1', 1, 0, 1000000000);
  if (input.lambda2 !== undefined) config.lambda2 = number(input.lambda2, 'Lambda 2', 1, 0, 1000000000);
  if (input.maxPathLength !== undefined) config.maxPathLength = number(input.maxPathLength, 'Maximum path length', 0, 0, 1000000000, true);
  if (input.maxHorizontalDistance !== undefined) config.maxHorizontalDistance = number(input.maxHorizontalDistance, 'Maximum horizontal distance', 0, 0, 1000000000, true);
  if (input.reliabilityMin !== undefined) config.reliabilityMin = number(input.reliabilityMin, 'Minimum reliability', 0, 0, 1);

  return errors.length ? { ok: false, errors } : { ok: true, config };
}

export function buildSimulatorArgs(config: LaunchConfig): string[] {
  const args = [
    '--json-stream',
    '--layers', String(config.numLayers),
    '--grid-factor', String(config.gridFactor),
    '--failure-mode', config.failureMode,
    '--initial-failure-rate', String(config.initialFailureRate),
    '--runtime-failure-rate', String(config.runtimeFailureRate),
    '--vertical-delay', String(config.verticalDelay),
    '--horizontal-delay', String(config.horizontalDelay),
    '--router-delay', String(config.routerDelay),
    '--cycles', String(config.cycles),
    '--seed', String(config.seed),
    '--redundancy', config.redundancy,
    '--failure-model', config.failureModel.type,
    '--hotspot-count', String(config.failureModel.hotspotCount),
    '--hotspot-radius', String(config.failureModel.hotspotRadius),
    '--hotspot-multiplier', String(config.failureModel.hotspotMultiplier),
    '--request-probability', String(config.requestProbability),
  ];
  if (config.failureModel.centers.length) args.push('--hotspot-centers', config.failureModel.centers.map(p => `${p.x},${p.y},${p.z}`).join(';'));
  if (config.powerConfig) args.push('--power-config', config.powerConfig);
  if (config.lambda1 !== undefined) args.push('--lambda1', String(config.lambda1));
  if (config.lambda2 !== undefined) args.push('--lambda2', String(config.lambda2));
  if (config.maxPathLength !== undefined) args.push('--max-path-length', String(config.maxPathLength));
  if (config.maxHorizontalDistance !== undefined) args.push('--max-horizontal-distance', String(config.maxHorizontalDistance));
  if (config.reliabilityMin !== undefined) args.push('--reliability-min', String(config.reliabilityMin));
  return args;
}

export function parseHotspotCenters(text: string): Point[] | null {
  if (!text.trim()) return [];
  const centers: Point[] = [];
  for (const entry of text.trim().split(';')) {
    const coordinates = entry.trim().split(',').map(value => value.trim());
    if (coordinates.length !== 3 || coordinates.some(value => !/^\d+$/.test(value))) return null;
    const [x, y, z] = coordinates.map(Number);
    if (x === undefined || y === undefined || z === undefined) return null;
    centers.push({ x, y, z });
  }
  return centers;
}
