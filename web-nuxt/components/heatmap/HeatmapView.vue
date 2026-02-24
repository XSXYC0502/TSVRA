<script setup lang="ts">
import { computed, watchEffect, ref } from 'vue';
import { storeToRefs } from 'pinia';
import { useSimulationStore } from '~/stores/simulation';
import { viridis } from '~/utils/colorScales';
import * as THREE from 'three';

const store = useSimulationStore();
const { tsvGrid, config, activeRequests, hotspotWeights } = storeToRefs(store);

const heatmapMeshRef = ref<THREE.InstancedMesh | null>(null);
const regionLinesGeom = ref<THREE.BufferGeometry | null>(null);

const gridSize = computed(() => config.value?.gridSize ?? 0);
const cameraHeight = computed(() => gridSize.value * 1.4);
const cameraPosition = computed(() => [0, cameraHeight.value, cameraHeight.value * 0.01] as [number, number, number]);
const instanceCount = computed(() => gridSize.value * gridSize.value);

const dummy = new THREE.Object3D();
const tempColor = new THREE.Color();

// Compute traffic map
const trafficData = computed(() => {
  if (!config.value || !tsvGrid.value) return { trafficMap: [] as number[][], maxTraffic: 0 };
  const size = config.value.gridSize;
  const map: number[][] = Array.from({ length: size }, () => Array(size).fill(0));
  let max = 0;

  for (const req of activeRequests.value.values()) {
    if (req.path) {
      for (const [x, y] of req.path) {
        if (y >= 0 && y < size && x >= 0 && x < size) {
          map[y][x]++;
          if (map[y][x] > max) max = map[y][x];
        }
      }
    }
  }

  // Use hotspot weights as baseline when no traffic
  if (hotspotWeights.value && max === 0) {
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

// Update instanced mesh
watchEffect(() => {
  const mesh = heatmapMeshRef.value;
  if (!mesh || !config.value) return;

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
      mesh.setMatrixAt(i, dummy.matrix);

      const t = maxTraffic > 0 ? trafficMap[y][x] / maxTraffic : 0;
      const [r, g, b] = viridis(t);
      tempColor.setRGB(r, g, b);
      mesh.setColorAt(i, tempColor);
      i++;
    }
  }
  mesh.instanceMatrix.needsUpdate = true;
  if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
});

// Region lines
const regionLinesPoints = computed(() => {
  const size = gridSize.value;
  if (size === 0) return new Float32Array(0);
  const pts: number[] = [];
  const half = size / 2;
  const numRegions = size / 4;
  for (let i = 0; i <= numRegions; i++) {
    const pos = i * 4 - half;
    pts.push(-half, 0.1, pos, half, 0.1, pos);
    pts.push(pos, 0.1, -half, pos, 0.1, half);
  }
  return new Float32Array(pts);
});

watchEffect(() => {
  if (!regionLinesGeom.value) return;
  const pts = regionLinesPoints.value;
  if (pts.length > 0) {
    regionLinesGeom.value.setAttribute('position', new THREE.BufferAttribute(pts, 3));
  }
});
</script>

<template>
  <div v-if="!config || !tsvGrid" class="flex items-center justify-center h-full text-gray-500">
    Start a simulation to see the heatmap
  </div>
  <div v-else class="absolute inset-0">
    <TresCanvas flat :camera="{ position: cameraPosition, fov: 50 }">
      <TresScene>
        <TresColor attach="background" :args="['#1a1a2e']" />

        <TresInstancedMesh
          ref="heatmapMeshRef"
          :args="[undefined, undefined, instanceCount]"
        >
          <TresBoxGeometry :args="[1, 1, 1]" />
          <TresMeshBasicMaterial vertex-colors :tone-mapped="false" />
        </TresInstancedMesh>

        <TresLineSegments>
          <TresBufferGeometry ref="regionLinesGeom" />
          <TresLineBasicMaterial color="#aaaaaa" :transparent="true" :opacity="0.4" />
        </TresLineSegments>

        <OrbitControls :enable-damping="true" :damping-factor="0.1" />
      </TresScene>
    </TresCanvas>

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
