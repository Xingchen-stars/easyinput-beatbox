/** Shared host-protocol v3 types and helpers (BLE primary, USB fallback). */

export const BPM_MIN = 60;
export const BPM_MAX = 240;
export const SWING_MIN = 50;
export const SWING_MAX = 75;
export const VOLUME_MIN = 0;
export const VOLUME_MAX = 127;
export const STEPS = 16;
export const TRACKS = 6;
export const PPQN_INTERNAL = 96;
export const PPQN_MIDI = 24;
export const TICKS_PER_16TH = 24;
export const TICKS_PER_QUARTER = 96;
export const TICKS_PER_BAR = 384;

export const NOTE_KICK = 36;
export const NOTE_RIM = 37;
export const NOTE_SNARE = 38;
export const NOTE_CLAP = 39;
export const NOTE_CHH = 42;
export const NOTE_OHH = 46;
export const NOTE_CLICK_ACCENT = 76;
export const NOTE_CLICK_NORMAL = 77;

export const TRACK_NOTES = [
  NOTE_KICK,
  NOTE_SNARE,
  NOTE_CHH,
  NOTE_OHH,
  NOTE_CLAP,
  NOTE_RIM,
] as const;
export const TRACK_LABELS = ["KICK", "SNARE", "CHH", "OHH", "CLAP", "RIM"] as const;

export type SyncStatus = "disconnected" | "connecting" | "synced" | "stale";

export type HostInbound =
  | { t: "hello"; v?: number; name?: string; caps?: string[] }
  | {
      t: "state";
      bpm: number;
      run: number;
      beat: number;
      step?: number;
      bar?: number;
      tick?: number;
      swing?: number;
      var?: number;
      fill?: number;
      rev?: number;
      click?: number;
      mode?: number;
      vol?: number;
    }
  | { t: "beat"; accent: number; beat: number; step?: number }
  | { t: "position"; bar: number; step: number; beat: number; tick: number; accent?: number }
  | { t: "note"; n: number; v: number }
  | { t: "key"; i: number; v: number }
  | { t: "pattern"; bank: number; rev: number; p: string }
  | { t: "pattern_dump"; rev: number; a: string; b: string; f: string }
  | { t: "start" }
  | { t: "continue" }
  | { t: "stop" }
  | { t: "ack"; cmd: string; ok: number; rev?: number }
  | { t: "error"; cmd?: string; msg?: string };

export function clampBpm(bpm: number): number {
  return Math.max(BPM_MIN, Math.min(BPM_MAX, Math.round(bpm)));
}

export function clampSwing(swing: number): number {
  return Math.max(SWING_MIN, Math.min(SWING_MAX, Math.round(swing)));
}

export function clampVolume(volume: number): number {
  return Math.max(VOLUME_MIN, Math.min(VOLUME_MAX, Math.round(volume)));
}

export function parseHostLine(line: string): HostInbound | null {
  if (!line.startsWith("{")) return null;
  try {
    const msg = JSON.parse(line) as HostInbound;
    if (!msg || typeof msg !== "object" || typeof (msg as { t?: unknown }).t !== "string") {
      return null;
    }
    return msg;
  } catch {
    return null;
  }
}

export function encodePatternHex(tracks: number[][]): string {
  let out = "";
  for (let t = 0; t < TRACKS; t++) {
    for (let s = 0; s < STEPS; s++) {
      const v = Math.max(0, Math.min(127, Math.round(tracks[t]?.[s] ?? 0)));
      out += v.toString(16).padStart(2, "0");
    }
  }
  return out;
}

export function decodePatternHex(hex: string): number[][] | null {
  if (!hex || hex.length !== TRACKS * STEPS * 2) return null;
  const tracks: number[][] = [];
  let i = 0;
  for (let t = 0; t < TRACKS; t++) {
    const row: number[] = [];
    for (let s = 0; s < STEPS; s++) {
      const byte = Number.parseInt(hex.slice(i, i + 2), 16);
      if (Number.isNaN(byte)) return null;
      row.push(byte);
      i += 2;
    }
    tracks.push(row);
  }
  return tracks;
}

export function emptyTracks(): number[][] {
  return Array.from({ length: TRACKS }, () => Array(STEPS).fill(0));
}
