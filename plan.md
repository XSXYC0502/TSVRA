# Fix Plan: Blank Heatmap and Failure Map Views

## Problem Analysis

After exploring the codebase, I've identified the root cause of the blank views:

**Heatmap View (HeatmapView.vue):**
- Uses `v-show` for visibility toggling in index.vue (lines 118-121)
- Scene builds only when `isVisible` is true (line 264)
- Camera position issue: `camera.position.set(0, cameraHeight, cameraHeight * 0.01)` (line 118)
  - The Z position is `cameraHeight * 0.01` which is extremely small (~0.224 for size 16)
  - Camera is essentially at (0, 22.4, 0.224) looking at origin
  - This creates a nearly top-down view with almost no depth
  - The heatmap plane is at y=0, so camera should have more Z distance

**Failure View (FailureView.vue):**
- Similar `v-show` visibility pattern
- Uses a 2x2 grid layout with 3D canvas + 2 charts
- Canvas container ref at line 389: `<div ref="canvasContainer" class="absolute inset-0 top-8" />`
- The `top-8` offset (32px) for the header might cause dimension issues
- Camera position seems reasonable: `camera.position.set(15, 15, 15)` (line 193)

**Common Issues:**
1. Both views use `v-show` which keeps DOM elements but sets `display: none`
2. When switching views, container dimensions might be 0 initially
3. Both have retry logic via `requestAnimationFrame` but may fail if timing is off
4. Scene building happens in `onMounted` and `watch` hooks

## Fix Strategy

### Phase 1: Fix Heatmap Camera Position
- Adjust camera Z position to provide proper viewing angle
- Ensure camera looks down at the heatmap plane correctly

### Phase 2: Fix Failure View Layout
- Review the grid layout and canvas positioning
- Ensure proper dimensions for the 3D canvas container
- Check if `top-8` offset causes issues

### Phase 3: Improve View Switching Robustness
- Ensure scenes rebuild reliably when switching views
- Add better dimension checking and retry logic

## Detailed Tasks

### Task 1: Fix Heatmap Camera Position ✓
**File:** `web-nuxt/components/heatmap/HeatmapView.vue`
**Line:** 118
**Change:**
```typescript
// Current (WRONG):
camera.position.set(0, cameraHeight, cameraHeight * 0.01);

// Fixed:
camera.position.set(0, cameraHeight, cameraHeight * 0.8);
```
**Reason:** Camera needs proper Z distance to see the 2D heatmap plane

### Task 2: Fix Failure View Canvas Container
**File:** `web-nuxt/components/failure/FailureView.vue`
**Line:** 389
**Change:** Review if `top-8` offset causes dimension calculation issues
**Action:** Test if removing offset or adjusting layout fixes the blank view

### Task 3: Add Debug Logging (Optional)
**Action:** Add console logs to track:
- Container dimensions when buildScene() is called
- Whether scene/renderer/camera are properly initialized
- Any Three.js errors

### Task 4: Test View Switching
**Action:** After fixes, test all view transitions:
- Dashboard → Heatmap
- Dashboard → Failure
- Quad view (all 4 views simultaneously)
- Ensure scenes render correctly on first show

## Expected Outcome

After fixes:
- Heatmap view shows 2D traffic density with proper camera angle
- Failure view shows 3D failure map + charts
- Both views render correctly when switching from other views
- Quad view shows all 4 views simultaneously without blank areas
