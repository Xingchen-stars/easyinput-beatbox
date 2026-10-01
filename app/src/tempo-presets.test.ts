import { describe, expect, it } from "vitest";
import { isTempoPreset, TEMPO_PRESETS } from "./tempo-presets";

describe("tempo presets", () => {
  it("exposes the three assignment presets in display order", () => {
    expect(TEMPO_PRESETS).toEqual([
      { label: "慢速练习", bpm: 90 },
      { label: "原版", bpm: 120 },
      { label: "快速节奏", bpm: 140 },
    ]);
  });

  it("recognizes only the configured preset values", () => {
    expect(isTempoPreset(90)).toBe(true);
    expect(isTempoPreset(120)).toBe(true);
    expect(isTempoPreset(140)).toBe(true);
    expect(isTempoPreset(100)).toBe(false);
  });
});
