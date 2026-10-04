<script setup lang="ts">
import { computed, ref } from 'vue';
import { storeToRefs } from 'pinia';
import { useSimulationStore } from '~/stores/simulation';
import { formatEnergy, formatNumber, formatPower, formatSimulationTime } from '~/utils/formatters';
import { Chart as ChartJS, LinearScale, LineElement, PointElement, Tooltip, Legend } from 'chart.js';
import type { ChartData, ChartOptions } from 'chart.js';
import { Line } from 'vue-chartjs';

ChartJS.register(LinearScale, LineElement, PointElement, Tooltip, Legend);

const store = useSimulationStore();
const { power, powerTimeline, powerModel, config } = storeToRefs(store);
const displayMode = ref<'average' | 'interval'>('average');
const calibrationLabel = computed(() => powerModel.value?.calibrationStatus === 'dsent_bulk45_reference'
  ? 'DSENT 45 nm Bulk reference'
  : 'Custom power coefficients');
const calibrationSource = computed(() => {
  const source = powerModel.value?.parameterSource;
  if (!source) return null;
  try {
    const url = new URL(source);
    return url.protocol === 'https:' ? url.href : null;
  } catch {
    return null;
  }
});
const technologyLabel = computed(() => powerModel.value?.technology?.includes('Bulk') ? 'Bulk' : '');

const components = [
  { key: 'routerStatic', label: 'Router static', color: '#a78bfa' },
  { key: 'routerDynamic', label: 'Router dynamic', color: '#60a5fa' },
  { key: 'horizontal', label: 'Horizontal links', color: '#34d399' },
  { key: 'vertical', label: 'Vertical channels', color: '#fbbf24' },
  { key: 'total', label: 'Total', color: '#f9fafb' },
] as const;

const powerChart = computed<ChartData<'line'>>(() => ({
  datasets: components.map(component => ({
    label: component.label,
    data: powerTimeline.value.map(sample => ({ x: sample.elapsedSeconds * 1e6, y: sample[displayMode.value][component.key] })),
    borderColor: component.color,
    backgroundColor: component.color,
    borderWidth: component.key === 'total' ? 2 : 1.5,
    pointRadius: 0,
    tension: 0,
  })),
}));

const energyUnit = computed(() => (power.value?.totalEnergyPj ?? 0) >= 1e6 ? { label: 'µJ', divisor: 1e6 } : { label: 'nJ', divisor: 1e3 });
const energyChart = computed<ChartData<'line'>>(() => ({
  datasets: [{
    label: `Cumulative energy (${energyUnit.value.label})`,
    data: powerTimeline.value.map(sample => ({ x: sample.elapsedSeconds * 1e6, y: sample.energyPj / energyUnit.value.divisor })),
    borderColor: '#22d3ee',
    backgroundColor: '#22d3ee',
    borderWidth: 2,
    pointRadius: 0,
    tension: 0,
  }],
}));

function chartOptions(yLabel: string): ChartOptions<'line'> {
  return {
    responsive: true,
    maintainAspectRatio: false,
    animation: false,
    interaction: { mode: 'index', intersect: false },
    plugins: {
      legend: { labels: { color: '#9ca3af', boxWidth: 12, font: { size: 10 } } },
      tooltip: {
        backgroundColor: '#1f2937',
        callbacks: { title: items => `Simulation time: ${items[0]?.parsed.x?.toFixed(3) ?? '0'} µs` },
      },
    },
    scales: {
      x: {
        type: 'linear',
        title: { display: true, text: 'Simulation time (µs)', color: '#9ca3af' },
        ticks: { color: '#9ca3af', maxTicksLimit: 6 },
        grid: { color: '#374151' },
      },
      y: {
        min: 0,
        title: { display: true, text: yLabel, color: '#9ca3af' },
        ticks: { color: '#9ca3af', maxTicksLimit: 6 },
        grid: { color: '#374151' },
      },
    },
  };
}

const powerOptions = chartOptions('Power (mW)');
const energyOptions = computed(() => chartOptions(`Energy (${energyUnit.value.label})`));
</script>

<template>
  <section class="space-y-3" aria-label="Power and energy">
    <div class="flex items-center justify-between gap-2">
      <h3 class="text-sm font-medium text-gray-300">Power and Energy</h3>
      <span v-if="config?.actualSeed !== undefined" class="text-xs text-gray-500">Seed: {{ config.actualSeed }}</span>
    </div>
    <div v-if="!power" class="text-xs text-gray-500 py-4">Power data will appear after the first simulation cycle.</div>
    <template v-else>
      <div class="grid grid-cols-2 xl:grid-cols-4 gap-3">
        <div class="bg-gray-800/50 rounded-lg p-3 border border-gray-700/50">
          <p class="text-xs text-gray-400 mb-1">Average Total Power</p>
          <p class="text-lg font-bold text-cyan-400">{{ formatPower(power.totalMw) }}</p>
          <p class="text-xs text-gray-500 mt-1">Current window: {{ formatPower(power.intervalTotalMw) }}</p>
        </div>
        <div class="bg-gray-800/50 rounded-lg p-3 border border-gray-700/50">
          <p class="text-xs text-gray-400 mb-1">Cumulative Energy</p>
          <p class="text-lg font-bold text-cyan-400">{{ formatEnergy(power.totalEnergyPj) }}</p>
        </div>
        <div class="bg-gray-800/50 rounded-lg p-3 border border-gray-700/50">
          <p class="text-xs text-gray-400 mb-1">Energy / Completed Flit</p>
          <p class="text-lg font-bold text-cyan-400">{{ formatEnergy(power.energyPerCompletedFlitPj) }}</p>
          <p v-if="power.energyPerCompletedFlitPj === null" class="text-xs text-gray-500 mt-1">No completed flits yet</p>
        </div>
        <div class="bg-gray-800/50 rounded-lg p-3 border border-gray-700/50">
          <p class="text-xs text-gray-400 mb-1">Simulation Time</p>
          <p class="text-lg font-bold text-cyan-400">{{ formatSimulationTime(power.elapsedSeconds) }}</p>
          <p class="text-xs text-gray-500 mt-1">{{ formatNumber(power.elapsedCycles) }} elapsed cycles</p>
        </div>
      </div>
      <div class="grid grid-cols-1 lg:grid-cols-2 gap-4">
        <div class="bg-gray-800/30 rounded-lg p-3 border border-gray-700/30">
          <div class="flex justify-between items-center gap-2 mb-2">
            <h4 class="text-xs font-medium text-gray-300">Power Components</h4>
            <select v-model="displayMode" aria-label="Power averaging" class="bg-gray-800 border border-gray-700 rounded text-xs px-1 py-0.5">
              <option value="average">Average since start</option>
              <option value="interval">Between samples</option>
            </select>
          </div>
          <div style="height: 220px"><Line :data="powerChart" :options="powerOptions" /></div>
        </div>
        <div class="bg-gray-800/30 rounded-lg p-3 border border-gray-700/30">
          <h4 class="text-xs font-medium text-gray-300 mb-2">Cumulative Energy</h4>
          <div style="height: 220px"><Line :data="energyChart" :options="energyOptions" /></div>
        </div>
      </div>
      <div class="grid grid-cols-2 lg:grid-cols-4 gap-x-4 gap-y-1 text-xs text-gray-400">
        <span>Router static: {{ formatPower(power.routerStaticMw) }}</span>
        <span>Router dynamic: {{ formatPower(power.routerDynamicMw) }}</span>
        <span>Horizontal: {{ formatPower(power.horizontalMw) }}</span>
        <span>Vertical: {{ formatPower(power.verticalMw) }}</span>
        <span>Router events: {{ formatNumber(power.routerEvents) }}</span>
        <span>Horizontal hops: {{ formatNumber(power.horizontalHops) }}</span>
        <span>Vertical hops: {{ formatNumber(power.verticalHops) }}</span>
        <span>Redundant vertical hops: {{ formatNumber(power.redundantVerticalHops) }}</span>
      </div>
      <p class="text-xs text-gray-500">Average power includes router static energy and dynamic energy from executed hops. The charts keep the latest 500 samples. Pauses and display speed do not change simulated time.</p>
      <div v-if="powerModel" class="text-xs text-gray-500 space-y-1">
        <p>
          {{ calibrationLabel }} · {{ powerModel.technologyNm }} nm {{ technologyLabel }} · {{ powerModel.voltageV }} V · {{ powerModel.flitBits }} bits ·
          {{ (powerModel.frequencyHz / 1e9).toFixed(2) }} GHz · toggle probability {{ powerModel.toggleProbability }}.
          <a v-if="calibrationSource" :href="calibrationSource" target="_blank" rel="noopener noreferrer" class="text-cyan-400 hover:underline">Calibration source</a>
        </p>
        <details>
          <summary class="cursor-pointer hover:text-gray-300">Model details</summary>
          <p class="mt-1 break-words">Calibration status: {{ powerModel.calibrationStatus }}</p>
          <p class="break-words">Parameter source: {{ powerModel.parameterSource }}</p>
        </details>
      </div>
    </template>
  </section>
</template>
