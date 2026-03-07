# Three.js InstancedMesh Color Gotchas

Notes on bugs encountered while building the 3D visualization views (Grid3DView, HeatmapView, FailureView) with Three.js `InstancedMesh`.

## Problem: InstancedMesh appears all black

When using `setColorAt()` on an `InstancedMesh` with `MeshLambertMaterial` or `MeshStandardMaterial`, all instances render as black regardless of the colors set.

### Root Causes

**1. Do NOT use `vertexColors: true` with `setColorAt()`**

`vertexColors: true` tells the material shader to read per-vertex color attributes from the geometry's `color` buffer attribute. `BoxGeometry` does not have a `color` attribute, so the shader reads `(0, 0, 0)` (black) for every vertex. The final color is `instanceColor * vertexColor = instanceColor * black = black`.

`setColorAt()` uses a completely separate mechanism — it sets per-instance colors via the `instanceColor` buffer attribute, which does NOT require `vertexColors: true`.

```ts
// WRONG - instances will be black
const mat = new THREE.MeshLambertMaterial({ vertexColors: true });

// CORRECT - instance colors work as expected
const mat = new THREE.MeshLambertMaterial();
```

**2. Set `material.needsUpdate = true` after first `setColorAt()`**

When an `InstancedMesh` is created, it has no `instanceColor` buffer. The first call to `setColorAt()` creates this buffer. However, the shader may have already been compiled without `instanceColor` support. Setting `material.needsUpdate = true` forces shader recompilation to include the `instanceColor` attribute.

```ts
// After setting all colors:
mesh.instanceMatrix.needsUpdate = true;
if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
(mesh.material as THREE.Material).needsUpdate = true;  // force shader recompile
```

**3. Set `renderer.toneMapping = THREE.NoToneMapping`**

Three.js default tone mapping can significantly darken colors, especially with PBR materials (`MeshStandardMaterial`). All 3D views in this project use `NoToneMapping` for faithful color reproduction.

### Working Pattern (used in FailureView)

```ts
// Material: no vertexColors, toneMapped false
const mat = new THREE.MeshLambertMaterial({ toneMapped: false });
const mesh = new THREE.InstancedMesh(geometry, mat, count);

// Renderer: disable tone mapping
renderer.toneMapping = THREE.NoToneMapping;

// Set colors
for (let i = 0; i < count; i++) {
  color.setRGB(r, g, b);
  mesh.setColorAt(i, color);
}
mesh.instanceMatrix.needsUpdate = true;
if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
(mesh.material as THREE.Material).needsUpdate = true;
```

### Note on Grid3DView

Grid3DView previously used `vertexColors: true` with `setColorAt()`, which caused all instances to render black. This was fixed by removing `vertexColors: true` and adding `material.needsUpdate = true` — the same pattern used in FailureView and HeatmapView.

## References

- [Three.js InstancedMesh docs](https://threejs.org/docs/#api/en/objects/InstancedMesh)
- [setColorAt unexpected behavior (Three.js forum)](https://discourse.threejs.org/t/instancedmesh-setcolorat-unexpected-behavior/51483)
- [instanceColor not defined when populated asynchronously (GitHub #21786)](https://github.com/mrdoob/three.js/issues/21786)
