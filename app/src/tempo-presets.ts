export const TEMPO_PRESETS = [
  { label: "慢速练习", bpm: 90 },
  { label: "原版", bpm: 120 },
  { label: "快速节奏", bpm: 140 },
] as const;

export function isTempoPreset(bpm: number): boolean {
  return TEMPO_PRESETS.some((preset) => preset.bpm === bpm);
}
