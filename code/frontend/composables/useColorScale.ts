import { computed } from 'vue';
import { viridis } from '~/utils/colorScales';
import type { ColorMode, TSVState } from '~/types/simulation';

export function useColorScale(mode: ColorMode) {
  return computed(() => {
    return (tsv: TSVState, maxUsage: number): [number, number, number] => {
      if (tsv.failed) return [1, 0.2, 0.2];

      switch (mode) {
        case 'type':
          return tsv.redundant ? [1, 0.84, 0] : [0.27, 0.53, 1];
        case 'traffic': {
          const t = maxUsage > 0 ? tsv.usageCount / maxUsage : 0;
          return viridis(t);
        }
        case 'status':
          return [0.27, 1, 0.27];
      }
    };
  });
}
