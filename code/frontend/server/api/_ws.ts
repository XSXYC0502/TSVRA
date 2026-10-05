import type { Peer } from 'crossws';
import { SimulationManager } from '../utils/simulationManager';
import { isRecord, validateLaunchConfig } from '../../shared/simulationConfig';

interface ClientState {
  sim: SimulationManager | null;
  initReceived: boolean;
  paused: boolean;
  cppFinished: boolean;
  stopped: boolean;
}

const clientStates = new WeakMap<Peer, ClientState>();

function getState(peer: Peer): ClientState {
  let state = clientStates.get(peer);
  if (!state) {
    state = {
      sim: null,
      initReceived: false,
      paused: false,
      cppFinished: false,
      stopped: false,
    };
    clientStates.set(peer, state);
  }
  return state;
}

function sendErrorToPeer(peer: Peer, code: string, message: string): void {
  try {
    peer.send(JSON.stringify({ type: 'error', code, message }));
  } catch {
    // peer may already be disconnected
  }
}

function checkBinaryAvailability(): { available: boolean; path: string | null } {
  const tmpMgr = new SimulationManager(() => {}, () => {}, () => {}, () => {});
  const binPath = tmpMgr.getBinPath();
  return { available: binPath !== null, path: binPath };
}

export default defineWebSocketHandler({
  open(peer) {
    console.log('WebSocket client connected');
    getState(peer);

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
      const value: unknown = JSON.parse(message.text());
      if (!isRecord(value)) {
        sendErrorToPeer(peer, 'invalid_message', 'Message must be an object.');
        return;
      }
      parsed = value;
    } catch {
      sendErrorToPeer(peer, 'invalid_message', 'Message must contain valid JSON.');
      return;
    }

    switch (parsed.type) {
      case 'start': {
        const validation = validateLaunchConfig(parsed.config);
        if (!validation.ok) {
          sendErrorToPeer(peer, 'invalid_config', validation.errors.join(' '));
          return;
        }
        console.log(`[WS-SRV] <<< start`);
        if (state.sim?.isRunning()) {
          console.log(`[WS-SRV] killing previous sim`);
          state.sim.kill();
        }
        state.initReceived = false;
        state.paused = false;
        state.cppFinished = false;
        state.stopped = false;

        const onMessage = (msg: unknown): void => {
          if (state.stopped || state.sim !== sim || !isRecord(msg)) return;

          const msgType = msg.type;

          // init message: always forward immediately
          if (msgType === 'init') {
            console.log(`[WS-SRV] onMessage: init → forwarding`);
            state.initReceived = true;
            peer.send(JSON.stringify(msg));
            return;
          }

          // done message: always forward immediately
          if (msgType === 'done') {
            console.log(`[WS-SRV] onMessage: done → forwarding`);
            state.cppFinished = true;
            peer.send(JSON.stringify(msg));
            return;
          }

          // A cycle already executed before pause must still update the browser.
          const cycle = msg.cycle;
          if (typeof cycle === 'number' && (cycle <= 3 || cycle % 1000 === 0)) {
            console.log(`[WS-SRV] onMessage: cycle ${cycle} → forward`);
          }
          peer.send(JSON.stringify(msg));
        };

        const onExit = (code: number | null): void => {
          if (state.sim !== sim) return;
          console.log(`[WS-SRV] onExit: code=${code} stopped=${state.stopped}`);
          state.cppFinished = true;

          if (state.stopped) {
            state.sim = null;
            return;
          }

          peer.send(JSON.stringify({ type: 'process_exit', code }));
          state.sim = null;
        };

        const onError = (err: Error): void => {
          if (state.sim !== sim) return;
          const code = err.message.includes('not found') ? 'binary_not_found' : 'spawn_failed';
          sendErrorToPeer(peer, code, err.message);
        };

        const onStderr = (line: string): void => {
          if (state.sim !== sim) return;
          try {
            peer.send(JSON.stringify({ type: 'stderr', line }));
          } catch {
            // peer may be disconnected
          }
        };

        const sim = new SimulationManager(onMessage, onExit, onError, onStderr);
        state.sim = sim;
        sim.start(validation.config);

        // Lockstep: always pause C++ immediately so it waits for browser ack
        console.log(`[WS-SRV] start: sending immediate 'p' (lockstep)`);
        state.sim.pause();
        break;
      }

      case 'ack': {
        const count = parsed.count ?? 1;
        if (typeof count !== 'number' || !Number.isInteger(count) || count < 1 || count > 100) {
          sendErrorToPeer(peer, 'invalid_ack', 'Ack count must be an integer between 1 and 100.');
          return;
        }
        if (!state.paused && !state.cppFinished && state.sim) {
          state.sim.sendCommand('n'.repeat(count));
        }
        break;
      }

      case 'pause': {
        console.log(`[WS-SRV] <<< pause`);
        state.paused = true;
        // C++ is already paused (lockstep) or will pause after current cycle
        state.sim?.pause();
        break;
      }

      case 'resume': {
        console.log(`[WS-SRV] <<< resume`);
        state.paused = false;
        // Browser sends ack to kick-start; no server 'n' needed
        break;
      }

      case 'speed': {
        console.log(`[WS-SRV] <<< speed`);
        // Flush: 'p' clears C++ steps_remaining_ to 0
        if (!state.cppFinished && state.sim) {
          state.sim.pause();
        }
        break;
      }

      case 'stop': {
        console.log(`[WS-SRV] <<< stop | sim=${!!state.sim} cppFinished=${state.cppFinished}`);
        state.paused = true;
        if (state.sim) {
          state.sim.stop();
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
      state.stopped = true;
      state.sim?.kill();
      state.sim = null;
    }
  },

  error(peer, error) {
    console.error('WebSocket error:', error);
  },
});
