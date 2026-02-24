<script setup lang="ts">
import { computed, watchEffect, ref } from 'vue';
import { storeToRefs } from 'pinia';
import { useSimulationStore } from '~/stores/simulation';
import { formatNumber } from '~/utils/formatters';
import * as THREE from 'three';
import type { TSVState, SimConfig, FailureEvent } from '~/types/simulation';
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  BarElement,
  LineElement,
  PointElement,
  Filler,
  Tooltip,
  Legend,
} from 'chart.js';
import { Bar, Line } from 'vue-chartjs';

ChartJS.register(CategoryScale, LinearScale, BarElement, LineElement, PointElement, Filler, Tooltip, Legend);

const store = useSimulationStore();
const { tsvGrid, config, failureTimeline, failedTSVPositions } = storeToRefs(store);

const layerSpacing = 3;
const failureMeshRef = ref<THREE.InstancedMesh | null>(null);

const dummy = new THREE.Object3D();
const tempColor = new THREE.Color();

// Update failure grid instanced mesh
watchEffect(() => {
  const mesh = failureMeshRef.value;
  if (!mesh || !tsvGrid.value || !config.value) return;

  const size = config.value.gridSize;
  const layers = config.value.numLayers;
  const half = size / 2;
  const grid = tsvGrid.value;

  let i = 0;
  for (let z = 0; z < layers; z++) {
    for (let y = 0; y < size; y++) {
      for (let x = 0; x < size; x++) {
        const tsv = grid[z]?.[y]?.[x];
        if (!tsv) { i++; continue; }
        dummy.position.set(x - half + 0.5, z * layerSpacing, y - half + 0.5);
        dummy.scale.set(0.8, 0.3, 0.8);
        dummy.updateMatrix();
        mesh.setMatrixAt(i, dummy.matrix);

        if (tsv.failed) {
          tempColor.setRGB(
            tsv.redundant ? 1 : 1,
            tsv.redundant ? 0.5 : 0.15,
            tsv.redundant ? 0 : 0.15
          );
        } else {
          tempColor.setRGB(0.15, 0.15, 0.2);
        }
        mesh.setColorAt(i, tempColor);
        i++;
      }
    }
  }
  mesh.instanceMatrix.needsUpdate = true;
  if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
});

const totalInstances = computed(() => {
  if (!config.value) return 0;
  return config.value.gridSize * config.value.gridSize * config.value.numLayers;
});

// Failure timeline chart data
const failureTimelineChartData = computed(() => {
  const data = failureTimeline.value;
  if (data.length === 0) return null;

  const sorted = [...data].sort((a, b) => a.cycle - b.cycle);
  let points = sorted.map((f, i) => ({ cycle: f.cycle, cumulative: i + 1 }));
  if (points.length > 200) {
    const step = Math.ceil(points.length / 200);
    points = points.filter((_, i) => i % step === 0 || i === points.length - 1);
  }

  return {
    labels: points.map(p => formatNumber(p.cycle)),
    datasets: [{
      label: 'Cumulative Failures',
      data: points.map(p => p.cumulative),
      borderColor: '#ef4444',
      backgroundColor: 'rgba(239, 68, 68, 0.1)',
      fill: false,
      pointRadius: 0,
      borderWidth: 2,
      tension: 0.1,
    }],
  };
});

const failureTimelineOptions = {
  responsive: true,
  maintainAspectRatio: false,
  plugins: {
    legend: { display: false },
    tooltip: {
      backgroundColor: '#1f2937',
      borderColor: '#374151',
      borderWidth: 1,
      titleFont: { size: 12 },
      bodyFont: { size: 12 },
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

// Redundancy chart data (failures by region)
const redundancyChartData = computed(() => {
  if (!tsvGrid.value || !config.value) return null;

  const numRegions = config.value.gridSize / 4;
  const regions: { region: string; failed: number; failedRedundant: number }[] = [];

  for (let ry = 0; ry < numRegions; ry++) {
    for (let rx = 0; rx < numRegions; rx++) {
      let failedR = 0, totalF = 0;
      for (let z = 0; z < config.value.numLayers; z++) {
        for (let dy = 0; dy < 4; dy++) {
          for (let dx = 0; dx < 4; dx++) {
            const tsv = tsvGrid.value[z]?.[ry * 4 + dy]?.[rx * 4 + dx];
            if (tsv?.failed) { totalF++; if (tsv.redundant) failedR++; }
          }
        }
      }
      if (totalF > 0) regions.push({ region: `(${rx},${ry})`, failed: totalF, failedRedundant: failedR });
    }
  }

  const sorted = regions.sort((a, b) => b.failed - a.failed).slice(0, 20);
  if (sorted.length === 0) return null;

  return {
    labels: sorted.map(r => r.region),
    datasets: [
      {
        label: 'Total Failed',
        data: sorted.map(r => r.failed),
        backgroundColor: '#ef4444',
        borderRadius: 2,
      },
      {
        label: 'Failed Redundant',
        data: sorted.map(r => r.failedRedundant),
        backgroundColor: '#f97316',
        borderRadius: 2,
      },
    ],
  };
});

const redundancyChartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  plugins: {
    tooltip: {
      backgroundColor: '#1f2937',
      borderColor: '#374151',
      borderWidth: 1,
      titleFont: { size: 12 },
      bodyFont: { size: 12 },
    },
    legend: {
      labels: { color: '#9ca3af', font: { size: 10 } },
    },
  },
  scales: {
    x: {
      ticks: { color: '#9ca3af', font: { size: 9 } },
      grid: { color: '#374151', drawBorder: false },
    },
    y: {
      ticks: { color: '#9ca3af', font: { size: 10 } },
      grid: { color: '#374151', drawBorder: false },
    },
  },
};
</script>

<template>
  <div v-if="!tsvGrid || !config" class="flex items-center justify-center h-full text-gray-500">
    Start a simulation to see failure analysis
  </div>
  <div v-else class="grid grid-cols-2 grid-rows-2 gap-2 p-2 h-full">
    <!-- Failure Map (3D) -->
    <div class="bg-gray-800/30 rounded-lg border border-gray-700/30 overflow-hidden relative">
      <div class="px-3 py-2 text-sm font-medium text-gray-300 border-b border-gray-700/30 relative z-10">
        Failure Map ({{ failedTSVPositions.length }} failed TSVs)
      </div>
      <div class="absolute inset-0 top-8">
        <TresCanvas :camera="{ position: [15, 15, 15], fov: 50 }">
          <TresScene>
            <TresAmbientLight :intensity="0.4" />
            <TresDirectionalLight :position="[10, 20, 10]" :intensity="0.6" />

            <TresInstancedMesh
              ref="failureMeshRef"
              :args="[undefined, undefined, totalInstances]"
            >
              <TresBoxGeometry :args="[1, 1, 1]" />
              <TresMeshStandardMaterial vertex-colors :tone-mapped="false" />
            </TresInstancedMesh>

            <OrbitControls :enable-damping="true" :damping-factor="0.1" />
          </TresScene>
        </TresCanvas>
      </div>
    </div>

    <!-- Failure Timeline -->
    <div class="bg-gray-800/30 rounded-lg p-3 border border-gray-700/30">
      <h3 class="text-sm font-medium text-gray-300 mb-2">Failure Timeline</h3>
      <div v-if="failureTimelineChartData" style="height: 180px">
        <Line :data="failureTimelineChartData" :options="failureTimelineOptions" />
      </div>
      <div v-else class="text-xs text-gray-500 text-center py-8">No failures yet</div>
      <div class="mt-2 text-xs text-gray-500">Cumulative TSV failures over simulation cycles</div>
    </div>

    <!-- Failures by Region -->
    <div class="bg-gray-800/30 rounded-lg p-3 border border-gray-700/30 col-span-2">
      <h3 class="text-sm font-medium text-gray-300 mb-2">Failures by Region</h3>
      <div v-if="redundancyChartData" style="height: 180px">
        <Bar :data="redundancyChartData" :options="redundancyChartOptions" />
      </div>
      <div v-else class="text-xs text-gray-500 text-center py-8">No failures in any region</div>
    </div>
  </div>
</template>
