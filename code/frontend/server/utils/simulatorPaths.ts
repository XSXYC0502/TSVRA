import fs from 'node:fs';
import path from 'node:path';

interface RuntimeLocation {
  cwd?: string;
  moduleDirectory?: string;
  env?: Record<string, string | undefined>;
  platform?: NodeJS.Platform;
}

const WINDOWS_CONFIGS = ['Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel'];

function ancestors(directory: string): string[] {
  const directories: string[] = [];
  let current = path.resolve(directory);
  while (true) {
    directories.push(current);
    const parent = path.dirname(current);
    if (parent === current) return directories;
    current = parent;
  }
}

export function resolveSimulationRoot(location: RuntimeLocation = {}): string {
  const cwd = location.cwd ?? process.cwd();
  const env = location.env ?? process.env;
  if (env.TSVRA_ROOT?.trim()) return path.resolve(cwd, env.TSVRA_ROOT.trim());

  // Nitro bundles this module beneath .output, so inspect ancestors rather
  // than assuming a fixed distance from the original server/utils directory.
  const starts = [cwd];
  if (location.moduleDirectory) starts.push(location.moduleDirectory);
  if (env.TSVRA_BIN?.trim()) starts.push(path.dirname(path.resolve(cwd, env.TSVRA_BIN.trim())));
  const candidates = [...new Set(starts.flatMap(ancestors))];
  return candidates.find(directory => fs.existsSync(path.join(directory, 'code/config/power_model.ini'))) ??
    candidates.find(directory => fs.existsSync(path.join(directory, 'code/backend/CMakeLists.txt'))) ??
    path.resolve(cwd);
}

export function getCandidateBinPaths(location: RuntimeLocation = {}): string[] {
  const cwd = location.cwd ?? process.cwd();
  const env = location.env ?? process.env;
  const root = resolveSimulationRoot(location);
  const executableNames = (location.platform ?? process.platform) === 'win32'
    ? ['tsvra.exe', 'tsvra'] : ['tsvra', 'tsvra.exe'];
  const buildDirectories = [
    path.join(root, 'code/build'),
    path.join(root, 'code/build/windows-msvc'),
    path.join(root, 'code/build/mingw'),
  ];
  const candidateDirs = buildDirectories.flatMap(directory => [
    path.join(directory, 'bin'),
    ...WINDOWS_CONFIGS.flatMap(config => [
      path.join(directory, 'bin', config),
      path.join(directory, config, 'bin'),
      path.join(directory, config),
    ]),
  ]);
  const candidates = env.TSVRA_BIN?.trim() ? [path.resolve(cwd, env.TSVRA_BIN.trim())] : [];
  for (const directory of candidateDirs) {
    for (const name of executableNames) candidates.push(path.join(directory, name));
  }
  return [...new Set(candidates)];
}

export function resolveBinPath(location: RuntimeLocation = {}): string {
  const candidates = getCandidateBinPaths(location);
  for (const candidate of candidates) {
    if (fs.existsSync(candidate) && fs.statSync(candidate).isFile()) return candidate;
  }
  throw new Error(`tsvra binary not found. Set TSVRA_BIN or build the project first. Searched:\n${candidates.join('\n')}`);
}
