import { ChildProcess, spawn } from 'child_process';
import { createInterface } from 'readline';
import path from 'path';
import { fileURLToPath } from 'url';
import type { LaunchConfig } from '../../types/simulation';
import { buildSimulatorArgs } from '../../shared/simulationConfig';
import { resolveBinPath, resolveSimulationRoot } from './simulatorPaths';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const runtimeLocation = { moduleDirectory: __dirname };

export class SimulationManager {
  private process: ChildProcess | null = null;
  private onMessage: (msg: unknown) => void;
  private onExit: (code: number | null) => void;
  private onError: (err: Error) => void;
  private onStderr: (line: string) => void;

  constructor(
    onMessage: (msg: unknown) => void,
    onExit: (code: number | null) => void,
    onError: (err: Error) => void,
    onStderr: (line: string) => void,
  ) {
    this.onMessage = onMessage;
    this.onExit = onExit;
    this.onError = onError;
    this.onStderr = onStderr;
  }

  start(config: LaunchConfig): void {
    if (this.process) {
      this.kill();
    }

    let binPath: string;
    try {
      binPath = resolveBinPath(runtimeLocation);
    } catch (err) {
      this.onError(err as Error);
      return;
    }

    const root = resolveSimulationRoot(runtimeLocation);
    const args = buildSimulatorArgs({
      ...config,
      powerConfig: config.powerConfig ?? path.join(root, 'code/config/power_model.ini'),
    });

    console.log(`[SimulationManager] Spawning: ${binPath} ${args.join(' ')}`);

    const child = spawn(binPath, args, {
      stdio: ['pipe', 'pipe', 'pipe'],
      cwd: root,
    });
    this.process = child;

    const rl = createInterface({ input: this.process.stdout! });
    rl.on('line', (line: string) => {
      try {
        const msg = JSON.parse(line);
        this.onMessage(msg);
      } catch {
        console.error('Failed to parse JSON from C++:', line);
        this.onError(new Error(`Invalid JSON from C++ process: ${line.slice(0, 200)}`));
      }
    });

    this.process.stderr?.on('data', (data: Buffer) => {
      const text = data.toString();
      console.error('[C++]', text);
      this.onStderr(text);
    });

    this.process.on('error', (err: NodeJS.ErrnoException) => {
      console.error('Simulator process error:', err.message);
      this.onError(new Error(
        err.code === 'ENOENT'
          ? `Binary not found at: ${binPath}`
          : `Spawn failed: ${err.message}`
      ));
    });

    this.process.on('exit', (code: number | null) => {
      this.onExit(code);
      if (this.process === child) this.process = null;
    });
  }

  getBinPath(): string | null {
    try {
      return resolveBinPath(runtimeLocation);
    } catch {
      return null;
    }
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
    const pid = this.process?.pid ?? null;
    console.log(`[SIM-MGR] kill() | pid=${pid}`);
    if (this.process) {
      this.process.kill('SIGTERM');
      this.process = null;
    }
  }

  isRunning(): boolean {
    return this.process !== null;
  }
}
