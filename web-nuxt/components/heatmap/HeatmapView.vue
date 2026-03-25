<script setup lang="ts">
import { computed, ref, onMounted, onBeforeUnmount, watch } from 'vue';
import { storeToRefs } from 'pinia';
import { useSimulationStore } from '~/stores/simulation';
import { viridis } from '~/utils/colorScales';
import { scheduleBuild } from '~/composables/useBuildQueue';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { LineSegments2 } from 'three/addons/lines/LineSegments2.js';
import { LineSegmentsGeometry } from 'three/addons/lines/LineSegmentsGeometry.js';
import { LineMaterial } from 'three/addons/lines/LineMaterial.js';

const store = useSimulationStore();
const { tsvGrid, config, hotspotWeights, activeView, cumulativeTraffic } = storeToRefs(store);

const layerSpacing = 3;

const canvasContainer = ref<HTMLDivElement | null>(null);
let renderer: THREE.WebGLRenderer | null = null;
let scene: THREE.Scene | null = null;
let camera: THREE.PerspectiveCamera | null = null;
let controls: OrbitControls | null = null;
let animationId = 0;
let resizeObserver: ResizeObserver | null = null;

let layerPlanes: THREE.Mesh[] = [];
let gridFrame: LineSegments2 | null = null;

const gridSize = computed(() => config.value?.gridSize ?? 0);
const numLayers = computed(() => config.value?.numLayers ?? 0);

// Visibility: pause animation when not visible
const isVisible = computed(() => activeView.value === 'heatmap' || activeView.value === 'quad');

// Compute traffic data from 3D cumulativeTraffic
const trafficData = computed(() => {
  if (!config.value || !tsvGrid.value) {
    return { trafficMap: [] as number[][][], maxTraffic: 0 };
  }
  const size = config.value.gridSize;
  const layers = config.value.numLayers;

  // Use cumulative traffic if available and has data
  if (cumulativeTraffic.value && cumulativeTraffic.value.length > 0) {
    const traffic = cumulativeTraffic.value;
    let max = 0;
    for (let z = 0; z < traffic.length; z++) {
      for (let y = 0; y < size; y++) {
        for (let x = 0; x < size; x++) {
          const v = traffic[z]?.[y]?.[x] ?? 0;
          if (v > max) max = v;
        }
      }
    }
    if (max > 0) {
      return { trafficMap: traffic, maxTraffic: max };
    }
  }

  // Fallback to hotspot weights expanded per-layer
  const map: number[][][] = [];
  for (let z = 0; z < layers; z++) {
    map[z] = Array.from({ length: size }, () => Array(size).fill(0));
  }
  let max = 0;

  if (hotspotWeights.value) {
    let minW = Infinity;
    let maxW = -Infinity;
    for (const row of hotspotWeights.value) {
      for (const w of row) {
        if (w < minW) minW = w;
        if (w > maxW) maxW = w;
      }
    }
    const range = maxW - minW || 1;
    for (let ry = 0; ry < hotspotWeights.value.length; ry++) {
      for (let rx = 0; rx < hotspotWeights.value[ry].length; rx++) {
        const normalized = (hotspotWeights.value[ry][rx] - minW) / range;
        for (let dy = 0; dy < 4; dy++) {
          for (let dx = 0; dx < 4; dx++) {
            const y = ry * 4 + dy;
            const x = rx * 4 + dx;
            if (y < size && x < size) {
              for (let z = 0; z < layers; z++) {
                map[z][y][x] = normalized;
              }
            }
          }
        }
      }
    }
    max = 1;
  }

  // Fallback: uniform baseline
  if (max === 0) {
    for (let z = 0; z < layers; z++) {
      for (let y = 0; y < size; y++) {
        for (let x = 0; x < size; x++) {
          map[z][y][x] = 0.5;
        }
      }
    }
    max = 1;
  }

  return { trafficMap: map, maxTraffic: max };
});

function buildScene() {
  if (!canvasContainer.value || !config.value) return;
  const el = canvasContainer.value;

  if (el.clientWidth === 0 || el.clientHeight === 0) {
    requestAnimationFrame(() => buildScene());
    return;
  }

  disposeScene();

  const size = config.value.gridSize;
  const layers = config.value.numLayers;
  const gridHeight = (layers - 1) * layerSpacing;
  const cameraDistance = size * 1.2;
  const cameraY = Math.max(cameraDistance, gridHeight * 2 + size * 0.5);

  scene = new THREE.Scene();
  scene.background = new THREE.Color('#1a1a2e');

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

  // Heatmap planes with vertex colors (smooth interpolation)
  layerPlanes = [];
  for (let z = 0; z < layers; z++) {
    const planeGeom = new THREE.PlaneGeometry(size, size, size - 1, size - 1);
    planeGeom.rotateX(-Math.PI / 2);
    const colors = new Float32Array(size * size * 3);
    planeGeom.setAttribute('color', new THREE.BufferAttribute(colors, 3));
    const mat = new THREE.MeshBasicMaterial({ vertexColors: true, toneMapped: false, side: THREE.DoubleSide });
    const mesh = new THREE.Mesh(planeGeom, mat);
    mesh.position.set(0, z * layerSpacing, 0);
    scene.add(mesh);
    layerPlanes.push(mesh);
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

  // Initial color update
  updateHeatmapMesh();

  startAnimation();

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

function updateHeatmapMesh() {
  if (layerPlanes.length === 0 || !config.value) return;

  const size = gridSize.value;
  const { trafficMap, maxTraffic } = trafficData.value;
  if (trafficMap.length === 0) return;

  for (let z = 0; z < layerPlanes.length; z++) {
    const colorAttr = layerPlanes[z].geometry.getAttribute('color') as THREE.BufferAttribute;
    const colors = colorAttr.array as Float32Array;

    for (let iy = 0; iy < size; iy++) {
      for (let ix = 0; ix < size; ix++) {
        const t = maxTraffic > 0 ? (trafficMap[z]?.[iy]?.[ix] ?? 0) / maxTraffic : 0;
        const [r, g, b] = viridis(t);
        const idx = (iy * size + ix) * 3;
        colors[idx] = r;
        colors[idx + 1] = g;
        colors[idx + 2] = b;
      }
    }
    colorAttr.needsUpdate = true;
  }
}

function disposeScene() {
  stopAnimation();
  if (resizeObserver) {
    resizeObserver.disconnect();
    resizeObserver = null;
  }

  for (const mesh of layerPlanes) {
    mesh.geometry.dispose();
    (mesh.material as THREE.Material).dispose();
    scene?.remove(mesh);
  }
  layerPlanes = [];

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

// Rebuild scene when config changes (different grid size / layers)
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

// Update heatmap colors every N cycles
let updatePending = false;
watch(
  [() => store.cycleVersion],
  () => {
    if (!scene || layerPlanes.length === 0) return;
    const interval = store.heatmapUpdateInterval;
    if (store.currentCycle % interval !== 0) return;
    if (updatePending) return;
    updatePending = true;
    requestAnimationFrame(() => {
      updatePending = false;
      updateHeatmapMesh();
    });
  }
);
</script>

<template>
  <div v-if="!config || !tsvGrid" class="flex items-center justify-center h-full text-gray-500">
    Start a simulation to see the heatmap
  </div>
  <div v-else class="absolute inset-0">
    <div ref="canvasContainer" class="absolute inset-0" />

    <div class="absolute bottom-3 left-3 bg-gray-900/90 rounded p-2 text-xs max-w-56">
      <div class="mb-1 text-gray-400">Traffic Density</div>
      <div class="flex items-center gap-2">
        <span>Low</span>
        <div class="w-24 h-2 rounded" style="background:linear-gradient(to right, #440154, #31688e, #35b779, #fde725)" />
        <span>High</span>
      </div>
      <div class="mt-2 text-gray-500 leading-tight">
        <div><span class="text-gray-400">Top layer:</span> edge request density (wire interconnects)</div>
        <div><span class="text-gray-400">Other layers:</span> TSV transmission traffic</div>
      </div>
    </div>
  </div>
</template>
