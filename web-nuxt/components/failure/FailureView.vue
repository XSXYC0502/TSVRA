<script setup lang="ts">
import { computed, ref, onMounted, onBeforeUnmount, watch } from 'vue';
import { storeToRefs } from 'pinia';
import { useSimulationStore } from '~/stores/simulation';
import { formatNumber } from '~/utils/formatters';
import { scheduleBuild } from '~/composables/useBuildQueue';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { LineSegments2 } from 'three/addons/lines/LineSegments2.js';
import { LineSegmentsGeometry } from 'three/addons/lines/LineSegmentsGeometry.js';
import { LineMaterial } from 'three/addons/lines/LineMaterial.js';
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
const { tsvGrid, config, failureTimeline, failedTSVPositions, activeView } = storeToRefs(store);

const layerSpacing = 3;

// 3D failure map
const canvasContainer = ref<HTMLDivElement | null>(null);
let renderer: THREE.WebGLRenderer | null = null;
let scene: THREE.Scene | null = null;
let camera: THREE.PerspectiveCamera | null = null;
let controls: OrbitControls | null = null;
let animationId = 0;
let resizeObserver: ResizeObserver | null = null;
let failureMesh: THREE.InstancedMesh | null = null;
let gridFrame: LineSegments2 | null = null;

const dummy = new THREE.Object3D();
const tempColor = new THREE.Color();

const totalInstances = computed(() => {
  if (!config.value) return 0;
  return config.value.gridSize * config.value.gridSize * config.value.numLayers;
});

// Visibility: pause animation when not visible
const isVisible = computed(() => activeView.value === 'failure' || activeView.value === 'quad');

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

// --- 3D failure map logic ---

function buildScene() {
  if (!canvasContainer.value || !config.value || !tsvGrid.value) return;
  const el = canvasContainer.value;

  // Retry via rAF if container has zero dimensions (just transitioned from display:none)
  if (el.clientWidth === 0 || el.clientHeight === 0) {
    console.log('[Failure] Container has zero dimensions, retrying...');
    requestAnimationFrame(() => buildScene());
    return;
  }

  console.log('[Failure] buildScene:', { width: el.clientWidth, height: el.clientHeight, totalInstances: totalInstances.value });

  // Clean up previous scene if any
  disposeScene();

  scene = new THREE.Scene();
  scene.background = new THREE.Color('#1a1a2e');

  const size = config.value.gridSize;
  const gridHeight = (config.value.numLayers - 1) * layerSpacing;
  const cameraDistance = size * 1.2;
  const cameraY = Math.max(cameraDistance, gridHeight * 2 + size * 0.5);

  camera = new THREE.PerspectiveCamera(50, el.clientWidth / el.clientHeight, 0.1, 1000);
  camera.position.set(cameraDistance, cameraY, cameraDistance);

  renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setSize(el.clientWidth, el.clientHeight);
  renderer.setPixelRatio(window.devicePixelRatio);
  renderer.toneMapping = THREE.NoToneMapping;
  el.appendChild(renderer.domElement);

  controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true;
  controls.dampingFactor = 0.1;
  controls.target.set(0, gridHeight / 2, 0);
  controls.update();

  // Lights
  const ambientLight = new THREE.AmbientLight(0xffffff, 0.8);
  scene.add(ambientLight);
  const dirLight = new THREE.DirectionalLight(0xffffff, 1.0);
  dirLight.position.set(size, size * 2, size);
  scene.add(dirLight);
  const dirLight2 = new THREE.DirectionalLight(0xffffff, 0.4);
  dirLight2.position.set(-size, size, -size);
  scene.add(dirLight2);

  // Create instanced mesh for failure grid
  const total = totalInstances.value;
  if (total > 0) {
    const boxGeom = new THREE.BoxGeometry(1, 1, 1);
    const stdMat = new THREE.MeshLambertMaterial({ toneMapped: false });
    failureMesh = new THREE.InstancedMesh(boxGeom, stdMat, total);
    scene.add(failureMesh);
    console.log('[Failure] Created InstancedMesh with', total, 'instances');
  }

  // Zone grid lines (fat lines for visible thickness)
  const lineGeom = new LineSegmentsGeometry();
  const lineMat = new LineMaterial({
    color: 0xffffff,
    linewidth: 1,
  });
  lineMat.resolution.set(el.clientWidth, el.clientHeight);
  gridFrame = new LineSegments2(lineGeom, lineMat);
  scene.add(gridFrame);
  updateGridFrame();

  // Initial update
  updateFailureMesh();

  // Start animation loop
  startAnimation();

  // Handle resize
  resizeObserver = new ResizeObserver(() => {
    if (!camera || !renderer) return;
    const w = el.clientWidth;
    const h = el.clientHeight;
    if (w === 0 || h === 0) return;
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
    renderer.setSize(w, h);
    if (gridFrame) {
      (gridFrame.material as LineMaterial).resolution.set(w, h);
    }
  });
  resizeObserver.observe(el);
}

function startAnimation() {
  if (animationId) return;
  function animate() {
    animationId = requestAnimationFrame(animate);
    controls?.update();
    if (renderer && scene && camera) {
      renderer.render(scene, camera);
    }
  }
  animate();
}

function stopAnimation() {
  if (animationId) {
    cancelAnimationFrame(animationId);
    animationId = 0;
  }
}

function getGridLinePositions(size: number, layout: string, gridFactor: number): number[] {
  const positions: number[] = [];
  if (layout === 'shared' && gridFactor > 1) {
    const stride = gridFactor - 1;
    for (let i = 0; 1 + stride * i < size; i++) positions.push(1 + stride * i);
  }
  return positions;
}

function updateGridFrame() {
  if (!gridFrame || !config.value) return;
  const size = config.value.gridSize;
  const layers = config.value.numLayers;
  const half = size / 2;
  const layout = config.value.redundancy?.layout ?? 'shared';
  const gf = config.value.gridFactor ?? 4;
  const linePositions = getGridLinePositions(size, layout, gf);
  const pts: number[] = [];

  for (let layer = 0; layer < layers; layer++) {
    const y = layer * layerSpacing;

    // Outer boundary
    pts.push(-half, y, -half, half, y, -half);
    pts.push(-half, y, half, half, y, half);
    pts.push(-half, y, -half, -half, y, half);
    pts.push(half, y, -half, half, y, half);

    // Internal lines at redundant TSV positions
    for (const pos of linePositions) {
      const worldPos = pos - half + 0.5;
      pts.push(-half, y, worldPos, half, y, worldPos);
      pts.push(worldPos, y, -half, worldPos, y, half);
    }
  }

  (gridFrame.geometry as LineSegmentsGeometry).setPositions(pts);
}

function updateFailureMesh() {
  if (!failureMesh || !tsvGrid.value || !config.value) return;

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
        dummy.scale.set(0.85, 0.3, 0.85);
        dummy.updateMatrix();
        failureMesh.setMatrixAt(i, dummy.matrix);

        if (tsv.failed) {
          tempColor.setRGB(1.0, 0.15, 0.15);  // red — failed
        } else if (tsv.redundant) {
          tempColor.setRGB(0.27, 0.53, 1.0);  // blue — healthy redundant
        } else {
          tempColor.setRGB(0.15, 0.7, 0.3);   // green — healthy normal
        }
        failureMesh.setColorAt(i, tempColor);
        i++;
      }
    }
  }
  failureMesh.instanceMatrix.needsUpdate = true;
  if (failureMesh.instanceColor) failureMesh.instanceColor.needsUpdate = true;
  (failureMesh.material as THREE.Material).needsUpdate = true;
}

function disposeScene() {
  stopAnimation();
  if (resizeObserver) {
    resizeObserver.disconnect();
    resizeObserver = null;
  }

  if (failureMesh) {
    failureMesh.geometry.dispose();
    (failureMesh.material as THREE.Material).dispose();
    scene?.remove(failureMesh);
    failureMesh = null;
  }

  if (gridFrame) {
    gridFrame.geometry.dispose();
    (gridFrame.material as THREE.Material).dispose();
    scene?.remove(gridFrame);
    gridFrame = null;
  }

  controls?.dispose();
  controls = null;

  if (renderer) {
    renderer.dispose();
    renderer.domElement.remove();
    renderer = null;
  }

  scene = null;
  camera = null;
}

onMounted(() => {
  if (isVisible.value && config.value && tsvGrid.value) {
    buildScene();
  }
});

onBeforeUnmount(() => {
  disposeScene();
});

// Rebuild scene when config changes
watch([config], () => {
  if (isVisible.value && config.value && tsvGrid.value && canvasContainer.value) {
    buildScene();
  } else if (!isVisible.value && scene) {
    disposeScene();
  }
});

// Handle view switching: build scene on first show, pause/resume animation
watch(
  () => store.activeView,
  () => {
    if (isVisible.value) {
      if (!scene && config.value && tsvGrid.value) {
        scheduleBuild(() => buildScene());
      } else if (scene) {
        startAnimation();
        const el = canvasContainer.value;
        if (renderer && camera && el && el.clientWidth > 0) {
          camera.aspect = el.clientWidth / el.clientHeight;
          camera.updateProjectionMatrix();
          renderer.setSize(el.clientWidth, el.clientHeight);
        }
      }
    } else {
      stopAnimation();
    }
  },
  { flush: 'post' }
);

// Update failure mesh colors reactively on cycle updates + failure changes
let updatePending = false;
watch(
  [() => store.cycleVersion, failedTSVPositions],
  () => {
    if (!scene || !failureMesh) return;
    const lastUpdate = store.lastCycleUpdate;
    const hasNewFailures = lastUpdate && lastUpdate.events.newFailures.length > 0;
    if (!hasNewFailures && failedTSVPositions.value.length === 0) return;
    if (updatePending) return;
    updatePending = true;
    requestAnimationFrame(() => {
      updatePending = false;
      updateFailureMesh();
    });
  }
);
</script>

<template>
  <div v-if="!tsvGrid || !config" class="flex items-center justify-center h-full text-gray-500">
    Start a simulation to see failure analysis
  </div>
  <div v-else class="absolute inset-0 overflow-y-auto">
    <!-- Failure Map (3D) — fills visible quadrant -->
    <div class="h-full relative">
      <div ref="canvasContainer" class="absolute inset-0" />
      <!-- Title overlay -->
      <div class="absolute top-0 left-0 right-0 px-3 py-2 text-sm font-medium text-gray-300 z-10 bg-gray-900/80">
        Failure Map ({{ failedTSVPositions.length }} failed TSVs)
      </div>
      <!-- Legend -->
      <div class="absolute bottom-2 left-2 z-10 bg-gray-900/80 rounded px-2 py-1.5 text-xs space-y-1">
        <div class="flex items-center gap-1.5">
          <span class="inline-block w-3 h-3 rounded-sm" style="background: rgb(38, 179, 77)" />
          <span class="text-gray-300">Normal</span>
        </div>
        <div class="flex items-center gap-1.5">
          <span class="inline-block w-3 h-3 rounded-sm" style="background: rgb(69, 135, 255)" />
          <span class="text-gray-300">Redundant</span>
        </div>
        <div class="flex items-center gap-1.5">
          <span class="inline-block w-3 h-3 rounded-sm" style="background: rgb(255, 38, 38)" />
          <span class="text-gray-300">Failed</span>
        </div>
      </div>
    </div>

    <!-- Charts -->
    <div class="space-y-2 p-2">
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
      <div class="bg-gray-800/30 rounded-lg p-3 border border-gray-700/30">
        <h3 class="text-sm font-medium text-gray-300 mb-2">Failures by Region</h3>
        <div v-if="redundancyChartData" style="height: 180px">
          <Bar :data="redundancyChartData" :options="redundancyChartOptions" />
        </div>
        <div v-else class="text-xs text-gray-500 text-center py-8">No failures in any region</div>
      </div>
    </div>
  </div>
</template>
