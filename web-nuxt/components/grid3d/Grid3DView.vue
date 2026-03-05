<script setup lang="ts">
import { ref, computed, onMounted, onBeforeUnmount, watch } from 'vue';
import { storeToRefs } from 'pinia';
import { useSimulationStore } from '~/stores/simulation';
import { useColorScale } from '~/composables/useColorScale';
import { scheduleBuild } from '~/composables/useBuildQueue';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import type { ColorMode } from '~/types/simulation';

const store = useSimulationStore();
const { tsvGrid, config, colorMode, activeRequests, activeView, cumulativeTraffic } = storeToRefs(store);

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
let gridFrame: THREE.LineSegments | null = null;
// Group for request path lines
let pathsGroup: THREE.Group | null = null;

const dummy = new THREE.Object3D();
const tempColor = new THREE.Color();

const gridSize = computed(() => config.value?.gridSize ?? 0);
const numLayers = computed(() => config.value?.numLayers ?? 0);
const centerY = computed(() => ((numLayers.value - 1) * layerSpacing) / 2);

// Visibility: pause animation when not visible
const isVisible = computed(() => activeView.value === 'grid3d' || activeView.value === 'quad');

// Compute traffic map from active requests
const trafficMap = computed(() => {
  const map = new Map<string, number>();
  let maxUsage = 1;
  if (colorMode.value === 'traffic') {
    for (const req of activeRequests.value.values()) {
      if (req.path) {
        for (const [x, y, z] of req.path) {
          const key = `${x},${y},${z}`;
          const count = (map.get(key) ?? 0) + 1;
          map.set(key, count);
          if (count > maxUsage) maxUsage = count;
        }
      }
    }
  }
  return { map, maxUsage };
});

// Request paths for visualization
const requestPathLines = computed(() => {
  const reqs = Array.from(activeRequests.value.values()).slice(0, 50);
  const half = gridSize.value / 2;
  return reqs
    .filter(req => req.path && req.path.length >= 2)
    .map(req => {
      const pts = req.path.map(
        ([x, y, z]) => new THREE.Vector3(x - half + 0.5, z * layerSpacing + 0.2, y - half + 0.5)
      );
      return { id: req.id, points: pts };
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
  const lambertMat = new THREE.MeshLambertMaterial({ vertexColors: true, toneMapped: false });
  layerMeshes = [];
  for (let z = 0; z < layers; z++) {
    const count = size * size;
    const mesh = new THREE.InstancedMesh(boxGeom, lambertMat.clone(), count);
    scene.add(mesh);
    layerMeshes.push(mesh);
  }

  // Grid frame
  const gridFrameGeom = new THREE.BufferGeometry();
  const gridFrameMat = new THREE.LineBasicMaterial({ color: '#333333', transparent: true, opacity: 0.5 });
  gridFrame = new THREE.LineSegments(gridFrameGeom, gridFrameMat);
  scene.add(gridFrame);

  // Paths group
  pathsGroup = new THREE.Group();
  scene.add(pathsGroup);

  // Initial update
  updateGridFrame();
  updateLayerMeshes();
  updatePaths();

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

function updateGridFrame() {
  if (!gridFrame) return;
  const size = gridSize.value;
  const layers = numLayers.value;
  if (size === 0 || layers === 0) return;

  const pts: number[] = [];
  const half = size / 2;
  const numRegions = size / 4;
  for (let layer = 0; layer < layers; layer++) {
    const y = layer * layerSpacing;
    for (let i = 0; i <= numRegions; i++) {
      const pos = i * 4 - half;
      pts.push(-half, y - 0.15, pos, half, y - 0.15, pos);
      pts.push(pos, y - 0.15, -half, pos, y - 0.15, half);
    }
  }

  gridFrame.geometry.dispose();
  const geom = new THREE.BufferGeometry();
  geom.setAttribute('position', new THREE.BufferAttribute(new Float32Array(pts), 3));
  gridFrame.geometry = geom;
}

function updateLayerMeshes() {
  if (!tsvGrid.value || !config.value) return;

  const size = gridSize.value;
  const colorFn = useColorScale(colorMode.value);
  const { map: tMap, maxUsage } = trafficMap.value;

  for (let z = 0; z < layerMeshes.length; z++) {
    const mesh = layerMeshes[z];
    const layer = tsvGrid.value[z];
    if (!layer) continue;

    let i = 0;
    for (let y = 0; y < size; y++) {
      for (let x = 0; x < size; x++) {
        const tsv = layer[y][x];
        dummy.position.set(x - size / 2 + 0.5, z * layerSpacing, y - size / 2 + 0.5);
        dummy.scale.set(0.8, 0.3, 0.8);
        dummy.updateMatrix();
        mesh.setMatrixAt(i, dummy.matrix);

        const usage = (colorMode.value === 'traffic' && tMap)
          ? (tMap.get(`${x},${y},${z}`) ?? 0)
          : tsv.usageCount;
        const [r, g, b] = colorFn.value({ ...tsv, usageCount: usage }, maxUsage);
        tempColor.setRGB(r, g, b);
        mesh.setColorAt(i, tempColor);
        i++;
      }
    }
    mesh.instanceMatrix.needsUpdate = true;
    if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
  }
}

function updatePaths() {
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
  for (const pathData of requestPathLines.value) {
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
watch(
  [() => store.cycleVersion, colorMode],
  () => {
    if (!scene || layerMeshes.length === 0) return;
    if (updatePending) return;
    updatePending = true;
    requestAnimationFrame(() => {
      updatePending = false;
      updateLayerMeshes();
      updatePaths();
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
      <div class="flex items-center gap-2"><div class="w-4 h-0.5" style="background:#ff6600" /><span>Request Path</span></div>
    </div>
  </div>
</template>
