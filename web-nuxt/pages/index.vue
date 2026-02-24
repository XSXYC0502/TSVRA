<script setup lang="ts">
import { onMounted, onUnmounted } from 'vue';
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
import QuadLayout from '~/components/quad/QuadLayout.vue';

const store = useSimulationStore();
const { activeView } = storeToRefs(store);
const { connect, disconnect } = useWebSocket();

const viewComponents: Record<string, any> = {
  dashboard: DashboardView,
  grid3d: Grid3DView,
  heatmap: HeatmapView,
  failure: FailureView,
  quad: QuadLayout,
};

onMounted(() => {
  connect();
});

onUnmounted(() => {
  disconnect();
});
</script>

<template>
  <div class="flex flex-col h-screen">
    <TheHeader />
    <ViewSwitcher />
    <div class="flex flex-1 overflow-hidden min-h-0">
      <main class="flex-1 overflow-hidden relative">
        <component :is="viewComponents[activeView]" />
      </main>
      <ControlPanel />
    </div>
  </div>
</template>
