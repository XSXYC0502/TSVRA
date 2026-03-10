import { ref } from 'vue';
import { useSimulationStore } from '~/stores/simulation';
import type { SpeedMode } from '~/types/simulation';

let ws: WebSocket | null = null;
let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
let ackTimer: ReturnType<typeof setTimeout> | null = null;

export const wsConnected = ref(false);

// Lockstep flow control: batch sizes and delays per speed mode
const ACK_BATCH: Record<SpeedMode, number> = {
  high: 100,
  medium: 1,
  slow: 1,
  auto: 1,
  step: 1,
};

const ACK_DELAY: Record<SpeedMode, number> = {
  high: 0,
  medium: 10,
  slow: 50,
  auto: 0,
  step: Infinity, // manual — never auto-ack
};

let cyclesSinceAck = 0;

function send(msg: object): void {
  const ready = ws?.readyState === WebSocket.OPEN;
  if (ready) {
    ws!.send(JSON.stringify(msg));
  }
}

function sendAck(count: number): void {
  send({ type: 'ack', count });
}

function scheduleAck(mode: SpeedMode, keyEvent: boolean): void {
  if (ackTimer) {
    clearTimeout(ackTimer);
    ackTimer = null;
  }

  const batch = ACK_BATCH[mode];
  const delay = (mode === 'auto' && keyEvent) ? 500 : ACK_DELAY[mode];

  if (delay === Infinity) return; // step mode: wait for manual ack

  if (delay > 0) {
    ackTimer = setTimeout(() => {
      ackTimer = null;
      sendAck(batch);
    }, delay);
  } else {
    sendAck(batch);
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

      if (msg.type === 'cycle') {
        if (msg.cycle % 100 === 0) {
          console.log(`[WS-CLIENT] recv: cycle ${msg.cycle}`);
        }
      } else {
        console.log(`[WS-CLIENT] recv: ${msg.type}`, msg.type === 'stderr' ? '' : JSON.stringify(msg).slice(0, 200));
      }

      switch (msg.type) {
        case 'init':
          store.applyInitMessage(msg);
          // Lockstep: ack init to trigger first batch of cycles
          cyclesSinceAck = 0;
          sendAck(ACK_BATCH[store.speedMode]);
          break;

        case 'cycle':
          if (store.simStatus === 'paused') break;

          store.applyCycleUpdate(msg);

          // Lockstep: ack after processing a batch
          if (store.speedMode !== 'step') {
            cyclesSinceAck++;
            const batch = ACK_BATCH[store.speedMode];
            if (cyclesSinceAck >= batch) {
              cyclesSinceAck = 0;
              scheduleAck(store.speedMode, msg.keyEvent === true);
            }
          }
          break;

        case 'done':
          store.applyDoneMessage(msg);
          break;

        case 'process_exit':
          if (store.simStatus !== 'done') {
            store.setSimStatus('idle');
          }
          break;

        case 'error':
          store.setError({ code: msg.code, message: msg.message });
          break;

        case 'status':
          store.setBinaryStatus(msg.binaryAvailable, msg.binaryPath);
          break;

        case 'stderr':
          store.addStderrLine(msg.line);
          break;
      }
    } catch (e) {
      console.error('Failed to parse WebSocket message:', e);
    }
  };
}

export function disconnect(): void {
  if (reconnectTimer) clearTimeout(reconnectTimer);
  if (ackTimer) clearTimeout(ackTimer);
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
  seed?: number;
}): void {
  const store = useSimulationStore();
  store.clearError();
  store.setSimStatus('running');
  cyclesSinceAck = 0;
  if (ackTimer) { clearTimeout(ackTimer); ackTimer = null; }
  send({ type: 'start', config });
}

export function pause(): void {
  send({ type: 'pause' });
  const store = useSimulationStore();
  store.setSimStatus('paused');
  cyclesSinceAck = 0;
  if (ackTimer) { clearTimeout(ackTimer); ackTimer = null; }
}

export function resume(): void {
  send({ type: 'resume' });
  const store = useSimulationStore();
  store.setSimStatus('running');
  cyclesSinceAck = 0;
  if (ackTimer) { clearTimeout(ackTimer); ackTimer = null; }
  // Kick-start with current batch size
  if (store.speedMode !== 'step') {
    sendAck(ACK_BATCH[store.speedMode]);
  }
}

export function stop(): void {
  send({ type: 'stop' });
  useSimulationStore().setSimStatus('idle');
  cyclesSinceAck = 0;
  if (ackTimer) { clearTimeout(ackTimer); ackTimer = null; }
}

export function step(): void {
  // In lockstep, step = send one ack credit
  sendAck(1);
  useSimulationStore().setSimStatus('stepping');
}

export function setSpeed(mode: SpeedMode): void {
  const store = useSimulationStore();
  store.setSpeedMode(mode);

  // Always clear pending ack state
  if (ackTimer) { clearTimeout(ackTimer); ackTimer = null; }
  cyclesSinceAck = 0;

  // If simulation is active, flush old credits and kick-start new mode
  const active = store.simStatus === 'running' || store.simStatus === 'stepping';
  if (active) {
    send({ type: 'speed' });          // server sends 'p' to C++ → flushes old steps_remaining_
    if (mode !== 'step') {
      store.setSimStatus('running');
      sendAck(ACK_BATCH[mode]);       // kick-start with new batch size
    }
  }
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
