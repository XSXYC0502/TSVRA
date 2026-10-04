import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import ts from 'typescript';
import { createPinia, setActivePinia } from 'pinia';

async function loadTypeScript(relative, replacements = {}) {
  const source = await readFile(new URL(relative, import.meta.url), 'utf8');
  let { outputText } = ts.transpileModule(source, { compilerOptions: { module: ts.ModuleKind.ESNext, target: ts.ScriptTarget.ES2022 } });
  for (const [name, url] of Object.entries(replacements)) {
    outputText = outputText.replaceAll(`'${name}'`, JSON.stringify(url));
  }
  const url = `data:text/javascript;base64,${Buffer.from(outputText).toString('base64')}`;
  return { url, module: await import(url) };
}

const configSource = await loadTypeScript('../shared/simulationConfig.ts');
const powerSource = await loadTypeScript('../shared/power.ts');
const { defaultLaunchConfig, validateLaunchConfig, buildSimulatorArgs, parseHotspotCenters } = configSource.module;
const { CHART_HISTORY_LIMIT, appendHistory, powerSample } = powerSource.module;
const { module: formatters } = await loadTypeScript('../utils/formatters.ts');
const { module: storeModule } = await loadTypeScript('../stores/simulation.ts', {
  pinia: import.meta.resolve('pinia'),
  vue: import.meta.resolve('vue'),
  '~/shared/simulationConfig': configSource.url,
  '~/shared/power': powerSource.url,
});

function baselinePower(overrides = {}) {
  return {
    elapsedCycles: 1, elapsedSeconds: 1e-9,
    routerEvents: 0, horizontalHops: 0, verticalHops: 0, redundantVerticalHops: 0,
    routerStaticMw: 1, routerDynamicMw: 2, horizontalMw: 3, verticalMw: 4, totalMw: 10,
    routerEnergyPj: 3, horizontalEnergyPj: 3, verticalEnergyPj: 4, totalEnergyPj: 10,
    intervalTotalMw: 10, energyPerCompletedFlitPj: null,
    ...overrides,
  };
}

function initialMessage() {
  const launch = defaultLaunchConfig();
  return {
    type: 'init',
    config: { ...launch, gridSize: 12, gridFactor: 3, numLayers: 3, totalCycles: 1000, actualSeed: 123, redundancy: { layout: 'corner4', sparesPerLayer: 36, redundancyRatio: 0.25 } },
    grid: [{ x: 0, y: 0, z: 0, redundant: true, failed: true }],
    initialFailures: [{ x: 0, y: 0, z: 0 }],
    hotspotWeights: [[1]],
    failureHotspots: [{ x: 4, y: 4, z: 0 }],
    failureProbabilities: [{ x: 0, y: 0, z: 0, initial: 0.1, runtime: 0.001 }],
    powerModel: { technologyNm: 45, voltageV: 1, frequencyHz: 1e9, flitBits: 64, toggleProbability: 0.15, calibrationStatus: 'reference', parameterSource: 'default' },
  };
}

function cycleMessage(cycle, overrides = {}) {
  return {
    type: 'cycle', cycle, updateVisuals: true, keyEvent: false,
    events: { newRequests: [], completed: [], failed: [], newFailures: [], reroutes: [] },
    stats: {
      pending: 0, transmitting: 0, totalRequests: 0, completed: 0, failed: 0, failedTSVs: 1, avgLatency: 0, redundantUsages: 0,
      power: baselinePower({ elapsedCycles: cycle + 1, elapsedSeconds: (cycle + 1) * 1e-9, totalEnergyPj: (cycle + 1) * 10, routerEnergyPj: (cycle + 1) * 3, horizontalEnergyPj: (cycle + 1) * 3, verticalEnergyPj: (cycle + 1) * 4 }),
    },
    ...overrides,
  };
}

test('web defaults retain explicit V1/H1000 with per-hop router delay and probability', () => {
  const validation = validateLaunchConfig(defaultLaunchConfig());
  assert.equal(validation.ok, true);
  assert.equal(validation.config.verticalDelay, 1);
  assert.equal(validation.config.horizontalDelay, 1000);
  assert.equal(validation.config.routerDelay, 15);
  assert.equal(validation.config.requestProbability, 0.5);
  assert.deepEqual(validation.config.failureModel, { type: 'uniform', hotspotCount: 1, hotspotRadius: 4, hotspotMultiplier: 5, centers: [] });
});

test('launch passes all distinct failure, redundancy, hotspot and power fields as argv', () => {
  const config = {
    ...defaultLaunchConfig(), numLayers: 4, failureMode: 'b', initialFailureRate: 0.2, runtimeFailureRate: 0.003,
    verticalDelay: 7, horizontalDelay: 321, routerDelay: 22, requestProbability: 0.9, redundancy: 'corner4', seed: 55,
    failureModel: { type: 'hotspot', hotspotCount: 2, hotspotRadius: 3.5, hotspotMultiplier: 8, centers: [{ x: 1, y: 2, z: 0 }, { x: 3, y: 4, z: 2 }] },
    powerConfig: '/tmp/power coefficients;literal.json', lambda1: 0, lambda2: 2, maxPathLength: 4000, reliabilityMin: 0.8,
  };
  const validation = validateLaunchConfig(config);
  assert.equal(validation.ok, true);
  const args = buildSimulatorArgs(validation.config);
  const flags = new Map();
  for (let i = 1; i < args.length; i += 2) flags.set(args[i], args[i + 1]);
  for (const [flag, value] of Object.entries({
    '--redundancy': 'corner4', '--failure-mode': 'b', '--failure-model': 'hotspot', '--hotspot-count': '2',
    '--hotspot-radius': '3.5', '--hotspot-multiplier': '8', '--hotspot-centers': '1,2,0;3,4,2',
    '--initial-failure-rate': '0.2', '--runtime-failure-rate': '0.003', '--router-delay': '22',
    '--vertical-delay': '7', '--horizontal-delay': '321', '--request-probability': '0.9', '--seed': '55',
    '--power-config': '/tmp/power coefficients;literal.json', '--lambda1': '0', '--lambda2': '2', '--max-path-length': '4000', '--reliability-min': '0.8',
  })) assert.equal(flags.get(flag), value, flag);
  assert.equal(flags.has('--failure-rate'), false);
  for (const redundancy of ['shared', 'corner4', 'none']) assert.equal(validateLaunchConfig({ ...config, redundancy }).ok, true);
});

test('legacy failureRate falls back separately and explicit rate overrides are preserved', () => {
  const validation = validateLaunchConfig({ failureRate: 0.01, runtimeFailureRate: 0.002 });
  assert.equal(validation.ok, true);
  assert.equal(validation.config.initialFailureRate, 0.01);
  assert.equal(validation.config.runtimeFailureRate, 0.002);
});

test('invalid API numeric inputs and enums are rejected before launch', () => {
  for (const config of [
    null, [], 42, { numLayers: 1 }, { numLayers: 2.5 }, { gridFactor: NaN }, { cycles: Infinity }, { seed: -1 },
    { initialFailureRate: -0.1 }, { runtimeFailureRate: 1.1 }, { requestProbability: '0.5' }, { routerDelay: 1.5 },
    { horizontalDelay: 0 }, { verticalDelay: 0 }, { failureMode: 'd' }, { redundancy: 'invalid' },
    { powerConfig: 123 }, { powerConfig: 'bad\0path' }, { failureModel: 'hotspot' },
    { failureModel: { type: 'clustered' } }, { failureModel: { hotspotCount: 0 } },
    { failureModel: { hotspotRadius: 0 } }, { failureModel: { hotspotMultiplier: 0.9 } },
  ]) assert.equal(validateLaunchConfig(config).ok, false, JSON.stringify(config));
});

test('manual centers require the matching count and a valid lower interface coordinate', () => {
  const base = defaultLaunchConfig();
  for (const center of [{ x: 16, y: 0, z: 0 }, { x: 0, y: -1, z: 0 }, { x: 0, y: 0, z: 1 }, { x: 0.5, y: 0, z: 0 }, { x: 0, y: 0 }]) {
    assert.equal(validateLaunchConfig({ ...base, failureModel: { ...base.failureModel, type: 'hotspot', centers: [center] } }).ok, false);
  }
  assert.equal(validateLaunchConfig({ ...base, failureModel: { ...base.failureModel, hotspotCount: 2, centers: [{ x: 0, y: 0, z: 0 }] } }).ok, false);
  assert.deepEqual(parseHotspotCenters(' 1,2,0 ; 3,4,0 '), [{ x: 1, y: 2, z: 0 }, { x: 3, y: 4, z: 0 }]);
  for (const text of ['1,,0', '1,2', '1,2,0;', '1,2.5,0', '1,2,0;wrong']) assert.equal(parseHotspotCenters(text), null);
});

test('average and interval powers use physical energy / simulated duration', () => {
  const previous = baselinePower();
  const power = baselinePower({
    elapsedCycles: 2, elapsedSeconds: 2e-9,
    routerEnergyPj: 13, horizontalEnergyPj: 8, verticalEnergyPj: 14, totalEnergyPj: 35,
    routerDynamicMw: 5.5, horizontalMw: 4, verticalMw: 7, totalMw: 17.5, intervalTotalMw: 25,
  });
  const sample = powerSample(power, previous);
  assert.deepEqual(sample.average, { routerStatic: 1, routerDynamic: 5.5, horizontal: 4, vertical: 7, total: 17.5 });
  assert.deepEqual(sample.interval, { routerStatic: 1, routerDynamic: 9, horizontal: 5, vertical: 10, total: 25 });
  assert.equal(sample.elapsedSeconds, 2e-9);
  assert.equal(sample.energyPj, 35);
  assert.equal(formatters.formatEnergy(null), 'N/A');
  assert.equal(formatters.formatEnergy(1500), '1.500 nJ');
  assert.equal(formatters.formatEnergy(1500000), '1.500 µJ');
});

test('histories are capped while cumulative totals remain accurate across pause and reset', () => {
  setActivePinia(createPinia());
  const store = storeModule.useSimulationStore();
  store.applyInitMessage(initialMessage());
  assert.equal(store.tsvGrid[0][0][0].failed, true);
  assert.equal(store.tsvGrid[1][0][0].failed, true);
  assert.equal(store.tsvGrid[2][0][0].failed, false);
  assert.equal(store.config.actualSeed, 123);
  assert.equal(store.failureProbabilities.length, 1);
  assert.deepEqual(store.failureCountTimeline, [{ cycle: 0, value: 1 }]);
  for (let cycle = 0; cycle < CHART_HISTORY_LIMIT + 10; cycle++) store.applyCycleUpdate(cycleMessage(cycle));
  assert.equal(store.powerTimeline.length, CHART_HISTORY_LIMIT);
  assert.equal(store.throughputTimeline.length, CHART_HISTORY_LIMIT);
  assert.equal(store.power.totalEnergyPj, (CHART_HISTORY_LIMIT + 10) * 10);
  store.setSimStatus('paused');
  store.setSpeedMode('slow');
  const before = store.power.totalEnergyPj;
  assert.equal(store.power.totalEnergyPj, before);
  store.applyCycleUpdate(cycleMessage(CHART_HISTORY_LIMIT + 10));
  assert.equal(store.simStatus, 'paused');
  assert.equal(store.power.totalEnergyPj, (CHART_HISTORY_LIMIT + 11) * 10);
  store.prepareRun(defaultLaunchConfig());
  assert.equal(store.power, null);
  assert.equal(store.powerTimeline.length, 0);
  assert.equal(store.throughputTimeline.length, 0);
  assert.equal(store.failureHotspots.length, 0);
  assert.equal(store.failureProbabilities.length, 0);
  assert.equal(store.lastCycleUpdate, null);
  assert.equal(store.speedMode, 'slow');
  store.reset();
  assert.equal(store.simStatus, 'idle');
});

test('preview supports all redundancy layouts and excludes the top layer from channels', () => {
  setActivePinia(createPinia());
  const store = storeModule.useSimulationStore();
  store.initializePreview(3, 3, 'corner4');
  assert.equal(store.tsvGrid[0][0][0].redundant, true);
  assert.equal(store.tsvGrid[2].flat().some(cell => cell.redundant), false);
  store.initializePreview(3, 2, 'none');
  assert.equal(store.tsvGrid.flat(2).some(cell => cell.redundant), false);
  store.initializePreview(3, 2, 'shared');
  assert.equal(store.tsvGrid[0][1][1].redundant, true);
  assert.equal(appendHistory([], new Array(CHART_HISTORY_LIMIT + 10).fill(1)).length, CHART_HISTORY_LIMIT);
});
