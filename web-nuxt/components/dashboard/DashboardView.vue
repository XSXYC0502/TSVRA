<script setup lang="ts">
import { computed } from 'vue';
import { storeToRefs } from 'pinia';
import { useSimulationStore } from '~/stores/simulation';
import { formatNumber, formatPercent, formatLatency } from '~/utils/formatters';
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  BarElement,
  LineElement,
  PointElement,
  ArcElement,
  Filler,
  Tooltip,
  Legend,
} from 'chart.js';
import { Bar, Line, Doughnut } from 'vue-chartjs';

ChartJS.register(CategoryScale, LinearScale, BarElement, LineElement, PointElement, ArcElement, Filler, Tooltip, Legend);

const store = useSimulationStore();
const {
  stats, completedCount, failedCount, latencySamples,
  throughputTimeline, summary, simStatus, currentCycle, totalCycles
} = storeToRefs(store);

const resolved = computed(() => completedCount.value + failedCount.value);
const successRate = computed(() => resolved.value > 0 ? (completedCount.value / resolved.value) * 100 : 0);
const avgLatency = computed(() => stats.value?.avgLatency ?? summary.value?.avgLatency ?? 0);
const failedTSVs = computed(() => stats.value?.failedTSVs ?? 0);

// Chart.js tooltip style
const tooltipStyle = {
  backgroundColor: '#1f2937',
  borderColor: '#374151',
  borderWidth: 1,
  titleFont: { size: 12 },
  bodyFont: { size: 12 },
};

// Latency histogram
const latencyChartData = computed(() => {
  const samples = latencySamples.value;
  if (samples.length === 0) return null;

  const min = Math.min(...samples);
  const max = Math.max(...samples);
  if (min === max) {
    return {
      labels: [String(min)],
      datasets: [{ label: 'Count', data: [samples.length], backgroundColor: '#3b82f6', borderRadius: 2 }],
    };
  }

  const numBins = 20;
  const binSize = (max - min) / numBins;
  const bins = Array.from({ length: numBins }, (_, i) => ({
    range: `${Math.round(min + i * binSize)}`,
    count: 0,
  }));
  for (const s of samples) {
    const idx = Math.min(Math.floor((s - min) / binSize), numBins - 1);
    bins[idx].count++;
  }

  return {
    labels: bins.map(b => b.range),
    datasets: [{
      label: 'Count',
      data: bins.map(b => b.count),
      backgroundColor: '#3b82f6',
      borderRadius: 2,
    }],
  };
});

const latencyChartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  plugins: { legend: { display: false }, tooltip: tooltipStyle },
  scales: {
    x: {
      ticks: { color: '#9ca3af', font: { size: 10 }, maxTicksLimit: 10 },
      grid: { color: '#374151', drawBorder: false },
    },
    y: {
      ticks: { color: '#9ca3af', font: { size: 10 } },
      grid: { color: '#374151', drawBorder: false },
    },
  },
};

// Throughput chart
const throughputChartData = computed(() => {
  const data = throughputTimeline.value;
  const displayData = data.length > 100 ? data.slice(-100) : data;
  if (displayData.length === 0) return null;

  return {
    labels: displayData.map(d => formatNumber(d.cycle)),
    datasets: [{
      label: 'Completed',
      data: displayData.map(d => d.value),
      borderColor: '#10b981',
      backgroundColor: 'rgba(16, 185, 129, 0.2)',
      fill: true,
      pointRadius: 0,
      borderWidth: 2,
      tension: 0.3,
    }],
  };
});

const throughputChartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  plugins: {
    legend: { display: false },
    tooltip: {
      ...tooltipStyle,
      callbacks: {
        title: (items: any[]) => `Cycle ${items[0]?.label ?? ''}`,
      },
    },
  },
  scales: {
    x: {
      ticks: { color: '#9ca3af', font: { size: 10 }, maxTicksLimit: 8 },
      grid: { color: '#374151', drawBorder: false },
    },
    y: {
      ticks: { color: '#9ca3af', font: { size: 10 } },
      grid: { color: '#374151', drawBorder: false },
    },
  },
};

// Status pie chart
const pieChartData = computed(() => {
  const t = completedCount.value + failedCount.value;
  if (t === 0) return null;
  return {
    labels: ['Completed', 'Failed'],
    datasets: [{
      data: [completedCount.value, failedCount.value],
      backgroundColor: ['#10b981', '#ef4444'],
      borderWidth: 0,
    }],
  };
});

const pieChartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  plugins: {
    tooltip: tooltipStyle,
    legend: { display: false },
  },
};
</script>

<template>
  <div v-if="simStatus === 'idle' || simStatus === 'configuring'" class="flex items-center justify-center h-full text-gray-500">
    Configure and start a simulation to see the dashboard
  </div>
  <div v-else class="p-4 h-full overflow-y-auto space-y-4">
    <div class="flex items-center justify-between">
      <h2 class="text-lg font-bold text-gray-200">
        {{ simStatus === 'done' ? 'Simulation Complete' : 'Simulation Dashboard' }}
      </h2>
      <span v-if="totalCycles > 0" class="text-sm text-gray-400">
        Progress: {{ Math.min((currentCycle / totalCycles) * 100, 100).toFixed(1) }}%
      </span>
    </div>

    <!-- KPI Cards -->
    <div class="grid grid-cols-4 gap-4">
      <div class="bg-gray-800/50 rounded-lg p-4 border border-gray-700/50">
        <p class="text-xs text-gray-400 mb-1">Resolved Requests</p>
        <p class="text-2xl font-bold text-blue-400">{{ formatNumber(resolved) }}</p>
        <p v-if="stats" class="text-xs text-gray-500 mt-1">{{ stats.pending }} pending, {{ stats.transmitting }} transmitting</p>
      </div>
      <div class="bg-gray-800/50 rounded-lg p-4 border border-gray-700/50">
        <p class="text-xs text-gray-400 mb-1">Success Rate</p>
        <p :class="['text-2xl font-bold', successRate > 99 ? 'text-green-400' : successRate > 95 ? 'text-yellow-400' : 'text-red-400']">
          {{ formatPercent(successRate) }}
        </p>
        <p v-if="failedCount > 0" class="text-xs text-gray-500 mt-1">{{ failedCount }} failed</p>
      </div>
      <div class="bg-gray-800/50 rounded-lg p-4 border border-gray-700/50">
        <p class="text-xs text-gray-400 mb-1">Avg Latency</p>
        <p class="text-2xl font-bold text-purple-400">{{ formatLatency(avgLatency) }}</p>
      </div>
      <div class="bg-gray-800/50 rounded-lg p-4 border border-gray-700/50">
        <p class="text-xs text-gray-400 mb-1">Failed TSVs</p>
        <p :class="['text-2xl font-bold', failedTSVs > 0 ? 'text-red-400' : 'text-green-400']">
          {{ failedTSVs }}
        </p>
        <p v-if="stats" class="text-xs text-gray-500 mt-1">{{ stats.redundantUsages }} redundant usages</p>
      </div>
    </div>

    <!-- Charts Row 1 -->
    <div class="grid grid-cols-2 gap-4">
      <div class="bg-gray-800/30 rounded-lg p-4 border border-gray-700/30">
        <h3 class="text-sm font-medium text-gray-300 mb-2">Latency Distribution</h3>
        <div v-if="latencyChartData" style="height: 200px">
          <Bar :data="latencyChartData" :options="latencyChartOptions" />
        </div>
        <div v-else class="text-xs text-gray-500 text-center py-8">No data yet</div>
      </div>
      <div class="bg-gray-800/30 rounded-lg p-4 border border-gray-700/30">
        <h3 class="text-sm font-medium text-gray-300 mb-2">Throughput Over Time</h3>
        <div v-if="throughputChartData" style="height: 200px">
          <Line :data="throughputChartData" :options="throughputChartOptions" />
        </div>
        <div v-else class="text-xs text-gray-500 text-center py-8">No data yet</div>
      </div>
    </div>

    <!-- Charts Row 2 -->
    <div class="grid grid-cols-2 gap-4">
      <div class="bg-gray-800/30 rounded-lg p-4 border border-gray-700/30">
        <h3 class="text-sm font-medium text-gray-300 mb-2">Request Status</h3>
        <div v-if="pieChartData" style="height: 200px">
          <Doughnut :data="pieChartData" :options="pieChartOptions" />
        </div>
        <div v-else class="text-xs text-gray-500 text-center py-8">No data yet</div>
      </div>
      <div class="bg-gray-800/30 rounded-lg p-4 border border-gray-700/30">
        <h3 class="text-sm font-medium text-gray-300 mb-2">Summary</h3>
        <div v-if="summary" class="space-y-2 text-sm">
          <div class="flex justify-between"><span class="text-gray-400">Total Requests</span><span>{{ summary.totalRequests.toLocaleString() }}</span></div>
          <div class="flex justify-between"><span class="text-gray-400">Completed</span><span class="text-green-400">{{ summary.completed.toLocaleString() }}</span></div>
          <div class="flex justify-between"><span class="text-gray-400">Failed</span><span class="text-red-400">{{ summary.failed }}</span></div>
          <div class="flex justify-between"><span class="text-gray-400">Success Rate</span><span>{{ summary.successRate.toFixed(2) }}%</span></div>
          <div class="flex justify-between"><span class="text-gray-400">Avg Latency</span><span>{{ summary.avgLatency.toFixed(1) }}</span></div>
          <div class="flex justify-between"><span class="text-gray-400">Max Latency</span><span>{{ summary.maxLatency.toLocaleString() }}</span></div>
          <div class="flex justify-between"><span class="text-gray-400">Min Latency</span><span>{{ summary.minLatency.toLocaleString() }}</span></div>
        </div>
        <div v-else class="text-xs text-gray-500 text-center py-8">Simulation in progress...</div>
      </div>
    </div>
  </div>
</template>
