// Global build queue: processes one heavy scene build per animation frame.
// Prevents UI stall when multiple Three.js views need to build simultaneously
// (e.g., switching to quad view for the first time).

const queue: (() => void)[] = [];
let running = false;

function drain() {
  if (queue.length === 0) {
    running = false;
    return;
  }
  const fn = queue.shift()!;
  fn();
  // Schedule next build for the NEXT frame — browser can paint between builds
  requestAnimationFrame(drain);
}

export function scheduleBuild(fn: () => void) {
  queue.push(fn);
  if (!running) {
    running = true;
    requestAnimationFrame(drain);
  }
}
