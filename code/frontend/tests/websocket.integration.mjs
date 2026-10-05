// Requires npm run build and the native build under code/build.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { readFile, mkdtemp, mkdir, writeFile, rm } from 'node:fs/promises';
import { createServer } from 'node:net';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import os from 'node:os';
import { setTimeout as delay } from 'node:timers/promises';
import WebSocket from 'ws';
import ts from 'typescript';

const directory = fileURLToPath(new URL('..', import.meta.url));
const repositoryRoot = path.resolve(directory, '../..');
const serverEntry = path.join(directory, '.output/server/index.mjs');
const configSource = await readFile(new URL('../shared/simulationConfig.ts', import.meta.url), 'utf8');
const configJs = ts.transpileModule(configSource, { compilerOptions: { module: ts.ModuleKind.ESNext } }).outputText;
const { defaultLaunchConfig } = await import(`data:text/javascript;base64,${Buffer.from(configJs).toString('base64')}`);
let output = '';
const servers = [];
const sockets = [];
let runtimeFixture;

async function launchServer(cwd, overrides = {}) {
  const portProbe = createServer();
  await new Promise((resolve, reject) => { portProbe.once('error', reject); portProbe.listen(0, '127.0.0.1', resolve); });
  const address = portProbe.address();
  assert.ok(address && typeof address !== 'string');
  const port = address.port;
  await new Promise(resolve => portProbe.close(resolve));
  const env = { ...process.env, NITRO_HOST: '127.0.0.1', NITRO_PORT: String(port) };
  // Exercise discovery independently of overrides inherited from the test shell.
  delete env.TSVRA_BIN;
  delete env.TSVRA_ROOT;
  Object.assign(env, overrides);
  const server = spawn(process.execPath, [serverEntry], { cwd, env, stdio: ['ignore', 'pipe', 'pipe'] });
  servers.push(server);
  server.stdout.on('data', chunk => { output = (output + chunk).slice(-12000); });
  server.stderr.on('data', chunk => { output = (output + chunk).slice(-12000); });
  const httpUrl = `http://127.0.0.1:${port}`;
  let available = false;
  for (let attempt = 0; attempt < 100; attempt++) {
    try { available = (await fetch(httpUrl)).ok; } catch { /* server booting */ }
    if (available) break;
    if (server.exitCode !== null) throw new Error('Nuxt server exited before startup');
    await delay(50);
  }
  assert.equal(available, true, 'Nuxt server startup');
  return { server, wsUrl: `ws://127.0.0.1:${port}/api/_ws` };
}

async function closeServer(server) {
  if (server.exitCode !== null || server.signalCode !== null) return;
  server.kill('SIGTERM');
  await new Promise(resolve => {
    let timer;
    const finish = () => { clearTimeout(timer); resolve(); };
    server.once('exit', finish);
    timer = setTimeout(() => { server.kill('SIGKILL'); resolve(); }, 2000);
  });
}

async function connectProbe(wsUrl) {
  const socket = new WebSocket(wsUrl);
  sockets.push(socket);
  const messages = [];
  const waiters = new Set();
  socket.on('message', data => {
    messages.push(JSON.parse(data.toString()));
    for (const check of [...waiters]) check();
  });
  await new Promise((resolve, reject) => { socket.once('open', resolve); socket.once('error', reject); });
  return {
    messages,
    send: message => socket.send(JSON.stringify(message)),
    waitFor(predicate, from = 0) {
      return new Promise((resolve, reject) => {
        let timer;
        const check = () => {
          const match = messages.slice(from).find(predicate);
          if (match) { clearTimeout(timer); waiters.delete(check); resolve(match); }
        };
        timer = setTimeout(() => { waiters.delete(check); reject(new Error('Timed out waiting for a WebSocket event')); }, 5000);
        waiters.add(check);
        check();
      });
    },
  };
}

try {
  const frontendServer = await launchServer(directory);
  const probe = await connectProbe(frontendServer.wsUrl);
  const status = await probe.waitFor(message => message.type === 'status');
  assert.equal(status.binaryAvailable, true);
  assert.equal(path.basename(status.binaryPath), process.platform === 'win32' ? 'tsvra.exe' : 'tsvra');
  assert.ok(status.binaryPath.startsWith(path.join(repositoryRoot, 'code/build')));
  console.log('PASS: production server discovers the relocated native binary from frontend cwd without overrides');

  // Invalid configuration does not create a simulator, including JSON null/infinity.
  for (const config of [null, { cycles: null }, { runtimeFailureRate: 2 }, { redundancy: 'bad' }, { failureModel: { type: 'hotspot', centers: [{ x: 0, y: 0, z: 1 }] } }]) {
    const mark = probe.messages.length;
    probe.send({ type: 'start', config });
    const error = await probe.waitFor(message => message.type === 'error', mark);
    assert.equal(error.code, 'invalid_config');
    assert.equal(probe.messages.slice(mark).some(message => message.type === 'init'), false);
  }
  console.log('PASS: WebSocket API rejects invalid config before native launch');

  const config = {
    ...defaultLaunchConfig(), numLayers: 3, gridFactor: 3, cycles: 20, seed: 123,
    redundancy: 'corner4', verticalDelay: 7, horizontalDelay: 321, routerDelay: 22,
    initialFailureRate: 0.001, runtimeFailureRate: 0.00001, requestProbability: 0,
    failureModel: { type: 'hotspot', hotspotCount: 2, hotspotRadius: 3.5, hotspotMultiplier: 8, centers: [{ x: 1, y: 2, z: 0 }, { x: 3, y: 4, z: 1 }] },
    maxHorizontalDistance: 50,
  };
  probe.send({ type: 'start', config });
  const init = await probe.waitFor(message => message.type === 'init');
  assert.equal(init.config.verticalDelay, 7);
  assert.equal(init.config.horizontalDelay, 321);
  assert.equal(init.config.routerDelay, 22);
  assert.equal(init.config.actualSeed, 123);
  assert.equal(init.config.requestProbability, 0);
  assert.equal(init.config.initialFailureRate, config.initialFailureRate);
  assert.equal(init.config.runtimeFailureRate, config.runtimeFailureRate);
  assert.equal(init.config.redundancy.layout, 'corner4');
  assert.equal(init.config.failureModel.type, 'hotspot');
  assert.equal(init.config.failureModel.hotspotRadius, 3.5);
  assert.equal(init.config.failureModel.hotspotMultiplier, 8);
  assert.deepEqual(init.failureHotspots, config.failureModel.centers);
  assert.equal(init.failureProbabilities.length, 12 * 12 * 2);
  assert.ok(init.failureProbabilities.every(point => point.z < 2));
  assert.ok(init.grid.filter(point => point.z === 2).every(point => !point.redundant));
  assert.equal(init.powerModel.calibrationStatus, 'dsent_bulk45_reference');
  assert.ok(init.powerModel.parameterSource.startsWith('https://'));
  assert.equal(init.powerModel.frequencyHz, 1e9);
  assert.ok(init.powerParameters, 'native includes exact coefficients loaded from the relocated power file');
  await delay(50);
  assert.equal(probe.messages.some(message => message.type === 'cycle'), false, 'native starts paused');
  console.log('PASS: native receives explicit delays, redundant layout, Gaussian centers, rate split, seed and power file from web');

  probe.send({ type: 'ack', count: 1 });
  const first = await probe.waitFor(message => message.type === 'cycle');
  assert.equal(first.cycle, 0);
  assert.equal(first.stats.power.elapsedCycles, 1);
  assert.equal(first.stats.power.elapsedSeconds, 1e-9);
  assert.equal(first.stats.power.energyPerCompletedFlitPj, null);
  assert.equal(first.stats.power.horizontalHops, 0);
  assert.equal(first.stats.power.totalMw, first.stats.power.routerStaticMw);
  const firstEnergy = first.stats.power.totalEnergyPj;
  const pauseMark = probe.messages.length;
  probe.send({ type: 'pause' });
  await delay(75);
  assert.equal(probe.messages.slice(pauseMark).some(message => message.type === 'cycle'), false);
  assert.equal(first.stats.power.totalEnergyPj, firstEnergy);

  probe.send({ type: 'resume' });
  probe.send({ type: 'ack', count: 1 });
  const second = await probe.waitFor(message => message.type === 'cycle' && message.cycle === 1, pauseMark);
  assert.equal(second.stats.power.elapsedCycles, 2);
  assert.ok(second.stats.power.totalEnergyPj > firstEnergy);
  const stepMark = probe.messages.length;
  await delay(50);
  assert.equal(probe.messages.slice(stepMark).some(message => message.type === 'cycle'), false, 'one ack produces one cycle');

  probe.send({ type: 'speed' });
  probe.send({ type: 'ack', count: 2 });
  const fourth = await probe.waitFor(message => message.type === 'cycle' && message.cycle === 3, stepMark);
  assert.equal(fourth.stats.power.elapsedCycles, 4);
  assert.equal(fourth.stats.power.totalMw, first.stats.power.totalMw);
  assert.equal(fourth.stats.power.totalEnergyPj, firstEnergy * 4);
  console.log('PASS: pause, resume, exact stepping and speed credits preserve simulation-time power and energy');

  probe.send({ type: 'stop' });
  const done = await probe.waitFor(message => message.type === 'done');
  const exit = await probe.waitFor(message => message.type === 'process_exit');
  assert.equal(exit.code, 0);
  assert.equal(done.summary.power.elapsedCycles, 4);
  assert.equal(done.summary.power.totalEnergyPj, fourth.stats.power.totalEnergyPj);
  assert.equal(done.summary.power.energyPerCompletedFlitPj, null);
  console.log('PASS: graceful Stop returns partial summary.power and native process exits');

  let baselineSummary;
  for (const redundancy of ['none', 'shared']) {
    const mark = probe.messages.length;
    const webDefaults = { ...defaultLaunchConfig(), redundancy, cycles: 3, seed: 789, initialFailureRate: 0, runtimeFailureRate: 0, requestProbability: 0 };
    probe.send({ type: 'start', config: webDefaults });
    const nextInit = await probe.waitFor(message => message.type === 'init', mark);
    assert.equal(nextInit.config.verticalDelay, 1);
    assert.equal(nextInit.config.horizontalDelay, 1000);
    assert.equal(nextInit.config.routerDelay, 15);
    assert.equal(nextInit.config.redundancy.layout, redundancy);
    assert.equal(nextInit.config.failureModel.type, 'uniform');
    assert.deepEqual(nextInit.failureHotspots, []);
    probe.send({ type: 'ack', count: 3 });
    const final = await probe.waitFor(message => message.type === 'done', mark);
    const finalExit = await probe.waitFor(message => message.type === 'process_exit', mark);
    assert.equal(finalExit.code, 0);
    assert.equal(final.summary.power.elapsedCycles, 3);
    assert.equal(final.summary.totalRequests, 0);
    if (redundancy === 'shared') baselineSummary = final.summary;
  }
  console.log('PASS: reset/relaunch keeps web V1/H1000/R15, both remaining redundancy layouts, uniform failures and cumulative summaries');

  const smokeConfig = {
    ...defaultLaunchConfig(), cycles: 3, seed: 789,
    initialFailureRate: 0, runtimeFailureRate: 0, requestProbability: 0,
  };
  async function runSmoke(socketProbe, config, expectedFrequency) {
    const mark = socketProbe.messages.length;
    socketProbe.send({ type: 'start', config });
    const nextInit = await socketProbe.waitFor(message => message.type === 'init', mark);
    assert.equal(nextInit.powerModel.frequencyHz, expectedFrequency);
    assert.equal(nextInit.config.actualSeed, config.seed);
    socketProbe.send({ type: 'ack', count: config.cycles });
    const result = await socketProbe.waitFor(message => message.type === 'done', mark);
    const processExit = await socketProbe.waitFor(message => message.type === 'process_exit', mark);
    assert.equal(processExit.code, 0);
    assert.equal(result.summary.power.elapsedCycles, config.cycles);
    assert.equal(result.summary.power.elapsedSeconds, config.cycles / expectedFrequency);
    return result.summary;
  }

  await closeServer(frontendServer.server);
  const rootServer = await launchServer(repositoryRoot);
  const rootProbe = await connectProbe(rootServer.wsUrl);
  const rootStatus = await rootProbe.waitFor(message => message.type === 'status');
  assert.equal(rootStatus.binaryPath, status.binaryPath);
  const rootSummary = await runSmoke(rootProbe, smokeConfig, 1e9);
  assert.deepEqual(rootSummary, baselineSummary, 'frontend/root cwd produce identical deterministic native summaries');
  console.log('PASS: production server launched from repository cwd finds the same binary/configuration and produces identical results');
  await closeServer(rootServer.server);

  // Distinct frequencies prove that both the runtime root and an explicit
  // powerConfig path reach the child; a successful launch alone cannot do that.
  runtimeFixture = await mkdtemp(path.join(os.tmpdir(), 'tsvra-web-runtime-'));
  await mkdir(path.join(runtimeFixture, 'code/config'), { recursive: true });
  const originalPower = await readFile(path.join(repositoryRoot, 'code/config/power_model.ini'), 'utf8');
  const customPower = originalPower.replace(/^frequency_hz\s*=.*$/m, 'frequency_hz = 2000000000');
  assert.notEqual(customPower, originalPower, 'custom fixture updates the frequency coefficient');
  await writeFile(path.join(runtimeFixture, 'code/config/power_model.ini'), customPower);
  await writeFile(path.join(runtimeFixture, 'code/config/reference.ini'), originalPower);
  const overrideServer = await launchServer(directory, { TSVRA_BIN: status.binaryPath, TSVRA_ROOT: runtimeFixture });
  const overrideProbe = await connectProbe(overrideServer.wsUrl);
  const overrideStatus = await overrideProbe.waitFor(message => message.type === 'status');
  assert.equal(overrideStatus.binaryPath, status.binaryPath);
  await runSmoke(overrideProbe, smokeConfig, 2e9);
  const relativeSummary = await runSmoke(overrideProbe, { ...smokeConfig, powerConfig: 'code/config/reference.ini' }, 1e9);
  assert.deepEqual(relativeSummary, baselineSummary, 'explicit relative power path resolves from TSVRA_ROOT');
  const absoluteSummary = await runSmoke(overrideProbe, {
    ...smokeConfig, powerConfig: path.join(repositoryRoot, 'code/config/power_model.ini'),
  }, 1e9);
  assert.deepEqual(absoluteSummary, baselineSummary, 'explicit absolute power path overrides the default runtime file');
  console.log('PASS: TSVRA_BIN/TSVRA_ROOT and explicit relative/absolute power files preserve native launch semantics');
} catch (error) {
  console.error(output);
  throw error;
} finally {
  for (const socket of sockets) socket.close();
  await Promise.all(servers.map(closeServer));
  if (runtimeFixture) await rm(runtimeFixture, { recursive: true, force: true });
}
