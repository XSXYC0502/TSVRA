<script setup lang="ts">
withDefaults(defineProps<{ text?: string }>(), {
  text: 'Loading...',
});
</script>

<template>
  <div class="flex items-center justify-center h-full">
    <div class="text-center">
      <div class="w-8 h-8 border-2 border-blue-500 border-t-transparent rounded-full animate-spin mx-auto mb-2" />
      <p class="text-sm text-gray-400">{{ text }}</p>
    </div>
  </div>
</template>
