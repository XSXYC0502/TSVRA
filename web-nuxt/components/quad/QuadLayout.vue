<script setup lang="ts">
import DashboardView from '~/components/dashboard/DashboardView.vue';
import Grid3DView from '~/components/grid3d/Grid3DView.vue';
import HeatmapView from '~/components/heatmap/HeatmapView.vue';
import FailureView from '~/components/failure/FailureView.vue';
</script>

<template>
  <div class="grid grid-cols-2 grid-rows-2 gap-1 h-full p-1">
    <div class="bg-gray-900 rounded overflow-hidden border border-gray-800 relative">
      <DashboardView />
    </div>
    <div class="bg-gray-900 rounded overflow-hidden border border-gray-800 relative">
      <Grid3DView />
    </div>
    <div class="bg-gray-900 rounded overflow-hidden border border-gray-800 relative">
      <HeatmapView />
    </div>
    <div class="bg-gray-900 rounded overflow-hidden border border-gray-800 relative">
      <FailureView />
    </div>
  </div>
</template>
