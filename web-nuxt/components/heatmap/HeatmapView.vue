<script setup lang="ts">
import { computed, ref, onMounted, onBeforeUnmount, watch } from 'vue';
import { storeToRefs } from 'pinia';
import { useSimulationStore } from '~/stores/simulation';
import { viridis } from '~/utils/colorScales';
import { scheduleBuild } from '~/composables/useBuildQueue';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

const store = useSimulationStore();
const { tsvGrid, config, hotspotWeights, activeView, cumulativeTraffic } = storeToRefs(store);

const canvasContainer = ref<HTMLDivElement | null>(null);
let renderer: THREE.WebGLRenderer | null = null;
let scene: THREE.Scene | null = null;
let camera: THREE.PerspectiveCamera | null = null;
let controls: OrbitControls | null = null;
let animationId = 0;
let resizeObserver: ResizeObserver | null = null;

let heatmapMesh: THREE.InstancedMesh | null = null;
let regionLines: THREE.LineSegments | null = null;

const dummy = new THREE.Object3D();
const tempColor = new THREE.Color();

const gridSize = computed(() => config.value?.gridSize ?? 0);
const instanceCount = computed(() => gridSize.value * gridSize.value);

// Visibility: pause animation when not visible
const isVisible = computed(() => activeView.value === 'heatmap' || activeView.value === 'quad');

// Compute traffic data using cumulativeTraffic as primary source
const trafficData = computed(() => {
  if (!config.value || !tsvGrid.value) return { trafficMap: [] as number[][], maxTraffic: 0 };
  const size = config.value.gridSize;

  // Use cumulative traffic if available and has data
  if (cumulativeTraffic.value) {
    const traffic = cumulativeTraffic.value;
    let max = 0;
    for (let y = 0; y < size; y++) {
      for (let x = 0; x < size; x++) {
        if (traffic[y][x] > max) max = traffic[y][x];
      }
    }

    if (max > 0) {
      return { trafficMap: traffic, maxTraffic: max };
    }
  }

  // Fallback to hotspot weights as baseline when no cumulative traffic
  const map: number[][] = Array.from({ length: size }, () => Array(size).fill(0));
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
        const w = hotspotWeights.value[ry][rx];
        const normalized = (w - minW) / range;
        for (let dy = 0; dy < 4; dy++) {
          for (let dx = 0; dx < 4; dx++) {
            const y = ry * 4 + dy;
            const x = rx * 4 + dx;
            if (y < size && x < size) {
              map[y][x] = normalized;
            }
          }
        }
      }
    }
    max = 1;
  }

  // Fallback: if still no data, show uniform baseline
  if (max === 0) {
    for (let y = 0; y < size; y++) {
      for (let x = 0; x < size; x++) {
        map[y][x] = 0.5;
      }
    }
    max = 1;
  }

  return { trafficMap: map, maxTraffic: max };
});

function buildScene() {
  if (!canvasContainer.value || !config.value) return;
  const el = canvasContainer.value;

  // Retry via rAF if container has zero dimensions (just transitioned from display:none)
  if (el.clientWidth === 0 || el.clientHeight === 0) {
    requestAnimationFrame(() => buildScene());
    return;
  }

  // Clean up previous scene if any
  disposeScene();

  const size = gridSize.value;

  scene = new THREE.Scene();
  scene.background = new THREE.Color('#1a1a2e');

  const cameraHeight = size * 1.4;
  camera = new THREE.PerspectiveCamera(50, el.clientWidth / el.clientHeight, 0.1, 1000);
  camera.position.set(0, cameraHeight, cameraHeight * 0.01);

  renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setSize(el.clientWidth, el.clientHeight);
  renderer.setPixelRatio(window.devicePixelRatio);
  renderer.toneMapping = THREE.NoToneMapping;
  el.appendChild(renderer.domElement);

  controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true;
  controls.dampingFactor = 0.1;
  controls.update();

  // Create heatmap instanced mesh
  const boxGeom = new THREE.BoxGeometry(1, 1, 1);
  const basicMat = new THREE.MeshBasicMaterial({ vertexColors: true, toneMapped: false });
  heatmapMesh = new THREE.InstancedMesh(boxGeom, basicMat, instanceCount.value);
  scene.add(heatmapMesh);

  // Region lines
  const linesGeom = new THREE.BufferGeometry();
  const linesMat = new THREE.LineBasicMaterial({ color: '#aaaaaa', transparent: true, opacity: 0.4 });
  regionLines = new THREE.LineSegments(linesGeom, linesMat);
  scene.add(regionLines);

  // Initial updates
  updateRegionLines();
  updateHeatmapMesh();

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

function updateRegionLines() {
  if (!regionLines) return;
  const size = gridSize.value;
  if (size === 0) return;

  const pts: number[] = [];
  const half = size / 2;
  const numRegions = size / 4;
  for (let i = 0; i <= numRegions; i++) {
    const pos = i * 4 - half;
    pts.push(-half, 0.1, pos, half, 0.1, pos);
    pts.push(pos, 0.1, -half, pos, 0.1, half);
  }

  regionLines.geometry.dispose();
  const geom = new THREE.BufferGeometry();
  geom.setAttribute('position', new THREE.BufferAttribute(new Float32Array(pts), 3));
  regionLines.geometry = geom;
}

function updateHeatmapMesh() {
  if (!heatmapMesh || !config.value) return;

  const size = gridSize.value;
  const half = size / 2;
  const { trafficMap, maxTraffic } = trafficData.value;
  if (trafficMap.length === 0) return;

  let i = 0;
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      dummy.position.set(x - half + 0.5, 0, y - half + 0.5);
      dummy.scale.set(0.95, 0.1, 0.95);
      dummy.updateMatrix();
      heatmapMesh.setMatrixAt(i, dummy.matrix);

      const t = maxTraffic > 0 ? trafficMap[y][x] / maxTraffic : 0;
      const [r, g, b] = viridis(t);
      tempColor.setRGB(r, g, b);
      heatmapMesh.setColorAt(i, tempColor);
      i++;
    }
  }
  heatmapMesh.instanceMatrix.needsUpdate = true;
  if (heatmapMesh.instanceColor) heatmapMesh.instanceColor.needsUpdate = true;
}

function disposeScene() {
  stopAnimation();
  if (resizeObserver) {
    resizeObserver.disconnect();
    resizeObserver = null;
  }

  if (heatmapMesh) {
    heatmapMesh.geometry.dispose();
    (heatmapMesh.material as THREE.Material).dispose();
    scene?.remove(heatmapMesh);
    heatmapMesh = null;
  }

  if (regionLines) {
    regionLines.geometry.dispose();
    (regionLines.material as THREE.Material).dispose();
    scene?.remove(regionLines);
    regionLines = null;
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

// Rebuild scene when config changes (different grid size)
watch([config], () => {
  if (isVisible.value && config.value && tsvGrid.value && canvasContainer.value) {
    buildScene();
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

// Update heatmap colors reactively on cycle updates
let updatePending = false;
watch(
  [() => store.cycleVersion],
  () => {
    if (!scene || !heatmapMesh) return;
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

    <div class="absolute bottom-3 left-3 bg-gray-900/90 rounded p-2 text-xs">
      <div class="mb-1 text-gray-400">Traffic Density</div>
      <div class="flex items-center gap-2">
        <span>Low</span>
        <div class="w-24 h-2 rounded" style="background:linear-gradient(to right, #440154, #31688e, #35b779, #fde725)" />
        <span>High</span>
      </div>
    </div>
  </div>
</template>
