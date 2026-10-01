import { describe, expect, it } from "vitest";

import { BleFrameDecoder, encodeBleLine } from "./ble-framing";

describe("Beatbox BLE framing", () => {
  it("round-trips a multi-byte JSON line over multiple packets", () => {
    const line = JSON.stringify({ t: "error", msg: "蓝牙连接测试" });
    const packets = encodeBleLine(line, 7);
    const decoder = new BleFrameDecoder();
    let decoded: string | null = null;
    for (const packet of packets) decoded = decoder.push(packet) ?? decoded;
    expect(packets.length).toBeGreaterThan(1);
    expect(decoded).toBe(line);
  });

  it("discards a broken packet sequence", () => {
    const packets = encodeBleLine('{"t":"ping","padding":"123456789"}', 8);
    const decoder = new BleFrameDecoder();
    expect(decoder.push(packets[0])).toBeNull();
    expect(decoder.push(packets[2])).toBeNull();
  });
});
