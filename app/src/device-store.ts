import {
  banksFromDump,
  cloneBanks,
  createEmptyBanks,
  type PatternBanks,
} from "./pattern";
import {
  clampBpm,
  clampSwing,
  clampVolume,
  decodePatternHex,
  parseHostLine,
  NOTE_CHH,
  NOTE_CLAP,
  NOTE_KICK,
  NOTE_OHH,
  NOTE_RIM,
  NOTE_SNARE,
  type HostInbound,
  type SyncStatus,
} from "./protocol";

/** Physical pad index (S1=0 … S6=5) for each drum note. */
const NOTE_TO_PAD: Record<number, number> = {
  [NOTE_CHH]: 0,
  [NOTE_OHH]: 1,
  [NOTE_CLAP]: 2,
  [NOTE_RIM]: 3,
  [NOTE_KICK]: 4,
  [NOTE_SNARE]: 5,
};

export type DeviceLink = "none" | "ble" | "serial" | "midi";

export type DeviceState = {
  sync: SyncStatus;
  connected: boolean;
  deviceName: string;
  link: DeviceLink;
  protocolVersion: number;
  caps: string[];
  bpm: number;
  bpmDraft: number | null;
  running: boolean;
  beatInBar: number;
  step: number;
  bar: number;
  tick: number;
  accent: boolean;
  swing: number;
  variation: number;
  fill: boolean;
  click: boolean;
  drumMode: boolean;
  volume: number;
  volumeDraft: number | null;
  revision: number;
  pattern: PatternBanks;
  patternDirty: boolean;
  lastError: string | null;
  updatedAt: number;
  lastPadNote: number | null;
  /** S1–S8 held state from device key events. */
  keysDown: boolean[];
  /** Momentary UI flash deadlines (ms) for each physical key. */
  keyFlashUntil: number[];
};

export function createInitialState(): DeviceState {
  return {
    sync: "disconnected",
    connected: false,
    deviceName: "未连接",
    link: "none",
    protocolVersion: 0,
    caps: [],
    bpm: 120,
    bpmDraft: null,
    running: false,
    beatInBar: 0,
    step: 0,
    bar: 0,
    tick: 0,
    accent: false,
    swing: 50,
    variation: 0,
    fill: false,
    click: true,
    drumMode: false,
    volume: 100,
    volumeDraft: null,
    revision: 1,
    pattern: createEmptyBanks(1),
    patternDirty: false,
    lastError: null,
    updatedAt: Date.now(),
    lastPadNote: null,
    keysDown: Array(8).fill(false),
    keyFlashUntil: Array(8).fill(0),
  };
}

function applyPatternBank(state: DeviceState, bank: number, rev: number, hex: string): DeviceState {
  const tracks = decodePatternHex(hex);
  if (!tracks) {
    return { ...state, lastError: "pattern decode failed", updatedAt: Date.now() };
  }
  /* While the user is editing, keep the local draft; otherwise mirror device. */
  if (state.patternDirty) {
    return { ...state, revision: rev, sync: "synced", updatedAt: Date.now() };
  }
  const pattern = cloneBanks(state.pattern);
  if (bank === 0) pattern.a = tracks;
  else if (bank === 1) pattern.b = tracks;
  else pattern.fill = tracks;
  pattern.revision = rev;
  return {
    ...state,
    pattern,
    revision: rev,
    sync: "synced",
    updatedAt: Date.now(),
  };
}

export function reduceHostMessage(state: DeviceState, msg: HostInbound): DeviceState {
  const now = Date.now();
  switch (msg.t) {
    case "hello": {
      const name = msg.name && /beatbox|easyinput/i.test(msg.name) ? msg.name : "EasyInput Beatbox";
      return {
        ...state,
        connected: true,
        link: state.link === "ble" ? "ble" : "serial",
        deviceName: name,
        protocolVersion: msg.v ?? 1,
        caps: msg.caps ?? [],
        sync: state.sync === "synced" ? "synced" : "connecting",
        updatedAt: now,
      };
    }
    case "state": {
      const bpm = clampBpm(msg.bpm);
      const ignoreBpm = state.bpmDraft != null && state.bpmDraft !== bpm;
      const volume = msg.vol != null ? clampVolume(msg.vol) : state.volume;
      const ignoreVolume = state.volumeDraft != null && state.volumeDraft !== volume;
      return {
        ...state,
        connected: true,
        sync: "synced",
        bpm: ignoreBpm ? state.bpm : bpm,
        bpmDraft: ignoreBpm ? state.bpmDraft : null,
        running: !!msg.run,
        beatInBar: (msg.beat ?? 0) & 0x03,
        step: (msg.step ?? state.step) & 0x0f,
        bar: msg.bar ?? state.bar,
        tick: msg.tick ?? state.tick,
        accent: ((msg.beat ?? 0) & 0x03) === 0,
        swing: msg.swing != null ? clampSwing(msg.swing) : state.swing,
        variation: msg.var != null ? (msg.var ? 1 : 0) : state.variation,
        fill: msg.fill != null ? !!msg.fill : state.fill,
        click: msg.click != null ? !!msg.click : state.click,
        drumMode: msg.mode != null ? !!msg.mode : state.drumMode,
        volume: ignoreVolume ? state.volume : volume,
        volumeDraft: ignoreVolume ? state.volumeDraft : null,
        revision: msg.rev ?? state.revision,
        updatedAt: now,
      };
    }
    case "beat":
      return {
        ...state,
        running: true,
        beatInBar: msg.beat & 0x03,
        step: msg.step != null ? msg.step & 0x0f : state.step,
        accent: !!msg.accent,
        sync: state.connected ? "synced" : state.sync,
        updatedAt: now,
      };
    case "position":
      return {
        ...state,
        bar: msg.bar,
        step: msg.step & 0x0f,
        beatInBar: msg.beat & 0x03,
        tick: msg.tick,
        accent: !!msg.accent,
        running: true,
        updatedAt: now,
      };
    case "start":
      return { ...state, running: true, updatedAt: now };
    case "continue":
      return { ...state, running: true, updatedAt: now };
    case "stop":
      return { ...state, running: false, updatedAt: now };
    case "pattern":
      return applyPatternBank(state, msg.bank, msg.rev, msg.p);
    case "pattern_dump": {
      const banks = banksFromDump(msg);
      if (!banks) {
        return { ...state, lastError: "pattern_dump decode failed", updatedAt: now };
      }
      if (state.patternDirty) {
        return { ...state, revision: banks.revision, sync: "synced", updatedAt: now };
      }
      return {
        ...state,
        pattern: banks,
        revision: banks.revision,
        sync: "synced",
        updatedAt: now,
      };
    }
    case "ack":
      if (msg.cmd === "pattern_set" && msg.ok) {
        return {
          ...state,
          revision: msg.rev ?? state.revision,
          pattern: { ...state.pattern, revision: msg.rev ?? state.revision },
          patternDirty: false,
          updatedAt: now,
        };
      }
      return { ...state, updatedAt: now };
    case "error":
      return {
        ...state,
        lastError: msg.msg ?? msg.cmd ?? "error",
        updatedAt: now,
      };
    case "note": {
      const pad = NOTE_TO_PAD[msg.n];
      const keyFlashUntil = [...state.keyFlashUntil];
      if (pad != null) {
        keyFlashUntil[pad] = now + 160;
      }
      return { ...state, lastPadNote: msg.n, keyFlashUntil, updatedAt: now };
    }
    case "key": {
      const idx = msg.i & 0x07;
      const keysDown = [...state.keysDown];
      const keyFlashUntil = [...state.keyFlashUntil];
      const down = !!msg.v;
      keysDown[idx] = down;
      if (down) {
        keyFlashUntil[idx] = now + 180;
      }
      return { ...state, keysDown, keyFlashUntil, updatedAt: now };
    }
    default:
      return state;
  }
}

export function reduceHostLine(state: DeviceState, line: string): DeviceState {
  const msg = parseHostLine(line);
  if (!msg) return state;
  return reduceHostMessage(state, msg);
}

export function markConnecting(
  state: DeviceState,
  link: Exclude<DeviceLink, "none" | "midi"> = "serial",
): DeviceState {
  return {
    ...state,
    connected: true,
    sync: "connecting",
    link,
    deviceName: "EasyInput Beatbox",
    patternDirty: false,
    updatedAt: Date.now(),
  };
}

export function markDisconnected(state: DeviceState, label = "未连接"): DeviceState {
  return {
    ...state,
    connected: false,
    sync: "disconnected",
    link: "none",
    deviceName: label,
    running: false,
    bpmDraft: null,
    volumeDraft: null,
    patternDirty: false,
    updatedAt: Date.now(),
  };
}

export function applyLocalBpmDraft(state: DeviceState, bpm: number): DeviceState {
  const v = clampBpm(bpm);
  return { ...state, bpm: v, bpmDraft: v, updatedAt: Date.now() };
}

export function clearBpmDraft(state: DeviceState): DeviceState {
  return { ...state, bpmDraft: null, updatedAt: Date.now() };
}

export function applyLocalVolumeDraft(state: DeviceState, volume: number): DeviceState {
  const v = clampVolume(volume);
  return { ...state, volume: v, volumeDraft: v, updatedAt: Date.now() };
}

export function clearVolumeDraft(state: DeviceState): DeviceState {
  return { ...state, volumeDraft: null, updatedAt: Date.now() };
}

export function applyLocalPattern(state: DeviceState, pattern: PatternBanks): DeviceState {
  return {
    ...state,
    pattern: cloneBanks(pattern),
    patternDirty: true,
    updatedAt: Date.now(),
  };
}
