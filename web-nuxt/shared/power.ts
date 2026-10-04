import type { PowerSample, PowerStats } from '../types/simulation';

export const CHART_HISTORY_LIMIT = 500;

export function appendHistory<T>(history: T[], entries: T[]): T[] {
  return [...history, ...entries].slice(-CHART_HISTORY_LIMIT);
}

// pJ / seconds / 1e9 = mW. Only simulation time is used.
export function powerSample(power: PowerStats, previous?: PowerStats): PowerSample {
  const seconds = power.elapsedSeconds - (previous?.elapsedSeconds ?? 0);
  const deltaMw = (energy: number, before: number) => seconds > 0 ? Math.max(0, energy - before) / seconds / 1e9 : 0;
  const routerMw = deltaMw(power.routerEnergyPj, previous?.routerEnergyPj ?? 0);
  return {
    elapsedSeconds: power.elapsedSeconds,
    average: {
      routerStatic: power.routerStaticMw,
      routerDynamic: power.routerDynamicMw,
      horizontal: power.horizontalMw,
      vertical: power.verticalMw,
      total: power.totalMw,
    },
    interval: {
      routerStatic: power.routerStaticMw,
      routerDynamic: Math.max(0, routerMw - power.routerStaticMw),
      horizontal: deltaMw(power.horizontalEnergyPj, previous?.horizontalEnergyPj ?? 0),
      vertical: deltaMw(power.verticalEnergyPj, previous?.verticalEnergyPj ?? 0),
      total: deltaMw(power.totalEnergyPj, previous?.totalEnergyPj ?? 0),
    },
    energyPj: power.totalEnergyPj,
  };
}
