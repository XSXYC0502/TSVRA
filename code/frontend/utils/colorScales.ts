// Attempt at a more accurate viridis-like scale
const VIRIDIS_STOPS: [number, number, number][] = [
  [0.267, 0.004, 0.329],
  [0.283, 0.141, 0.458],
  [0.254, 0.265, 0.530],
  [0.207, 0.372, 0.553],
  [0.164, 0.471, 0.558],
  [0.128, 0.567, 0.551],
  [0.135, 0.659, 0.518],
  [0.267, 0.749, 0.441],
  [0.478, 0.821, 0.318],
  [0.741, 0.873, 0.150],
  [0.993, 0.906, 0.144],
];

function sampleGradient(stops: [number, number, number][], t: number): [number, number, number] {
  t = Math.max(0, Math.min(1, t));
  const idx = t * (stops.length - 1);
  const lo = Math.floor(idx);
  const hi = Math.min(lo + 1, stops.length - 1);
  const frac = idx - lo;
  return [
    stops[lo][0] + (stops[hi][0] - stops[lo][0]) * frac,
    stops[lo][1] + (stops[hi][1] - stops[lo][1]) * frac,
    stops[lo][2] + (stops[hi][2] - stops[lo][2]) * frac,
  ];
}

export function viridis(t: number): [number, number, number] {
  return sampleGradient(VIRIDIS_STOPS, t);
}

const INFERNO_STOPS: [number, number, number][] = [
  [0.001, 0.000, 0.014],
  [0.122, 0.047, 0.283],
  [0.304, 0.060, 0.467],
  [0.493, 0.058, 0.455],
  [0.658, 0.131, 0.349],
  [0.797, 0.255, 0.233],
  [0.899, 0.412, 0.130],
  [0.956, 0.595, 0.042],
  [0.964, 0.780, 0.155],
  [0.988, 0.998, 0.645],
];

export function inferno(t: number): [number, number, number] {
  return sampleGradient(INFERNO_STOPS, t);
}

export function interpolateColor(
  color1: [number, number, number],
  color2: [number, number, number],
  t: number
): [number, number, number] {
  return [
    color1[0] + (color2[0] - color1[0]) * t,
    color1[1] + (color2[1] - color1[1]) * t,
    color1[2] + (color2[2] - color1[2]) * t,
  ];
}
