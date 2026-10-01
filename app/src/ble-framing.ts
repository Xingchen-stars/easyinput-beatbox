export const BLE_FRAGMENT_START = 0x01;
export const BLE_FRAGMENT_END = 0x02;
export const BLE_FRAGMENT_HEADER = 2;
export const BLE_DEFAULT_PAYLOAD = 100;

const encoder = new TextEncoder();
const decoder = new TextDecoder();

/** Split one host-protocol JSON line into ordered BLE packets. */
export function encodeBleLine(line: string, payloadLimit = BLE_DEFAULT_PAYLOAD): Uint8Array[] {
  const bytes = encoder.encode(line);
  if (bytes.length === 0) return [];
  if (payloadLimit < 1 || payloadLimit > 120) {
    throw new RangeError("BLE payload limit must be between 1 and 120 bytes.");
  }

  const packets: Uint8Array[] = [];
  let sequence = 0;
  for (let offset = 0; offset < bytes.length; offset += payloadLimit) {
    const payload = bytes.subarray(offset, Math.min(offset + payloadLimit, bytes.length));
    const packet = new Uint8Array(BLE_FRAGMENT_HEADER + payload.length);
    packet[0] =
      (offset === 0 ? BLE_FRAGMENT_START : 0) |
      (offset + payload.length === bytes.length ? BLE_FRAGMENT_END : 0);
    packet[1] = sequence++ & 0xff;
    packet.set(payload, BLE_FRAGMENT_HEADER);
    packets.push(packet);
  }
  return packets;
}

/** Reassemble notifications from the board; malformed sequences are discarded. */
export class BleFrameDecoder {
  private chunks: Uint8Array[] = [];
  private length = 0;
  private expectedSequence = 0;
  private active = false;

  reset() {
    this.chunks = [];
    this.length = 0;
    this.expectedSequence = 0;
    this.active = false;
  }

  push(value: DataView | Uint8Array): string | null {
    const bytes =
      value instanceof DataView
        ? new Uint8Array(value.buffer, value.byteOffset, value.byteLength)
        : value;
    if (bytes.length < BLE_FRAGMENT_HEADER) {
      this.reset();
      return null;
    }

    const flags = bytes[0];
    const sequence = bytes[1];
    if ((flags & BLE_FRAGMENT_START) !== 0) {
      this.reset();
      this.active = true;
      this.expectedSequence = sequence;
    }
    if (!this.active || sequence !== this.expectedSequence) {
      this.reset();
      return null;
    }
    this.expectedSequence = (this.expectedSequence + 1) & 0xff;

    const payload = bytes.slice(BLE_FRAGMENT_HEADER);
    if (payload.length === 0 || this.length + payload.length >= 768) {
      this.reset();
      return null;
    }
    this.chunks.push(payload);
    this.length += payload.length;

    if ((flags & BLE_FRAGMENT_END) === 0) return null;
    const joined = new Uint8Array(this.length);
    let offset = 0;
    for (const chunk of this.chunks) {
      joined.set(chunk, offset);
      offset += chunk.length;
    }
    const line = decoder.decode(joined);
    this.reset();
    return line;
  }
}
