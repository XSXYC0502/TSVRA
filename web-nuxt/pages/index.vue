<script setup lang="ts">
import { onMounted, onUnmounted, computed, ref } from 'vue';
import { storeToRefs } from 'pinia';
import { useSimulationStore } from '~/stores/simulation';
import { useWebSocket } from '~/composables/useWebSocket';
import TheHeader from '~/components/layout/TheHeader.vue';
import ViewSwitcher from '~/components/layout/ViewSwitcher.vue';
import ControlPanel from '~/components/layout/ControlPanel.vue';
import DashboardView from '~/components/dashboard/DashboardView.vue';
import Grid3DView from '~/components/grid3d/Grid3DView.vue';
import HeatmapView from '~/components/heatmap/HeatmapView.vue';
import FailureView from '~/components/failure/FailureView.vue';

const store = useSimulationStore();
const { activeView, error, simStatus, lastCycleTime, stderrLines, binaryAvailable } = storeToRefs(store);
const { connect, disconnect, start: wsStart } = useWebSocket();

const showStderr = ref(false);

// Stale data detection: no cycle update for >5s while running
const staleData = ref(false);
let staleTimer: ReturnType<typeof setInterval> | null = null;

function checkStale() {
  if (simStatus.value === 'running' && lastCycleTime.value > 0) {
    staleData.value = Date.now() - lastCycleTime.value > 5000;
  } else {
    staleData.value = false;
  }
}

// Loading state: running but no data received yet
const isLoading = computed(() => {
  return simStatus.value === 'running' && lastCycleTime.value === 0;
});

onMounted(() => {
  connect();
  staleTimer = setInterval(checkStale, 1000);
});

onUnmounted(() => {
  disconnect();
  if (staleTimer) clearInterval(staleTimer);
});

function retryStart() {
  store.clearError();
}
</script>

<template>
  <div class="flex flex-col h-screen">
    <TheHeader />

    <!-- Error banner -->
    <div v-if="error" class="bg-red-900/80 border-b border-red-700 px-4 py-3 flex items-center gap-3">
      <svg class="w-5 h-5 text-red-400 shrink-0" fill="currentColor" viewBox="0 0 20 20">
        <path fill-rule="evenodd" d="M10 18a8 8 0 100-16 8 8 0 000 16zM8.707 7.293a1 1 0 00-1.414 1.414L8.586 10l-1.293 1.293a1 1 0 101.414 1.414L10 11.414l1.293 1.293a1 1 0 001.414-1.414L11.414 10l1.293-1.293a1 1 0 00-1.414-1.414L10 8.586 8.707 7.293z" clip-rule="evenodd" />
      </svg>
      <div class="flex-1 text-sm">
        <span class="font-medium text-red-300">Error: </span>
        <span class="text-red-200">{{ error.message }}</span>
        <span class="text-red-400 text-xs ml-2">({{ error.code }})</span>
      </div>
      <button
        class="px-3 py-1 bg-red-700 hover:bg-red-600 rounded text-xs font-medium text-red-100"
        @click="retryStart"
      >
        Dismiss
      </button>
    </div>

    <!-- Binary unavailable warning -->
    <div v-if="binaryAvailable === false && !error" class="bg-yellow-900/80 border-b border-yellow-700 px-4 py-2 flex items-center gap-3">
      <svg class="w-5 h-5 text-yellow-400 shrink-0" fill="currentColor" viewBox="0 0 20 20">
        <path fill-rule="evenodd" d="M8.257 3.099c.765-1.36 2.722-1.36 3.486 0l5.58 9.92c.75 1.334-.213 2.98-1.742 2.98H4.42c-1.53 0-2.493-1.646-1.743-2.98l5.58-9.92zM11 13a1 1 0 11-2 0 1 1 0 012 0zm-1-8a1 1 0 00-1 1v3a1 1 0 002 0V6a1 1 0 00-1-1z" clip-rule="evenodd" />
      </svg>
      <span class="text-sm text-yellow-200">tsvra binary not found. Build the C++ project first. On Windows/MSVC use <code class="bg-yellow-800 px-1 rounded">cmake --preset windows-msvc</code> then <code class="bg-yellow-800 px-1 rounded">cmake --build --preset windows-msvc-release</code>. On other platforms use <code class="bg-yellow-800 px-1 rounded">cmake -S . -B build</code> and <code class="bg-yellow-800 px-1 rounded">cmake --build build --config Release</code>.</span>
    </div>

    <!-- Loading indicator -->
    <div v-if="isLoading" class="bg-blue-900/60 border-b border-blue-700 px-4 py-2 flex items-center gap-3">
      <svg class="animate-spin w-4 h-4 text-blue-400" fill="none" viewBox="0 0 24 24">
        <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4" />
        <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4z" />
      </svg>
      <span class="text-sm text-blue-200">Starting simulation...</span>
    </div>

    <!-- Stale data warning -->
    <div v-if="staleData && !isLoading" class="bg-yellow-900/40 border-b border-yellow-800 px-4 py-1.5 flex items-center gap-2">
      <svg class="animate-pulse w-4 h-4 text-yellow-500" fill="currentColor" viewBox="0 0 20 20">
        <path fill-rule="evenodd" d="M10 18a8 8 0 100-16 8 8 0 000 16zm1-12a1 1 0 10-2 0v4a1 1 0 00.293.707l2.828 2.829a1 1 0 101.415-1.415L11 9.586V6z" clip-rule="evenodd" />
      </svg>
      <span class="text-xs text-yellow-300">Waiting for data...</span>
    </div>

    <ViewSwitcher />
    <div class="flex flex-1 overflow-hidden min-h-0">
      <main
        class="flex-1 overflow-hidden"
        :class="activeView === 'quad' ? 'grid grid-cols-2 grid-rows-2 gap-1 p-1' : 'relative'"
      >
        <div
          v-show="activeView === 'dashboard' || activeView === 'quad'"
          :class="activeView === 'quad' ? 'bg-gray-900 rounded overflow-hidden border border-gray-800 relative' : 'absolute inset-0'"
        >
          <DashboardView />
        </div>
        <div
          v-show="activeView === 'grid3d' || activeView === 'quad'"
          :class="activeView === 'quad' ? 'bg-gray-900 rounded overflow-hidden border border-gray-800 relative' : 'absolute inset-0'"
        >
          <Grid3DView />
        </div>
        <div
          v-show="activeView === 'heatmap' || activeView === 'quad'"
          :class="activeView === 'quad' ? 'bg-gray-900 rounded overflow-hidden border border-gray-800 relative' : 'absolute inset-0'"
        >
          <HeatmapView />
        </div>
        <div
          v-show="activeView === 'failure' || activeView === 'quad'"
          :class="activeView === 'quad' ? 'bg-gray-900 rounded overflow-hidden border border-gray-800 relative' : 'absolute inset-0'"
        >
          <FailureView />
        </div>
      </main>
      <ControlPanel />
    </div>

    <!-- Stderr debug panel (collapsible) -->
    <div v-if="stderrLines.length > 0" class="border-t border-gray-800">
      <button
        class="w-full px-4 py-1.5 bg-gray-900 hover:bg-gray-800 text-xs text-gray-400 flex items-center gap-2"
        @click="showStderr = !showStderr"
      >
        <svg
          :class="['w-3 h-3 transition-transform', showStderr ? 'rotate-90' : '']"
          fill="currentColor" viewBox="0 0 20 20"
        >
          <path fill-rule="evenodd" d="M7.293 14.707a1 1 0 010-1.414L10.586 10 7.293 6.707a1 1 0 011.414-1.414l4 4a1 1 0 010 1.414l-4 4a1 1 0 01-1.414 0z" clip-rule="evenodd" />
        </svg>
        C++ Debug Output ({{ stderrLines.length }} lines)
      </button>
      <div v-if="showStderr" class="bg-gray-950 max-h-40 overflow-y-auto px-4 py-2 font-mono text-xs text-gray-500">
        <div v-for="(line, i) in stderrLines" :key="i">{{ line }}</div>
      </div>
    </div>
  </div>
</template>
