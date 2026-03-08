<script setup lang="ts">
import { storeToRefs } from 'pinia';
import { useSimulationStore } from '~/stores/simulation';
import { useWebSocket } from '~/composables/useWebSocket';
import SimConfigForm from '~/components/config/SimConfigForm.vue';
import type { SpeedMode } from '~/types/simulation';

const store = useSimulationStore();
const { simStatus, speedMode, stats, summary } = storeToRefs(store);
const ws = useWebSocket();

const speedOptions: { key: SpeedMode; label: string }[] = [
  { key: 'step', label: 'Step' },
  { key: 'slow', label: 'Slow' },
  { key: 'medium', label: 'Medium' },
  { key: 'high', label: 'High' },
  { key: 'auto', label: 'Auto' },
];
</script>

<template>
  <aside class="w-72 bg-gray-900 border-l border-gray-800 overflow-y-auto p-4 flex flex-col gap-4">
    <SimConfigForm v-if="simStatus === 'idle' || simStatus === 'configuring'" />

    <!-- Speed control — visible before and during simulation -->
    <div v-if="simStatus !== 'configuring' && simStatus !== 'done'">
      <label class="text-xs text-gray-400 mb-1 block">Speed</label>
      <div class="flex gap-1">
        <button
          v-for="s in speedOptions"
          :key="s.key"
          :class="[
            'px-2 py-1 rounded text-xs font-medium',
            speedMode === s.key ? 'bg-blue-600 text-white' : 'bg-gray-800 text-gray-400 hover:bg-gray-700'
          ]"
          @click="ws.setSpeed(s.key)"
        >
          {{ s.label }}
        </button>
      </div>
    </div>

    <template v-if="simStatus !== 'idle' && simStatus !== 'configuring'">
      <!-- Control buttons -->
      <div class="flex flex-wrap gap-2">
        <button
          v-if="simStatus === 'running'"
          class="px-3 py-1.5 bg-yellow-600 hover:bg-yellow-700 rounded text-sm font-medium"
          @click="ws.pause()"
        >
          Pause
        </button>
        <button
          v-if="simStatus === 'paused' || simStatus === 'stepping'"
          class="px-3 py-1.5 bg-green-600 hover:bg-green-700 rounded text-sm font-medium"
          @click="ws.resume()"
        >
          Resume
        </button>
        <button
          v-if="simStatus !== 'done'"
          class="px-3 py-1.5 bg-red-600 hover:bg-red-700 rounded text-sm font-medium"
          @click="ws.stop()"
        >
          Stop
        </button>
        <button
          v-if="simStatus === 'paused' || simStatus === 'stepping' || (simStatus === 'running' && speedMode === 'step')"
          class="px-3 py-1.5 bg-blue-600 hover:bg-blue-700 rounded text-sm font-medium"
          @click="ws.step()"
        >
          Next Step
        </button>
        <button
          v-if="simStatus === 'done'"
          class="px-3 py-1.5 bg-gray-600 hover:bg-gray-700 rounded text-sm font-medium"
          @click="store.reset()"
        >
          Reset
        </button>
      </div>

      <!-- Heatmap update interval -->
      <div v-if="simStatus !== 'done'">
        <label class="text-xs text-gray-400 mb-1 block">Heatmap Update (cycles)</label>
        <input
          type="number"
          :value="store.heatmapUpdateInterval"
          min="1"
          class="w-full bg-gray-800 border border-gray-700 rounded px-2 py-1 text-sm focus:border-blue-500 focus:outline-none"
          @input="(e: Event) => { store.setHeatmapUpdateInterval(Number((e.target as HTMLInputElement).value)) }"
        />
      </div>

      <!-- Running stats -->
      <div v-if="stats" class="space-y-1 text-sm">
        <h3 class="font-medium text-gray-300">Running Stats</h3>
        <div class="grid grid-cols-2 gap-1 text-xs">
          <span class="text-gray-500">Pending:</span><span>{{ stats.pending }}</span>
          <span class="text-gray-500">Transmitting:</span><span>{{ stats.transmitting }}</span>
          <span class="text-gray-500">Completed:</span><span>{{ stats.completed.toLocaleString() }}</span>
          <span class="text-gray-500">Failed:</span><span>{{ stats.failed }}</span>
          <span class="text-gray-500">Failed TSVs:</span><span>{{ stats.failedTSVs }}</span>
          <span class="text-gray-500">Avg Latency:</span><span>{{ stats.avgLatency.toFixed(1) }}</span>
        </div>
      </div>

      <!-- Summary when done -->
      <div v-if="summary" class="space-y-1 text-sm">
        <h3 class="font-medium text-green-400">Final Summary</h3>
        <div class="grid grid-cols-2 gap-1 text-xs">
          <span class="text-gray-500">Total:</span><span>{{ summary.totalRequests.toLocaleString() }}</span>
          <span class="text-gray-500">Completed:</span><span>{{ summary.completed.toLocaleString() }}</span>
          <span class="text-gray-500">Failed:</span><span>{{ summary.failed }}</span>
          <span class="text-gray-500">Success Rate:</span><span>{{ summary.successRate.toFixed(2) }}%</span>
          <span class="text-gray-500">Avg Latency:</span><span>{{ summary.avgLatency.toFixed(1) }}</span>
          <span class="text-gray-500">Max Latency:</span><span>{{ summary.maxLatency.toLocaleString() }}</span>
          <span class="text-gray-500">Min Latency:</span><span>{{ summary.minLatency.toLocaleString() }}</span>
        </div>
      </div>
    </template>
  </aside>
</template>
