<script setup lang="ts">
import { computed, reactive, ref, watch } from 'vue';
import { storeToRefs } from 'pinia';
import { useSimulationStore } from '~/stores/simulation';
import { useWebSocket } from '~/composables/useWebSocket';
import { defaultLaunchConfig, parseHotspotCenters, validateLaunchConfig } from '~/shared/simulationConfig';
import type { LaunchConfig } from '~/types/simulation';

const store = useSimulationStore();
const { wsConnected, binaryAvailable } = storeToRefs(store);
const ws = useWebSocket();
const config = reactive<LaunchConfig>(defaultLaunchConfig());
const centerMode = ref<'automatic' | 'manual'>('automatic');
const centerText = ref('');
const attempted = ref(false);

type NumericKey = 'numLayers' | 'gridFactor' | 'verticalDelay' | 'horizontalDelay' | 'routerDelay' | 'cycles' | 'seed' | 'requestProbability';
const fields: { key: NumericKey; label: string; min: number; max: number; step?: number }[] = [
  { key: 'numLayers', label: 'Layers', min: 2, max: 10 },
  { key: 'gridFactor', label: 'Grid Factor (side = 4 × factor)', min: 3, max: 16 },
  { key: 'verticalDelay', label: 'Vertical Delay (cycles / hop)', min: 1, max: 1000000000 },
  { key: 'horizontalDelay', label: 'Horizontal Delay (cycles / hop)', min: 1, max: 1000000000 },
  { key: 'routerDelay', label: 'Router Delay (cycles / hop)', min: 0, max: 1000000000 },
  { key: 'requestProbability', label: 'Request Probability (per cycle)', min: 0, max: 1, step: 0.01 },
  { key: 'cycles', label: 'Cycles', min: 1, max: 1000000000 },
  { key: 'seed', label: 'Seed (0 = random)', min: 0, max: 4294967295 },
];

function updateNumber(key: NumericKey, event: Event) {
  if (event.target instanceof HTMLInputElement) {
    config[key] = event.target.value === '' ? NaN : Number(event.target.value);
  }
}

function updateHeatmapInterval(event: Event) {
  if (event.target instanceof HTMLInputElement) store.setHeatmapUpdateInterval(Number(event.target.value));
}

const validation = computed(() => {
  const manual = config.failureModel.type === 'hotspot' && centerMode.value === 'manual';
  const centers = manual ? parseHotspotCenters(centerText.value) : [];
  if (centers === null || (manual && centers.length === 0)) {
    return { ok: false as const, errors: ['Enter manual centers as x,y,z; x,y,z, one center per hotspot.'] };
  }
  return validateLaunchConfig({ ...config, failureModel: { ...config.failureModel, centers } });
});

function handleStart() {
  attempted.value = true;
  if (validation.value.ok) ws.start(validation.value.config);
}

watch(
  () => [config.numLayers, config.gridFactor, config.redundancy],
  () => {
    if (store.simStatus === 'idle' && validateLaunchConfig({ numLayers: config.numLayers, gridFactor: config.gridFactor, redundancy: config.redundancy }).ok) {
      store.initializePreview(config.gridFactor, config.numLayers, config.redundancy);
    }
  }
);
</script>

<template>
  <form class="space-y-3" @submit.prevent="handleStart">
    <h3 class="font-medium text-gray-300">Configuration</h3>

    <div v-for="field in fields" :key="field.key">
      <label :for="field.key" class="text-xs text-gray-400 block mb-0.5">{{ field.label }}</label>
      <input
        :id="field.key"
        type="number"
        :value="config[field.key]"
        :min="field.key === 'gridFactor' && config.redundancy !== 'shared' ? 1 : field.min"
        :max="field.max"
        :step="field.step ?? 1"
        required
        class="w-full bg-gray-800 border border-gray-700 rounded px-2 py-1 text-sm focus:border-blue-500 focus:outline-none"
        @input="updateNumber(field.key, $event)"
      >
    </div>
    <p class="text-xs text-gray-500">Each request carries one 64-bit flit. Request probability is overall per cycle. Power uses simulated time at 1 GHz.</p>

    <div>
      <label for="redundancy" class="text-xs text-gray-400 block mb-0.5">Redundancy Layout</label>
      <select id="redundancy" v-model="config.redundancy" class="w-full bg-gray-800 border border-gray-700 rounded px-2 py-1 text-sm">
        <option value="shared">Shared Spare</option>
        <option value="corner4">Four Corners</option>
        <option value="none">None</option>
      </select>
    </div>

    <fieldset class="space-y-2 border-t border-gray-800 pt-3">
      <legend class="text-sm text-gray-300 pt-3">Failures</legend>
      <label for="failureMode" class="text-xs text-gray-400 block">Failure Timing</label>
      <select id="failureMode" v-model="config.failureMode" class="w-full bg-gray-800 border border-gray-700 rounded px-2 py-1 text-sm">
        <option value="a">Initial Only</option>
        <option value="b">Runtime Only</option>
        <option value="c">Combined</option>
      </select>
      <div>
        <label for="initialFailureRate" class="text-xs text-gray-400 block mb-0.5">Initial Probability (per channel)</label>
        <input id="initialFailureRate" v-model.number="config.initialFailureRate" type="number" min="0" max="1" step="any" required :disabled="config.failureMode === 'b'" class="w-full bg-gray-800 border border-gray-700 rounded px-2 py-1 text-sm disabled:opacity-50">
      </div>
      <div>
        <label for="runtimeFailureRate" class="text-xs text-gray-400 block mb-0.5">Runtime Probability (per channel / cycle)</label>
        <input id="runtimeFailureRate" v-model.number="config.runtimeFailureRate" type="number" min="0" max="1" step="any" required :disabled="config.failureMode === 'a'" class="w-full bg-gray-800 border border-gray-700 rounded px-2 py-1 text-sm disabled:opacity-50">
      </div>
      <label for="failureDistribution" class="text-xs text-gray-400 block">Spatial Distribution</label>
      <select id="failureDistribution" v-model="config.failureModel.type" class="w-full bg-gray-800 border border-gray-700 rounded px-2 py-1 text-sm">
        <option value="uniform">Uniform</option>
        <option value="hotspot">Fixed Gaussian Hotspots</option>
      </select>
      <p class="text-xs text-gray-500">A failure disables the logical 64-bit vertical channel. Spatial failure hotspots stay fixed and are independent of traffic hotspots.</p>

      <template v-if="config.failureModel.type === 'hotspot'">
        <div>
          <label for="hotspotCount" class="text-xs text-gray-400 block mb-0.5">Hotspot Count</label>
          <input id="hotspotCount" v-model.number="config.failureModel.hotspotCount" type="number" min="1" max="1000" step="1" required class="w-full bg-gray-800 border border-gray-700 rounded px-2 py-1 text-sm">
        </div>
        <div>
          <label for="hotspotRadius" class="text-xs text-gray-400 block mb-0.5">Gaussian Radius (grid units)</label>
          <input id="hotspotRadius" v-model.number="config.failureModel.hotspotRadius" type="number" min="0.000001" step="any" required class="w-full bg-gray-800 border border-gray-700 rounded px-2 py-1 text-sm">
        </div>
        <div>
          <label for="hotspotMultiplier" class="text-xs text-gray-400 block mb-0.5">Peak Probability Multiplier</label>
          <input id="hotspotMultiplier" v-model.number="config.failureModel.hotspotMultiplier" type="number" min="1" step="any" required class="w-full bg-gray-800 border border-gray-700 rounded px-2 py-1 text-sm">
        </div>
        <div>
          <label for="centerMode" class="text-xs text-gray-400 block mb-0.5">Hotspot Centers</label>
          <select id="centerMode" v-model="centerMode" class="w-full bg-gray-800 border border-gray-700 rounded px-2 py-1 text-sm">
            <option value="automatic">Automatic (from seed)</option>
            <option value="manual">Manual Coordinates</option>
          </select>
        </div>
        <div v-if="centerMode === 'manual'">
          <label for="hotspotCenters" class="text-xs text-gray-400 block mb-0.5">Centers: x,y,z; x,y,z</label>
          <textarea id="hotspotCenters" v-model="centerText" rows="2" placeholder="4,4,0" required class="w-full bg-gray-800 border border-gray-700 rounded px-2 py-1 text-sm" />
          <p class="text-xs text-gray-500 mt-1">x/y: 0–{{ config.gridFactor * 4 - 1 }}. z: 0–{{ config.numLayers - 2 }}, the lower layer of the channel. Enter {{ config.failureModel.hotspotCount }} center(s).</p>
        </div>
      </template>
    </fieldset>

    <div>
      <label for="powerConfig" class="text-xs text-gray-400 block mb-0.5">Power Parameters File (optional)</label>
      <input id="powerConfig" v-model="config.powerConfig" type="text" placeholder="Default power parameters" class="w-full bg-gray-800 border border-gray-700 rounded px-2 py-1 text-sm">
      <p class="text-xs text-gray-500 mt-1">Path to an editable INI file on the simulation server.</p>
    </div>
    <div>
      <label for="heatmapUpdate" class="text-xs text-gray-400 block mb-0.5">Heatmap Update (cycles)</label>
      <input id="heatmapUpdate" :value="store.heatmapUpdateInterval" type="number" min="1" step="1" class="w-full bg-gray-800 border border-gray-700 rounded px-2 py-1 text-sm" @input="updateHeatmapInterval">
    </div>

    <ul v-if="attempted && !validation.ok" class="text-xs text-red-300 space-y-1" role="alert">
      <li v-for="message in validation.errors" :key="message">{{ message }}</li>
    </ul>
    <button type="submit" :disabled="!wsConnected || binaryAvailable === false" class="w-full py-2 bg-blue-600 hover:bg-blue-700 disabled:bg-gray-700 disabled:text-gray-500 rounded font-medium text-sm">
      {{ binaryAvailable === false ? 'Binary Not Found' : 'Start Simulation' }}
    </button>
  </form>
</template>
