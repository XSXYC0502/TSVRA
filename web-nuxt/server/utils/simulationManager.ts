import { ChildProcess, spawn } from 'child_process';
import { createInterface } from 'readline';
import path from 'path';
import fs from 'fs';
import { fileURLToPath } from 'url';
import type { LaunchConfig } from '../../types/simulation';
import { buildSimulatorArgs } from '../../shared/simulationConfig';

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

function resolveSimulationRoot(binPath: string): string {
  const explicitRoot = process.env.TSVRA_ROOT?.trim();
  if (explicitRoot) return path.resolve(explicitRoot);
  // Build and MSVC configuration subdirectories, plus development/preview cwd.
  const candidates = [process.cwd(), path.resolve(process.cwd(), '..')];
  let directory = path.dirname(binPath);
  for (let depth = 0; depth < 4; depth++) {
    candidates.push(directory);
    directory = path.dirname(directory);
  }
  return candidates.find(candidate => fs.existsSync(path.join(candidate, 'power_model.ini'))) ??
    (path.basename(process.cwd()) === 'web-nuxt' ? path.resolve(process.cwd(), '..') : process.cwd());
}

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
      binPath = resolveBinPath();
    } catch (err) {
      this.onError(err as Error);
      return;
    }

    const args = buildSimulatorArgs(config);

    console.log(`[SimulationManager] Spawning: ${binPath} ${args.join(' ')}`);

    const child = spawn(binPath, args, {
      stdio: ['pipe', 'pipe', 'pipe'],
      cwd: resolveSimulationRoot(binPath),
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
      return resolveBinPath();
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
