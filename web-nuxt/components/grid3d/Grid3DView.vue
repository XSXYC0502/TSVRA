<script setup lang="ts">
import { ref, computed, watchEffect, onMounted, onBeforeUnmount } from 'vue';
import { storeToRefs } from 'pinia';
import { useSimulationStore } from '~/stores/simulation';
import { useColorScale } from '~/composables/useColorScale';
import * as THREE from 'three';
import type { ColorMode } from '~/types/simulation';

const store = useSimulationStore();
const { tsvGrid, config, colorMode, activeRequests } = storeToRefs(store);

const layerSpacing = 3;

// Template refs for instanced meshes - one per layer
const layerMeshRefs = ref<(THREE.InstancedMesh | null)[]>([]);
// Template ref for lines container
const linesGroup = ref<THREE.Group | null>(null);
// Template ref for grid frame
const gridFrameGeom = ref<THREE.BufferGeometry | null>(null);

const gridSize = computed(() => config.value?.gridSize ?? 0);
const numLayers = computed(() => config.value?.numLayers ?? 0);
const centerY = computed(() => ((numLayers.value - 1) * layerSpacing) / 2);
const cameraDistance = computed(() => gridSize.value * 1.2);
const cameraY = computed(() => {
  const gridHeight = (numLayers.value - 1) * layerSpacing;
  return Math.max(cameraDistance.value, gridHeight * 2 + gridSize.value * 0.5);
});
const cameraPosition = computed(() => [cameraDistance.value, cameraY.value, cameraDistance.value] as [number, number, number]);

const dummy = new THREE.Object3D();
const tempColor = new THREE.Color();

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

// Get color function based on current mode
const getColor = useColorScale(colorMode.value);

// Watch for color mode changes
watchEffect(() => {
  // Re-evaluate getColor when colorMode changes
  const _mode = colorMode.value;
});

// Update instanced meshes when data changes
function updateLayerMesh(layerIndex: number, mesh: THREE.InstancedMesh | null) {
  if (!mesh || !tsvGrid.value) return;
  const layer = tsvGrid.value[layerIndex];
  if (!layer) return;

  const size = gridSize.value;
  const colorFn = useColorScale(colorMode.value);
  const { map: tMap, maxUsage } = trafficMap.value;
  let i = 0;

  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const tsv = layer[y][x];
      dummy.position.set(x - size / 2 + 0.5, layerIndex * layerSpacing, y - size / 2 + 0.5);
      dummy.scale.set(0.8, 0.3, 0.8);
      dummy.updateMatrix();
      mesh.setMatrixAt(i, dummy.matrix);

      const usage = (colorMode.value === 'traffic' && tMap)
        ? (tMap.get(`${x},${y},${layerIndex}`) ?? 0)
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

// Grid frame points
const gridFramePoints = computed(() => {
  const size = gridSize.value;
  const layers = numLayers.value;
  if (size === 0 || layers === 0) return new Float32Array(0);

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
  return new Float32Array(pts);
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

// Refs for line objects
const lineObjects = ref<THREE.Line[]>([]);

// Watch and update lines
watchEffect(() => {
  if (!linesGroup.value) return;
  // Clear existing lines
  while (linesGroup.value.children.length > 0) {
    const child = linesGroup.value.children[0];
    linesGroup.value.remove(child);
    if ((child as THREE.Line).geometry) (child as THREE.Line).geometry.dispose();
    if ((child as THREE.Line).material) ((child as THREE.Line).material as THREE.Material).dispose();
  }

  // Add new lines
  const lineMaterial = new THREE.LineBasicMaterial({ color: '#ff6600', transparent: true, opacity: 0.8 });
  for (const pathData of requestPathLines.value) {
    const geometry = new THREE.BufferGeometry().setFromPoints(pathData.points);
    const line = new THREE.Line(geometry, lineMaterial.clone());
    linesGroup.value.add(line);
  }
});

// Watch and update grid frame
watchEffect(() => {
  if (!gridFrameGeom.value) return;
  const pts = gridFramePoints.value;
  if (pts.length > 0) {
    gridFrameGeom.value.setAttribute('position', new THREE.BufferAttribute(pts, 3));
  }
});

// Provide a function for child mesh updates triggered by watcher
const meshInstances = computed(() => {
  if (!tsvGrid.value || !config.value) return [];
  return tsvGrid.value.map((_, z) => ({
    layerIndex: z,
    count: gridSize.value * gridSize.value,
  }));
});

// Use a single watcher that updates all layer meshes when data changes
watchEffect(() => {
  // Dependencies: tsvGrid, colorMode, trafficMap
  const _grid = tsvGrid.value;
  const _cm = colorMode.value;
  const _tm = trafficMap.value;

  for (let z = 0; z < layerMeshRefs.value.length; z++) {
    updateLayerMesh(z, layerMeshRefs.value[z]);
  }
});

function setLayerMeshRef(index: number, el: any) {
  if (el) {
    if (!layerMeshRefs.value[index]) {
      layerMeshRefs.value[index] = el;
    } else {
      layerMeshRefs.value[index] = el;
    }
  }
}
</script>

<template>
  <div v-if="!tsvGrid || !config" class="flex items-center justify-center h-full text-gray-500">
    Start a simulation to see the 3D grid
  </div>
  <div v-else class="absolute inset-0">
    <TresCanvas flat :camera="{ position: cameraPosition, fov: 50 }">
      <TresScene>
        <TresColor attach="background" :args="['#1a1a2e']" />
        <TresAmbientLight :intensity="0.8" />
        <TresDirectionalLight :position="[gridSize, gridSize * 2, gridSize]" :intensity="1.0" />
        <TresDirectionalLight :position="[-gridSize, gridSize, -gridSize]" :intensity="0.4" />

        <!-- Instanced meshes for each layer -->
        <TresInstancedMesh
          v-for="layer in meshInstances"
          :key="layer.layerIndex"
          :ref="(el: any) => setLayerMeshRef(layer.layerIndex, el?.instance || el)"
          :args="[undefined, undefined, layer.count]"
        >
          <TresBoxGeometry :args="[1, 1, 1]" />
          <TresMeshLambertMaterial vertex-colors :tone-mapped="false" />
        </TresInstancedMesh>

        <!-- Grid frame -->
        <TresLineSegments>
          <TresBufferGeometry ref="gridFrameGeom" />
          <TresLineBasicMaterial color="#333333" :transparent="true" :opacity="0.5" />
        </TresLineSegments>

        <!-- Request paths container -->
        <TresGroup ref="linesGroup" />

        <OrbitControls :enable-damping="true" :damping-factor="0.1" :target="[0, centerY, 0]" />
      </TresScene>
    </TresCanvas>

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
