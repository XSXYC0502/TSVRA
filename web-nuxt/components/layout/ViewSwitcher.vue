<script setup lang="ts">
import { storeToRefs } from 'pinia';
import { useSimulationStore } from '~/stores/simulation';
import type { ViewMode } from '~/types/simulation';

const store = useSimulationStore();
const { activeView } = storeToRefs(store);

const views: { key: ViewMode; label: string }[] = [
  { key: 'dashboard', label: 'Dashboard' },
  { key: 'grid3d', label: '3D Grid' },
  { key: 'heatmap', label: 'Heatmap' },
  { key: 'failure', label: 'Failure Analysis' },
  { key: 'quad', label: 'Quad View' },
];
</script>

<template>
  <div class="flex gap-1 px-4 py-1 bg-gray-900 border-b border-gray-800">
    <button
      v-for="v in views"
      :key="v.key"
      :class="[
        'px-3 py-1.5 rounded text-sm font-medium transition-colors',
        activeView === v.key
          ? 'bg-blue-600 text-white'
          : 'text-gray-400 hover:text-gray-200 hover:bg-gray-800'
      ]"
      @click="store.setActiveView(v.key)"
    >
      {{ v.label }}
    </button>
  </div>
</template>
