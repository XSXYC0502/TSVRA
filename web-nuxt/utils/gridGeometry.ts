export function isRedundantPosition(x: number, y: number): boolean {
  const modX = x % 4;
  const modY = y % 4;
  return (modX === 0 || modX === 3) && (modY === 0 || modY === 3);
}

export function getRegion(x: number, y: number): { rx: number; ry: number } {
  return { rx: Math.floor(x / 4), ry: Math.floor(y / 4) };
}

export function getRegionBounds(rx: number, ry: number): { x1: number; y1: number; x2: number; y2: number } {
  return { x1: rx * 4, y1: ry * 4, x2: rx * 4 + 3, y2: ry * 4 + 3 };
}
