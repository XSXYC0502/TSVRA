// Requires npm run build and a compiled ../bin/tsvra (or TSVRA_BIN).
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { readFile } from 'node:fs/promises';
import { createServer } from 'node:net';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import WebSocket from 'ws';
import ts from 'typescript';

const directory = fileURLToPath(new URL('..', import.meta.url));
const configSource = await readFile(new URL('../shared/simulationConfig.ts', import.meta.url), 'utf8');
const configJs = ts.transpileModule(configSource, { compilerOptions: { module: ts.ModuleKind.ESNext } }).outputText;
const { defaultLaunchConfig } = await import(`data:text/javascript;base64,${Buffer.from(configJs).toString('base64')}`);
const portProbe = createServer();
await new Promise((resolve, reject) => { portProbe.once('error', reject); portProbe.listen(0, '127.0.0.1', resolve); });
const address = portProbe.address();
assert.ok(address && typeof address !== 'string');
const port = address.port;
await new Promise(resolve => portProbe.close(resolve));
const httpUrl = `http://127.0.0.1:${port}`;
const wsUrl = `ws://127.0.0.1:${port}/api/_ws`;
let output = '';
const server = spawn(process.execPath, ['.output/server/index.mjs'], {
  cwd: directory,
  env: { ...process.env, NITRO_HOST: '127.0.0.1', NITRO_PORT: String(port), TSVRA_BIN: process.env.TSVRA_BIN ?? path.resolve(directory, '../bin/tsvra') },
  stdio: ['ignore', 'pipe', 'pipe'],
});
server.stdout.on('data', chunk => { output = (output + chunk).slice(-12000); });
server.stderr.on('data', chunk => { output = (output + chunk).slice(-12000); });
const sockets = [];

async function connectProbe() {
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
  let available = false;
  for (let attempt = 0; attempt < 100; attempt++) {
    try { available = (await fetch(httpUrl)).ok; } catch { /* server booting */ }
    if (available) break;
    if (server.exitCode !== null) throw new Error('Nuxt server exited before startup');
    await delay(50);
  }
  assert.equal(available, true, 'Nuxt server startup');
  const probe = await connectProbe();
  const status = await probe.waitFor(message => message.type === 'status');
  assert.equal(status.binaryAvailable, true);

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
  }
  console.log('PASS: reset/relaunch keeps web V1/H1000/R15, both remaining redundancy layouts, uniform failures and cumulative summaries');
} catch (error) {
  console.error(output);
  throw error;
} finally {
  for (const socket of sockets) socket.close();
  server.kill('SIGTERM');
  await new Promise(resolve => {
    if (server.exitCode !== null || server.signalCode !== null) resolve();
    else { server.once('exit', resolve); setTimeout(resolve, 2000).unref(); }
  });
}
