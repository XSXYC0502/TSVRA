import { ChildProcess, spawn } from 'child_process';
import { createInterface } from 'readline';
import path from 'path';
import fs from 'fs';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const WINDOWS_CONFIG_DIRS = ['Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel'];

function getCandidateBinPaths(): string[] {
  const executableNames = process.platform === 'win32'
    ? ['tsvra.exe', 'tsvra']
    : ['tsvra', 'tsvra.exe'];

  const candidateDirs = [
    path.resolve(__dirname, '../../../bin'),
    path.resolve(process.cwd(), '../bin'),
    path.resolve(process.cwd(), 'bin'),
    path.resolve(__dirname, '../../../build'),
    path.resolve(process.cwd(), '../build'),
    path.resolve(process.cwd(), 'build'),
    ...WINDOWS_CONFIG_DIRS.flatMap((config) => [
      path.resolve(__dirname, `../../../bin/${config}`),
      path.resolve(__dirname, `../../../build/${config}`),
      path.resolve(process.cwd(), `../bin/${config}`),
      path.resolve(process.cwd(), `../build/${config}`),
      path.resolve(process.cwd(), `bin/${config}`),
      path.resolve(process.cwd(), `build/${config}`),
    ]),
  ];

  const explicitBin = process.env.TSVRA_BIN?.trim();
  const candidates = explicitBin ? [path.resolve(explicitBin)] : [];

  for (const dir of candidateDirs) {
    for (const executableName of executableNames) {
      candidates.push(path.join(dir, executableName));
    }
  }

  return [...new Set(candidates)];
}

function resolveBinPath(): string {
  const candidates = getCandidateBinPaths();
  for (const candidate of candidates) {
    if (fs.existsSync(candidate) && fs.statSync(candidate).isFile()) {
      return candidate;
    }
  }
  throw new Error(`tsvra binary not found. Set TSVRA_BIN or build the project first. Searched:\n${candidates.join('\n')}`);
}

export type SimConfig = {
  numLayers: number;
  gridFactor: number;
  failureMode: 'a' | 'b' | 'c';
  failureRate: number;
  verticalDelay: number;
  horizontalDelay: number;
  cycles: number;
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

  start(config: SimConfig): void {
    if (this.process) {
      this.kill();
    }

    let binPath: string;
    try {
      binPath = resolveBinPath();
    } catch (err) {
      this.onError(err as Error);
      return;
    }

    const args = [
      '--json-stream',
      '--layers', String(config.numLayers),
      '--grid-factor', String(config.gridFactor),
      '--failure-mode', config.failureMode,
      '--failure-rate', String(config.failureRate),
      '--vertical-delay', String(config.verticalDelay),
      '--horizontal-delay', String(config.horizontalDelay),
      '--cycles', String(config.cycles),
    ];

    if (config.seed !== undefined) args.push('--seed', String(config.seed));
    if (config.lambda1 !== undefined) args.push('--lambda1', String(config.lambda1));
    if (config.lambda2 !== undefined) args.push('--lambda2', String(config.lambda2));
    if (config.maxPathLength !== undefined) args.push('--max-path-length', String(config.maxPathLength));
    if (config.reliabilityMin !== undefined) args.push('--reliability-min', String(config.reliabilityMin));

    console.log(`[SimulationManager] Spawning: ${binPath} ${args.join(' ')}`);

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
      this.process = null;
    });
  }

  getBinPath(): string | null {
    try {
      return resolveBinPath();
    } catch {
      return null;
    }
  }

  sendCommand(cmd: string): void {
    const writable = this.process?.stdin?.writable ?? false;
    const pid = this.process?.pid ?? null;
    console.log(`[SIM-MGR] sendCommand('${cmd}') | pid=${pid} writable=${writable}`);
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
