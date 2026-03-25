<script setup lang="ts">
import { reactive, watch } from 'vue';
import { storeToRefs } from 'pinia';
import { useSimulationStore } from '~/stores/simulation';
import { useWebSocket } from '~/composables/useWebSocket';

const store = useSimulationStore();
const { wsConnected, binaryAvailable } = storeToRefs(store);
const ws = useWebSocket();

const config = reactive({
  numLayers: 2,
  gridFactor: 4,
  failureMode: 'c' as 'a' | 'b' | 'c',
  failureRate: 0.000001,
  verticalDelay: 1,
  horizontalDelay: 1000,
  cycles: 10000,
  seed: 0,
  redundancy: 'shared' as 'shared' | 'none',
});

const fields = [
  { key: 'numLayers', label: 'Layers', type: 'number', min: 2, max: 10 },
  { key: 'gridFactor', label: 'Grid Factor', type: 'number', min: 3, max: 16 },
  { key: 'failureRate', label: 'Failure Rate', type: 'number', step: 0.0001 },
  { key: 'verticalDelay', label: 'Vertical Delay', type: 'number', min: 1 },
  { key: 'horizontalDelay', label: 'Horizontal Delay', type: 'number', min: 1 },
  { key: 'cycles', label: 'Cycles', type: 'number', min: 1000 },
  { key: 'seed', label: 'Seed (0=random)', type: 'number', min: 0 },
] as const;

function handleStart() {
  ws.start({ ...config });
}

watch(
  () => [config.numLayers, config.gridFactor, config.redundancy],
  ([numLayers, gridFactor, redundancy]) => {
    if (store.simStatus === 'idle') {
      store.initializePreview(gridFactor as number, numLayers as number, redundancy as 'shared' | 'none');
    }
  }
);
</script>

<template>
  <div class="space-y-3">
    <h3 class="font-medium text-gray-300">Configuration</h3>

    <div v-for="f in fields" :key="f.key">
      <label class="text-xs text-gray-400 block mb-0.5">{{ f.label }}</label>
      <input
        :type="f.type"
        :value="(config as any)[f.key]"
        :min="'min' in f ? f.min : undefined"
        :max="'max' in f ? f.max : undefined"
        :step="'step' in f ? f.step : undefined"
        class="w-full bg-gray-800 border border-gray-700 rounded px-2 py-1 text-sm focus:border-blue-500 focus:outline-none"
        @input="(e: Event) => { (config as any)[f.key] = Number((e.target as HTMLInputElement).value) }"
      />
    </div>

    <!-- Failure mode -->
    <div>
      <label class="text-xs text-gray-400 block mb-0.5">Failure Mode</label>
      <select
        v-model="config.failureMode"
        class="w-full bg-gray-800 border border-gray-700 rounded px-2 py-1 text-sm focus:border-blue-500 focus:outline-none"
      >
        <option value="a">Initial Only</option>
        <option value="b">Runtime Only</option>
        <option value="c">Combined</option>
      </select>
    </div>

    <!-- Redundancy layout -->
    <div>
      <label class="text-xs text-gray-400 block mb-0.5">Redundancy Layout</label>
      <select
        v-model="config.redundancy"
        class="w-full bg-gray-800 border border-gray-700 rounded px-2 py-1 text-sm focus:border-blue-500 focus:outline-none"
      >
        <option value="shared">Shared Spare</option>
        <option value="none">None</option>
      </select>
    </div>

    <!-- Heatmap update interval -->
    <div>
      <label class="text-xs text-gray-400 block mb-0.5">Heatmap Update (cycles)</label>
      <input
        type="number"
        :value="store.heatmapUpdateInterval"
        min="1"
        class="w-full bg-gray-800 border border-gray-700 rounded px-2 py-1 text-sm focus:border-blue-500 focus:outline-none"
        @input="(e: Event) => { store.setHeatmapUpdateInterval(Number((e.target as HTMLInputElement).value)) }"
      />
    </div>

    <button
      :disabled="!wsConnected || binaryAvailable === false"
      class="w-full py-2 bg-blue-600 hover:bg-blue-700 disabled:bg-gray-700 disabled:text-gray-500 rounded font-medium text-sm"
      @click="handleStart"
    >
      {{ binaryAvailable === false ? 'Binary Not Found' : 'Start Simulation' }}
    </button>
  </div>
</template>
