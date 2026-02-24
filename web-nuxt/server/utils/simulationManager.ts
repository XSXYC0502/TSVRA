import { ChildProcess, spawn } from 'child_process';
import { createInterface } from 'readline';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

export type SimConfig = {
  numLayers: number;
  gridFactor: number;
  failureMode: 'a' | 'b' | 'c';
  failureRate: number;
  verticalDelay: number;
  horizontalDelay: number;
  cycles: number;
  batchSize: number;
  seed?: number;
  lambda1?: number;
  lambda2?: number;
  maxPathLength?: number;
  reliabilityMin?: number;
};

export class SimulationManager {
  private process: ChildProcess | null = null;
  private onMessage: (msg: unknown) => void;
  private onExit: (code: number | null) => void;

  constructor(onMessage: (msg: unknown) => void, onExit: (code: number | null) => void) {
    this.onMessage = onMessage;
    this.onExit = onExit;
  }

  start(config: SimConfig): void {
    if (this.process) {
      this.kill();
    }

    const binPath = path.resolve(__dirname, '../../bin/tsvra');

    const args = [
      '--json-stream',
      '--layers', String(config.numLayers),
      '--grid-factor', String(config.gridFactor),
      '--failure-mode', config.failureMode,
      '--failure-rate', String(config.failureRate),
      '--vertical-delay', String(config.verticalDelay),
      '--horizontal-delay', String(config.horizontalDelay),
      '--cycles', String(config.cycles),
      '--batch-size', String(config.batchSize),
    ];

    if (config.seed !== undefined) args.push('--seed', String(config.seed));
    if (config.lambda1 !== undefined) args.push('--lambda1', String(config.lambda1));
    if (config.lambda2 !== undefined) args.push('--lambda2', String(config.lambda2));
    if (config.maxPathLength !== undefined) args.push('--max-path-length', String(config.maxPathLength));
    if (config.reliabilityMin !== undefined) args.push('--reliability-min', String(config.reliabilityMin));

    this.process = spawn(binPath, args, {
      stdio: ['pipe', 'pipe', 'pipe'],
    });

    const rl = createInterface({ input: this.process.stdout! });
    rl.on('line', (line: string) => {
      try {
        const msg = JSON.parse(line);
        this.onMessage(msg);
      } catch {
        console.error('Failed to parse JSON from C++:', line);
      }
    });

    this.process.stderr?.on('data', (data: Buffer) => {
      console.error('[C++]', data.toString());
    });

    this.process.on('error', (err: NodeJS.ErrnoException) => {
      console.error('Simulator process error:', err.message);
    });

    this.process.on('exit', (code) => {
      this.onExit(code);
      this.process = null;
    });
  }

  sendCommand(cmd: string): void {
    if (this.process?.stdin?.writable) {
      this.process.stdin.write(cmd);
    }
  }

  pause(): void {
    this.sendCommand('p');
  }

  resume(): void {
    this.sendCommand('r');
  }

  stop(): void {
    this.sendCommand('s');
  }

  step(): void {
    this.sendCommand('n');
  }

  kill(): void {
    if (this.process) {
      this.process.kill('SIGTERM');
      this.process = null;
    }
  }

  isRunning(): boolean {
    return this.process !== null;
  }
}
