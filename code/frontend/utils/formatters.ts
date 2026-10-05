export function formatNumber(n: number): string {
  if (n >= 1_000_000) return (n / 1_000_000).toFixed(1) + 'M';
  if (n >= 1_000) return (n / 1_000).toFixed(1) + 'K';
  return n.toLocaleString();
}

export function formatCycle(cycle: number, total: number): string {
  return `${formatNumber(cycle)} / ${formatNumber(total)}`;
}

export function formatPercent(value: number): string {
  return value.toFixed(2) + '%';
}

export function formatLatency(latency: number): string {
  return latency.toFixed(1) + ' cycles';
}

export function formatPower(mw: number): string {
  if (!Number.isFinite(mw)) return 'N/A';
  return `${mw.toLocaleString(undefined, { maximumFractionDigits: 3 })} mW`;
}

export function formatEnergy(pj: number | null | undefined): string {
  if (pj == null || !Number.isFinite(pj)) return 'N/A';
  if (Math.abs(pj) >= 1e9) return `${(pj / 1e9).toFixed(3)} mJ`;
  if (Math.abs(pj) >= 1e6) return `${(pj / 1e6).toFixed(3)} µJ`;
  if (Math.abs(pj) >= 1e3) return `${(pj / 1e3).toFixed(3)} nJ`;
  return `${pj.toFixed(3)} pJ`;
}

export function formatSimulationTime(seconds: number): string {
  if (seconds >= 1) return `${seconds.toFixed(3)} s`;
  if (seconds >= 1e-3) return `${(seconds * 1e3).toFixed(3)} ms`;
  if (seconds >= 1e-6) return `${(seconds * 1e6).toFixed(3)} µs`;
  return `${(seconds * 1e9).toFixed(3)} ns`;
}
