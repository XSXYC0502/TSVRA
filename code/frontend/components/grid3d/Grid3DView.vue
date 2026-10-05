<script setup lang="ts">
import { ref, computed, onMounted, onBeforeUnmount, watch } from 'vue';
import { storeToRefs } from 'pinia';
import { useSimulationStore } from '~/stores/simulation';
import { useColorScale } from '~/composables/useColorScale';
import { scheduleBuild } from '~/composables/useBuildQueue';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { LineSegments2 } from 'three/addons/lines/LineSegments2.js';
import { LineSegmentsGeometry } from 'three/addons/lines/LineSegmentsGeometry.js';
import { LineMaterial } from 'three/addons/lines/LineMaterial.js';
import type { ColorMode } from '~/types/simulation';

const store = useSimulationStore();
const { tsvGrid, config, colorMode, activeRequests, rerouteDisplayPaths, activeView, cumulativeTraffic } = storeToRefs(store);

const layerSpacing = 3;

const canvasContainer = ref<HTMLDivElement | null>(null);
let renderer: THREE.WebGLRenderer | null = null;
let scene: THREE.Scene | null = null;
let camera: THREE.PerspectiveCamera | null = null;
let controls: OrbitControls | null = null;
let animationId = 0;
let resizeObserver: ResizeObserver | null = null;

// Per-layer instanced meshes
let layerMeshes: THREE.InstancedMesh[] = [];
// Grid frame line segments
let gridFrame: LineSegments2 | null = null;
// Group for request path lines
let pathsGroup: THREE.Group | null = null;

const dummy = new THREE.Object3D();
const tempColor = new THREE.Color();

const gridSize = computed(() => config.value?.gridSize ?? 0);
const numLayers = computed(() => config.value?.numLayers ?? 0);
const centerY = computed(() => ((numLayers.value - 1) * layerSpacing) / 2);

// Visibility: pause animation when not visible
const isVisible = computed(() => activeView.value === 'grid3d' || activeView.value === 'quad');

// Compute traffic map from cumulative traffic (matches Heatmap behavior)
const trafficMap = computed(() => {
  let maxUsage = 1;
  const traffic = cumulativeTraffic.value;
  if (colorMode.value === 'traffic' && traffic && traffic.length > 0) {
    for (const layer of traffic) {
      if (!layer) continue;
      for (const row of layer) {
        if (!row) continue;
        for (const val of row) {
          if (val > maxUsage) maxUsage = val;
        }
      }
    }
  }
  return { traffic, maxUsage };
});

// Reroute paths — from persistent display paths (persist 1000 cycles after completion/failure)
const reroutePathLines = computed(() => {
  const half = gridSize.value / 2;
  const entries = Array.from(rerouteDisplayPaths.value.values()).slice(0, 50);
  return entries
    .filter(entry => entry.path && entry.path.length >= 2)
    .map((entry, i) => {
      const pts = entry.path.map(
        ([x, y, z]) => new THREE.Vector3(x - half + 0.5, z * layerSpacing + 0.2, y - half + 0.5)
      );
      return { id: i, points: pts };
    });
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
  const layers = numLayers.value;

  scene = new THREE.Scene();
  scene.background = new THREE.Color('#1a1a2e');

  const cameraDistance = size * 1.2;
  const gridHeight = (layers - 1) * layerSpacing;
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
  controls.target.set(0, centerY.value, 0);
  controls.update();

  // Lights
  const ambientLight = new THREE.AmbientLight(0xffffff, 0.8);
  scene.add(ambientLight);
  const dirLight1 = new THREE.DirectionalLight(0xffffff, 1.0);
  dirLight1.position.set(size, size * 2, size);
  scene.add(dirLight1);
  const dirLight2 = new THREE.DirectionalLight(0xffffff, 0.4);
  dirLight2.position.set(-size, size, -size);
  scene.add(dirLight2);

  // Create per-layer instanced meshes
  const boxGeom = new THREE.BoxGeometry(1, 1, 1);
  const lambertMat = new THREE.MeshLambertMaterial({ toneMapped: false });
  layerMeshes = [];
  for (let z = 0; z < layers; z++) {
    const count = size * size;
    const mesh = new THREE.InstancedMesh(boxGeom, lambertMat.clone(), count);
    scene.add(mesh);
    layerMeshes.push(mesh);
  }

  // Grid frame
  const lineGeom = new LineSegmentsGeometry();
  const lineMat = new LineMaterial({
    color: 0xffffff,
    linewidth: 1,
  });
  lineMat.resolution.set(el.clientWidth, el.clientHeight);
  gridFrame = new LineSegments2(lineGeom, lineMat);
  scene.add(gridFrame);

  // Paths group
  pathsGroup = new THREE.Group();
  scene.add(pathsGroup);

  // Initial update
  updateGridFrame();
  updateLayerMeshes();
  updateReroutePaths();

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
    if (gridFrame) (gridFrame.material as LineMaterial).resolution.set(w, h);
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
  if (!gridFrame) return;
  const size = gridSize.value;
  const layers = numLayers.value;
  if (size === 0 || layers === 0) return;

  const pts: number[] = [];
  const half = size / 2;
  const layout = config.value?.redundancy?.layout ?? 'shared';
  const gf = config.value?.gridFactor ?? 4;
  const linePositions = getGridLinePositions(size, layout, gf);

  for (let layer = 0; layer < layers; layer++) {
    const y = layer * layerSpacing;

    // Outer boundary (edge-aligned, above blocks)
    pts.push(-half, y + 0.16, -half, half, y + 0.16, -half);
    pts.push(-half, y + 0.16, half, half, y + 0.16, half);
    pts.push(-half, y + 0.16, -half, -half, y + 0.16, half);
    pts.push(half, y + 0.16, -half, half, y + 0.16, half);

    // Internal lines through spare/redundant positions (cell-center aligned, above blocks)
    for (const pos of linePositions) {
      const worldPos = pos - half + 0.5;
      pts.push(-half, y + 0.16, worldPos, half, y + 0.16, worldPos);
      pts.push(worldPos, y + 0.16, -half, worldPos, y + 0.16, half);
    }
  }

  gridFrame.geometry.dispose();
  const geom = new LineSegmentsGeometry();
  geom.setPositions(pts);
  gridFrame.geometry = geom;
}

function updateLayerMeshes() {
  if (!tsvGrid.value || !config.value) return;

  const size = gridSize.value;
  const colorFn = useColorScale(colorMode.value);
  const { traffic, maxUsage } = trafficMap.value;

  for (let z = 0; z < layerMeshes.length; z++) {
    const mesh = layerMeshes[z]!;
    const layer = tsvGrid.value[z];
    if (!layer) continue;

    let i = 0;
    for (let y = 0; y < size; y++) {
      const row = layer[y];
      if (!row) continue;
      for (let x = 0; x < size; x++) {
        const tsv = row[x];
        if (!tsv) { i++; continue; }
        dummy.position.set(x - size / 2 + 0.5, z * layerSpacing, y - size / 2 + 0.5);
        dummy.scale.set(0.8, 0.3, 0.8);
        dummy.updateMatrix();
        mesh.setMatrixAt(i, dummy.matrix);

        const usage = (colorMode.value === 'traffic' && traffic && traffic[z])
          ? (traffic[z]![y]?.[x] ?? 0)
          : tsv.usageCount;
        const [r, g, b] = colorFn.value({ ...tsv, usageCount: usage } as any, maxUsage);
        tempColor.setRGB(r, g, b);
        mesh.setColorAt(i, tempColor);
        i++;
      }
    }
    mesh.instanceMatrix.needsUpdate = true;
    if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
    (mesh.material as THREE.Material).needsUpdate = true;
  }
}

function updateReroutePaths() {
  if (!pathsGroup) return;

  // Clear existing lines
  while (pathsGroup.children.length > 0) {
    const child = pathsGroup.children[0] as THREE.Line;
    pathsGroup.remove(child);
    if (child.geometry) child.geometry.dispose();
    if (child.material) (child.material as THREE.Material).dispose();
  }

  // Add new lines
  const lineMaterial = new THREE.LineBasicMaterial({ color: '#ff6600', transparent: true, opacity: 0.8 });
  for (const pathData of reroutePathLines.value) {
    const geometry = new THREE.BufferGeometry().setFromPoints(pathData.points);
    const line = new THREE.Line(geometry, lineMaterial.clone());
    pathsGroup.add(line);
  }
}

function disposeScene() {
  stopAnimation();
  if (resizeObserver) {
    resizeObserver.disconnect();
    resizeObserver = null;
  }

  // Dispose layer meshes
  for (const mesh of layerMeshes) {
    mesh.geometry.dispose();
    (mesh.material as THREE.Material).dispose();
    scene?.remove(mesh);
  }
  layerMeshes = [];

  // Dispose grid frame
  if (gridFrame) {
    gridFrame.geometry.dispose();
    (gridFrame.material as THREE.Material).dispose();
    scene?.remove(gridFrame);
    gridFrame = null;
  }

  // Dispose paths group
  if (pathsGroup) {
    while (pathsGroup.children.length > 0) {
      const child = pathsGroup.children[0] as THREE.Line;
      pathsGroup.remove(child);
      if (child.geometry) child.geometry.dispose();
      if (child.material) (child.material as THREE.Material).dispose();
    }
    scene?.remove(pathsGroup);
    pathsGroup = null;
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
// flush: 'post' ensures DOM is updated (v-show applied) before we check dimensions
watch(
  () => store.activeView,
  () => {
    if (isVisible.value) {
      if (!scene && config.value && tsvGrid.value) {
        // buildScene will retry via rAF if container dimensions are zero
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

// Update colors/positions reactively when cycle data changes
// Use rAF-based throttle to avoid excessive Three.js updates
let updatePending = false;
let needsLayerUpdate = false;
let needsRerouteUpdate = false;
let prevColorMode = colorMode.value;
watch(
  [() => store.cycleVersion, colorMode],
  () => {
    if (!scene || layerMeshes.length === 0) return;
    const lastUpdate = store.lastCycleUpdate;
    const colorModeChanged = prevColorMode !== colorMode.value;
    prevColorMode = colorMode.value;

    if (colorModeChanged) {
      needsLayerUpdate = true;
    } else if (lastUpdate && lastUpdate.events.newFailures.length > 0) {
      needsLayerUpdate = true;
    } else if (lastUpdate?.updateVisuals) {
      needsLayerUpdate = true;
    }

    if (lastUpdate && (lastUpdate.events.reroutes.length > 0 || lastUpdate.events.completed.length > 0 || lastUpdate.events.failed.length > 0)) {
      needsRerouteUpdate = true;
    }
    // Also trigger reroute update when display paths change (e.g., expiry pruning)
    if (store.rerouteDisplayPaths.size > 0) {
      needsRerouteUpdate = true;
    }
    if (!needsLayerUpdate && !needsRerouteUpdate) return;

    if (updatePending) return;
    updatePending = true;
    requestAnimationFrame(() => {
      updatePending = false;
      if (needsLayerUpdate) { updateLayerMeshes(); needsLayerUpdate = false; }
      if (needsRerouteUpdate) { updateReroutePaths(); needsRerouteUpdate = false; }
    });
  }
);
</script>

<template>
  <div v-if="!tsvGrid || !config" class="flex items-center justify-center h-full text-gray-500">
    Start a simulation to see the 3D grid
  </div>
  <div v-else class="absolute inset-0">
    <div ref="canvasContainer" class="absolute inset-0" />

    <!-- Color mode buttons -->
    <div class="absolute top-3 left-3 flex gap-1">
      <button
        v-for="mode in (['type', 'traffic', 'status'] as ColorMode[])"
        :key="mode"
        :class="[
          'px-2 py-1 rounded text-xs font-medium',
          colorMode === mode ? 'bg-blue-600 text-white' : 'bg-gray-800/80 text-gray-300 hover:bg-gray-700/80'
        ]"
        @click="store.setColorMode(mode)"
      >
        {{ mode.charAt(0).toUpperCase() + mode.slice(1) }}
      </button>
    </div>

    <!-- Legend -->
    <div class="absolute bottom-3 left-3 bg-gray-900/90 rounded p-2 text-xs space-y-1">
      <template v-if="colorMode === 'type'">
        <div class="flex items-center gap-2"><div class="w-3 h-3 rounded" style="background:#4488ff" /><span>Normal TSV</span></div>
        <div class="flex items-center gap-2"><div class="w-3 h-3 rounded" style="background:#ffd700" /><span>Redundant TSV</span></div>
        <div class="flex items-center gap-2"><div class="w-3 h-3 rounded" style="background:#ff3333" /><span>Failed TSV</span></div>
      </template>
      <template v-if="colorMode === 'status'">
        <div class="flex items-center gap-2"><div class="w-3 h-3 rounded" style="background:#44ff44" /><span>Healthy</span></div>
        <div class="flex items-center gap-2"><div class="w-3 h-3 rounded" style="background:#ff3333" /><span>Failed</span></div>
      </template>
      <template v-if="colorMode === 'traffic'">
        <div class="flex items-center gap-2"><span>Low</span><div class="w-16 h-2 rounded" style="background:linear-gradient(to right, #440154, #31688e, #35b779, #fde725)" /><span>High</span></div>
      </template>
      <div class="flex items-center gap-2"><div class="w-4 h-0.5" style="background:#ff6600" /><span>Reroute Path</span></div>
    </div>
  </div>
</template>
