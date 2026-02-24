<script setup lang="ts">
const props = defineProps<{
  min: number;
  max: number;
  label: string;
  colorFn: (t: number) => [number, number, number];
}>();

const stops = Array.from({ length: 20 }, (_, i) => {
  const t = i / 19;
  const [r, g, b] = props.colorFn(t);
  return `rgb(${Math.round(r * 255)}, ${Math.round(g * 255)}, ${Math.round(b * 255)})`;
});

const gradientStyle = `linear-gradient(to top, ${stops.join(', ')})`;
</script>

<template>
  <div class="flex flex-col items-center gap-1">
    <span class="text-xs text-gray-400">{{ label }}</span>
    <div class="w-4 h-32 rounded" :style="{ background: gradientStyle }" />
    <div class="flex justify-between w-full text-xs text-gray-500">
      <span>{{ min }}</span>
      <span>{{ max }}</span>
    </div>
  </div>
</template>
