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
