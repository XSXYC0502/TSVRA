import { ref } from 'vue';
import { useSimulationStore } from '~/stores/simulation';
import type { SpeedMode } from '~/types/simulation';

let ws: WebSocket | null = null;
let reconnectTimer: ReturnType<typeof setTimeout> | null = null;

export const wsConnected = ref(false);

function send(msg: object): void {
  if (ws?.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(msg));
  }
}

function scheduleReconnect(): void {
  if (reconnectTimer) clearTimeout(reconnectTimer);
  reconnectTimer = setTimeout(() => connect(), 2000);
}

export function connect(): void {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const url = `${protocol}//${window.location.host}/api/_ws`;

  ws = new WebSocket(url);

  ws.onopen = () => {
    const store = useSimulationStore();
    store.setWsConnected(true);
    wsConnected.value = true;
  };

  ws.onclose = () => {
    const store = useSimulationStore();
    store.setWsConnected(false);
    wsConnected.value = false;
    scheduleReconnect();
  };

  ws.onerror = () => {
    ws?.close();
  };

  ws.onmessage = (event: MessageEvent) => {
    try {
      const msg = JSON.parse(event.data);
      const store = useSimulationStore();

      switch (msg.type) {
        case 'init':
          store.applyInitMessage(msg);
          break;
        case 'batch':
          store.applyBatchUpdate(msg);
          break;
        case 'done':
          store.applyDoneMessage(msg);
          break;
        case 'process_exit':
          if (store.simStatus !== 'done') {
            store.setSimStatus('idle');
          }
          break;
      }
    } catch (e) {
      console.error('Failed to parse WebSocket message:', e);
    }
  };
}

export function disconnect(): void {
  if (reconnectTimer) clearTimeout(reconnectTimer);
  ws?.close();
  ws = null;
}

export function start(config: {
  numLayers: number;
  gridFactor: number;
  failureMode: 'a' | 'b' | 'c';
  failureRate: number;
  verticalDelay: number;
  horizontalDelay: number;
  cycles: number;
  batchSize: number;
  seed?: number;
}): void {
  send({ type: 'start', config });
  useSimulationStore().setSimStatus('running');
}

export function pause(): void {
  send({ type: 'pause' });
  useSimulationStore().setSimStatus('paused');
}

export function resume(): void {
  send({ type: 'resume' });
  useSimulationStore().setSimStatus('running');
}

export function stop(): void {
  send({ type: 'stop' });
}

export function step(): void {
  send({ type: 'step' });
  useSimulationStore().setSimStatus('stepping');
}

export function setSpeed(mode: SpeedMode): void {
  send({ type: 'speed', mode });
  useSimulationStore().setSpeedMode(mode);
}

export function useWebSocket() {
  return {
    connect,
    disconnect,
    start,
    pause,
    resume,
    stop,
    step,
    setSpeed,
    wsConnected,
  };
}
