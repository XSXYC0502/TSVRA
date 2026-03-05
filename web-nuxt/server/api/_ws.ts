import type { Peer } from 'crossws';
import { SimulationManager, type SimConfig } from '../utils/simulationManager';

type SpeedMode = 'slow' | 'medium' | 'high' | 'auto' | 'step';

const SPEED_DELAYS: Record<string, number> = {
  slow: 500,
  medium: 100,
  high: 10,
  auto: 10,
};

interface ClientState {
  sim: SimulationManager | null;
  speedMode: SpeedMode;
  messageQueue: unknown[];
  forwardTimer: ReturnType<typeof setInterval> | null;
  pendingStep: boolean;
}

const clientStates = new WeakMap<Peer, ClientState>();

function getState(peer: Peer): ClientState {
  let state = clientStates.get(peer);
  if (!state) {
    state = {
      sim: null,
      speedMode: 'high',
      messageQueue: [],
      forwardTimer: null,
      pendingStep: false,
    };
    clientStates.set(peer, state);
  }
  return state;
}

function startForwarding(peer: Peer, state: ClientState): void {
  stopForwarding(state);

  if (state.speedMode === 'step') {
    return;
  }

  // High mode: forward immediately in onMessage callback, no timer needed
  if (state.speedMode === 'high') {
    return;
  }

  const delay = SPEED_DELAYS[state.speedMode] ?? 10;

  state.forwardTimer = setInterval(() => {
    // Drain up to 10 messages per tick for better throughput
    let count = 0;
    while (state.messageQueue.length > 0 && count < 10) {
      const msg = state.messageQueue.shift()!;
      peer.send(JSON.stringify(msg));
      count++;
    }

    // In auto mode, slow down when there's a key event
    if (state.speedMode === 'auto' && count > 0 && isKeyEvent(state.messageQueue[0])) {
      stopForwarding(state);
      setTimeout(() => {
        if (state.speedMode === 'auto') {
          startForwarding(peer, state);
        }
      }, 500);
    }
  }, delay);
}

function stopForwarding(state: ClientState): void {
  if (state.forwardTimer !== null) {
    clearInterval(state.forwardTimer);
    state.forwardTimer = null;
  }
}

function isKeyEvent(msg: unknown): boolean {
  if (typeof msg === 'object' && msg !== null) {
    const record = msg as Record<string, unknown>;
    return record['keyEvent'] === true;
  }
  return false;
}

function forwardStepMessage(peer: Peer, state: ClientState): void {
  if (state.messageQueue.length === 0) {
    state.pendingStep = true;
    return;
  }

  const msg = state.messageQueue.shift()!;
  peer.send(JSON.stringify(msg));
}

function sendErrorToPeer(peer: Peer, code: string, message: string): void {
  try {
    peer.send(JSON.stringify({ type: 'error', code, message }));
  } catch {
    // peer may already be disconnected
  }
}

function checkBinaryAvailability(): { available: boolean; path: string | null } {
  // Create a temporary manager just to check the binary path
  const tmpMgr = new SimulationManager(() => {}, () => {}, () => {}, () => {});
  const binPath = tmpMgr.getBinPath();
  return { available: binPath !== null, path: binPath };
}

export default defineWebSocketHandler({
  open(peer) {
    console.log('WebSocket client connected');
    getState(peer);

    // Send binary health check on connect
    const { available, path: binPath } = checkBinaryAvailability();
    peer.send(JSON.stringify({
      type: 'status',
      binaryAvailable: available,
      binaryPath: binPath,
    }));
  },

  message(peer, message) {
    const state = getState(peer);
    let parsed: Record<string, unknown>;
    try {
      parsed = JSON.parse(message.text());
    } catch {
      console.error('Invalid JSON from client:', message.text());
      return;
    }

    switch (parsed.type) {
      case 'start': {
        if (state.sim?.isRunning()) {
          state.sim.kill();
        }
        stopForwarding(state);
        state.messageQueue = [];

        const onMessage = (msg: unknown): void => {
          if (state.speedMode === 'step') {
            state.messageQueue.push(msg);
            if (state.pendingStep) {
              state.pendingStep = false;
              forwardStepMessage(peer, state);
            }
            return;
          }

          // High mode: forward immediately, bypass queue
          if (state.speedMode === 'high') {
            peer.send(JSON.stringify(msg));
            return;
          }

          state.messageQueue.push(msg);
        };

        const onExit = (code: number | null): void => {
          // Flush remaining messages
          for (const msg of state.messageQueue) {
            peer.send(JSON.stringify(msg));
          }
          state.messageQueue = [];
          stopForwarding(state);

          peer.send(JSON.stringify({ type: 'process_exit', code }));
          state.sim = null;
        };

        const onError = (err: Error): void => {
          const code = err.message.includes('not found') ? 'binary_not_found' : 'spawn_failed';
          sendErrorToPeer(peer, code, err.message);
        };

        const onStderr = (line: string): void => {
          try {
            peer.send(JSON.stringify({ type: 'stderr', line }));
          } catch {
            // peer may be disconnected
          }
        };

        state.sim = new SimulationManager(onMessage, onExit, onError, onStderr);
        state.sim.start(parsed.config as SimConfig);
        startForwarding(peer, state);
        break;
      }

      case 'pause': {
        state.sim?.pause();
        break;
      }

      case 'resume': {
        state.sim?.resume();
        break;
      }

      case 'stop': {
        state.sim?.stop();
        break;
      }

      case 'step': {
        if (state.speedMode === 'step') {
          forwardStepMessage(peer, state);
        }
        state.sim?.step();
        break;
      }

      case 'speed': {
        const mode = parsed.mode as SpeedMode;
        if (['slow', 'medium', 'high', 'auto', 'step'].includes(mode)) {
          state.speedMode = mode;
          stopForwarding(state);

          // Flush queued messages when switching to high mode
          if (mode === 'high') {
            for (const msg of state.messageQueue) {
              peer.send(JSON.stringify(msg));
            }
            state.messageQueue = [];
          }

          if (mode !== 'step') {
            startForwarding(peer, state);
          }
        }
        break;
      }

      default: {
        console.warn('Unknown message type:', parsed.type);
      }
    }
  },

  close(peer) {
    console.log('WebSocket client disconnected');
    const state = clientStates.get(peer);
    if (state) {
      stopForwarding(state);
      if (state.sim?.isRunning()) {
        state.sim.kill();
      }
    }
  },

  error(peer, error) {
    console.error('WebSocket error:', error);
  },
});
