<script setup lang="ts">
import { storeToRefs } from 'pinia';
import { useSimulationStore } from '~/stores/simulation';

const store = useSimulationStore();
const { wsConnected, simStatus, currentCycle, totalCycles } = storeToRefs(store);
</script>

<template>
  <header class="flex items-center justify-between px-4 py-2 bg-gray-900 border-b border-gray-800">
    <h1 class="text-lg font-bold tracking-wide">TSVRA Visualization</h1>
    <div class="flex items-center gap-4 text-sm">
      <span
        v-if="simStatus !== 'idle' && simStatus !== 'configuring' && totalCycles > 0"
        class="text-gray-400"
      >
        Cycle {{ currentCycle.toLocaleString() }} / {{ totalCycles.toLocaleString() }}
        ({{ Math.min((currentCycle / totalCycles) * 100, 100).toFixed(1) }}%)
      </span>
      <div class="flex items-center gap-2">
        <div :class="['w-2 h-2 rounded-full', wsConnected ? 'bg-green-500' : 'bg-red-500']" />
        <span class="text-gray-400">{{ wsConnected ? 'Connected' : 'Disconnected' }}</span>
      </div>
      <span :class="[
        'px-2 py-0.5 rounded text-xs font-medium',
        simStatus === 'running' ? 'bg-green-900 text-green-300' :
        simStatus === 'paused' ? 'bg-yellow-900 text-yellow-300' :
        simStatus === 'done' ? 'bg-blue-900 text-blue-300' :
        'bg-gray-800 text-gray-400'
      ]">
        {{ simStatus.toUpperCase() }}
      </span>
    </div>
  </header>
</template>
