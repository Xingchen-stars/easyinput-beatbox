import { afterEach, describe, expect, it, vi } from "vitest";

import {
  BeatboxLink,
  BluetoothConnectionError,
  describeBluetoothConnectionError,
  readBluetoothDiagnostics,
} from "./link";
import { BleFrameDecoder, encodeBleLine } from "./ble-framing";

type LinkWithPrivateOpen = {
  openPort(port: SerialPort): Promise<void>;
  openBleDevice(device: BluetoothDevice): Promise<void>;
  tryExistingConnections(): Promise<void>;
  preferredTransport: "ble" | "serial";
  disconnect(): Promise<void>;
};

afterEach(() => {
  vi.useRealTimers();
  vi.unstubAllGlobals();
});

function makeWindowTimerStub() {
  const values = new Map<string, string>();
  vi.stubGlobal("window", {
    setInterval: globalThis.setInterval.bind(globalThis),
    clearInterval: globalThis.clearInterval.bind(globalThis),
    setTimeout: globalThis.setTimeout.bind(globalThis),
    clearTimeout: globalThis.clearTimeout.bind(globalThis),
    location: { search: "", href: "http://127.0.0.1/" },
    localStorage: {
      getItem: (key: string) => values.get(key) ?? null,
      setItem: (key: string, value: string) => values.set(key, value),
      removeItem: (key: string) => values.delete(key),
    },
  });
  return values;
}

/** Model Windows' one-pending-GATT-operation rule, including early notifications. */
function makeStrictBleDevice(options: { subscribeDelay?: number; authAckDelay?: number; pairing?: boolean } = {}) {
  let listener: ((event: Event) => void) | undefined;
  let pending = false;
  let overlaps = 0;
  const operations: string[] = [];
  const requests: string[] = [];
  const decoder = new BleFrameDecoder();
  const delay = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms));
  const operation = async <T>(name: string, body: () => T, settleDelay = 2): Promise<T> => {
    if (pending) {
      overlaps += 1;
      throw Object.assign(new Error("GATT operation already in progress."), { name: "NetworkError" });
    }
    pending = true;
    operations.push(`${name}:start`);
    try {
      const value = body();
      await delay(settleDelay);
      return value;
    } finally {
      operations.push(`${name}:settled`);
      pending = false;
    }
  };
  const notify = (message: object) => {
    for (const packet of encodeBleLine(JSON.stringify(message))) {
      queueMicrotask(() => {
        tx.value = new DataView(packet.buffer as ArrayBuffer);
        listener?.({ target: tx } as unknown as Event);
      });
    }
  };
  const write = (value: ArrayBuffer) => {
    return operation("write", () => {
      const line = decoder.push(new Uint8Array(value));
      if (!line) return;
      requests.push(line);
      const message = JSON.parse(line);
      if (message.t === "ble_enroll" || message.t === "ble_auth") {
        // The peripheral's notification precedes the browser write acknowledgement.
        notify({ t: "ble_auth_ok", mode: message.t === "ble_enroll" ? "enrolled" : "known", v: 1 });
      } else if (message.t === "ping") {
        notify({ t: "hello", v: 3, name: "EasyInput Beatbox" });
        notify({ t: "state", bpm: 120, run: 0, var: 0, mode: 0 });
      }
    }, options.authAckDelay ?? 2);
  };
  const rx = {
    writeValueWithResponse: vi.fn(write),
    writeValueWithoutResponse: vi.fn(write),
  } as unknown as BluetoothRemoteGATTCharacteristic;
  const tx = {
    value: undefined as DataView | undefined,
    addEventListener: vi.fn((_type: string, fn: EventListener) => { listener = fn; }),
    removeEventListener: vi.fn(() => { listener = undefined; }),
    startNotifications: vi.fn(() => operation("subscribe", () => {
      const challenge = () => notify({ t: "ble_auth_challenge", nonce: "00112233445566778899aabbccddeeff", pairing: options.pairing === false ? 0 : 1, v: 1 });
      if (options.subscribeDelay != null) challenge();
      else setTimeout(challenge, 10);
      return tx;
    }, options.subscribeDelay ?? 2)),
    stopNotifications: vi.fn(() => operation("unsubscribe", () => tx)),
  };
  const service = {
    getCharacteristic: vi.fn((uuid: string) => operation("characteristic", () => uuid.endsWith("102") ? rx : tx)),
  } as unknown as BluetoothRemoteGATTService;
  const gatt = {
    connected: true,
    connect: vi.fn(() => operation("connect", () => gatt)),
    getPrimaryService: vi.fn(() => operation("service", () => service)),
    disconnect: vi.fn(() => { gatt.connected = false; }),
  };
  const device = {
    id: "strict-ble-device", name: "EasyInput Beatbox", gatt,
    addEventListener: vi.fn(), removeEventListener: vi.fn(),
  } as unknown as BluetoothDevice;
  return { device, requests, operations, notify, rx, tx, get overlaps() { return overlaps; } };
}

describe("BeatboxLink Windows GATT timing regressions", () => {
  it("waits for notification subscription to settle before answering an early challenge", async () => {
    makeWindowTimerStub();
    const fixture = makeStrictBleDevice({ subscribeDelay: 120 });
    const link = new BeatboxLink() as unknown as LinkWithPrivateOpen;
    try {
      await link.openBleDevice(fixture.device);
      expect(fixture.overlaps).toBe(0);
      expect(fixture.requests.map((line) => JSON.parse(line).t)).toEqual(["ble_enroll", "ping"]);
    } finally { await link.disconnect(); }
  });

  it("waits for the final auth write acknowledgement before the protocol ping", async () => {
    makeWindowTimerStub();
    const fixture = makeStrictBleDevice({ authAckDelay: 160 });
    const link = new BeatboxLink() as unknown as LinkWithPrivateOpen;
    try {
      await link.openBleDevice(fixture.device);
      expect(fixture.overlaps).toBe(0);
      expect(fixture.requests.map((line) => JSON.parse(line).t)).toEqual(["ble_enroll", "ping"]);
    } finally { await link.disconnect(); }
  });

  it("keeps concurrent commands and multi-packet messages whole and ordered", async () => {
    makeWindowTimerStub();
    const fixture = makeStrictBleDevice();
    const link = new BeatboxLink() as unknown as LinkWithPrivateOpen & {
      writeBleLineRaw(line: string): Promise<void>;
    };
    try {
      await link.openBleDevice(fixture.device);
      const large = JSON.stringify({ t: "pattern_set", p: "a".repeat(430) });
      const short = '{"t":"bpm","v":90}';
      await Promise.all([link.writeBleLineRaw(large), link.writeBleLineRaw(short)]);
      expect(fixture.overlaps).toBe(0);
      expect(fixture.requests.slice(-2)).toEqual([large, short]);
    } finally { await link.disconnect(); }
  });

  it("reuses a saved browser key outside the S7 window without re-enrollment", async () => {
    const storage = makeWindowTimerStub();
    const id = "1122334455667788";
    const key = "ab".repeat(32);
    const storageKey = "easyinput-beatbox-ble-v1:strict-ble-device";
    storage.set(storageKey, JSON.stringify({ id, key }));
    const fixture = makeStrictBleDevice({ pairing: false, subscribeDelay: 120, authAckDelay: 160 });
    const link = new BeatboxLink() as unknown as LinkWithPrivateOpen;
    try {
      await link.openBleDevice(fixture.device);
      const messages = fixture.requests.map((line) => JSON.parse(line));
      expect(messages.map((message) => message.t)).toEqual(["ble_auth", "ping"]);
      expect(messages[0]).toMatchObject({ id, proof: expect.stringMatching(/^[a-f0-9]{64}$/) });
      expect(storage.get(storageKey)).toBe(JSON.stringify({ id, key }));
      const diagnostic = JSON.stringify(readBluetoothDiagnostics());
      expect(diagnostic).not.toContain(key);
      expect(diagnostic).not.toContain(id);
      expect(diagnostic).not.toContain("00112233445566778899aabbccddeeff");
      expect(fixture.overlaps).toBe(0);
    } finally { await link.disconnect(); }
  });

  it("still refuses a new browser when the physical S7 window is closed", async () => {
    const storage = makeWindowTimerStub();
    const fixture = makeStrictBleDevice({ pairing: false });
    const link = new BeatboxLink() as unknown as LinkWithPrivateOpen;
    await expect(link.openBleDevice(fixture.device)).rejects.toThrow("这台浏览器尚未登记");
    expect(storage.size).toBe(0);
    expect(fixture.requests).toHaveLength(0);
  });

  it("does not treat diagnostics as a protocol hello or accept a removed notification listener", async () => {
    makeWindowTimerStub();
    const fixture = makeStrictBleDevice();
    const link = new BeatboxLink() as unknown as LinkWithPrivateOpen & {
      bleProtocolSignalSeen: boolean;
      handleBleNotification(event: Event): void;
    };
    await link.openBleDevice(fixture.device);
    link.bleProtocolSignalSeen = false;
    fixture.notify({ t: "ble_diag", event: "connect" });
    await Promise.resolve();
    expect(link.bleProtocolSignalSeen).toBe(false);
    await link.disconnect();
    for (const packet of encodeBleLine('{"t":"hello","v":3}')) {
      fixture.tx.value = new DataView(packet.buffer as ArrayBuffer);
      link.handleBleNotification({ target: fixture.tx } as unknown as Event);
    }
    expect(link.bleProtocolSignalSeen).toBe(false);
  });

  it("cancels the rest of a fragmented message and queued commands on disconnect", async () => {
    makeWindowTimerStub();
    const fixture = makeStrictBleDevice();
    const link = new BeatboxLink() as unknown as LinkWithPrivateOpen & {
      writeBleLineRaw(line: string): Promise<void>;
    };
    await link.openBleDevice(fixture.device);
    const baseline = vi.mocked(fixture.rx.writeValueWithoutResponse).mock.calls.length;
    const large = link.writeBleLineRaw(JSON.stringify({ t: "pattern_set", p: "b".repeat(430) }));
    const queued = link.writeBleLineRaw('{"t":"bpm","v":90}');
    const results = Promise.allSettled([large, queued]);
    await Promise.resolve();
    await link.disconnect();
    expect((await results).map((result) => result.status)).toEqual(["rejected", "rejected"]);
    expect(vi.mocked(fixture.rx.writeValueWithoutResponse).mock.calls.length).toBe(baseline + 1);
    expect(fixture.requests.map(line => JSON.parse(line).t)).toEqual(["ble_enroll", "ping"]);
  });
});

describe("BeatboxLink serial opening", () => {
  it("shares an in-progress Web Serial open between concurrent callers", async () => {
    let markStarted!: () => void;
    let rejectOpen!: (reason: Error) => void;
    const started = new Promise<void>((resolve) => {
      markStarted = resolve;
    });
    const pendingOpen = new Promise<void>((_resolve, reject) => {
      rejectOpen = reject;
    });
    const open = vi.fn(() => {
      markStarted();
      return pendingOpen;
    });
    const port = {
      open,
      close: vi.fn(async () => undefined),
      readable: null,
      writable: null,
    } as unknown as SerialPort;
    const link = new BeatboxLink() as unknown as LinkWithPrivateOpen;

    const automaticAttempt = link.openPort(port);
    await started;
    const manualAttempt = link.openPort(port);

    expect(open).toHaveBeenCalledTimes(1);

    const failure = new Error("simulated open failure");
    rejectOpen(failure);
    await expect(automaticAttempt).rejects.toThrow("simulated open failure");
    await expect(manualAttempt).rejects.toThrow("simulated open failure");
    expect(open).toHaveBeenCalledTimes(1);
  });

  it("asserts DTR and keeps RTS low after opening the ESP32-S3 port", async () => {
    makeWindowTimerStub();
    const setSignals = vi.fn(async () => undefined);
    const port = {
      open: vi.fn(async () => undefined),
      setSignals,
      close: vi.fn(async () => undefined),
      readable: null,
      writable: null,
    } as unknown as SerialPort;
    const link = new BeatboxLink() as unknown as LinkWithPrivateOpen;

    await link.openPort(port);

    expect(setSignals).toHaveBeenCalledWith({
      dataTerminalReady: true,
      requestToSend: false,
    });
    await link.disconnect();
  });

  it("shows disconnected immediately while a browser serial read is being cancelled", async () => {
    makeWindowTimerStub();
    let finishCancel!: () => void;
    const cancelPending = new Promise<void>((resolve) => {
      finishCancel = resolve;
    });
    const reader = {
      read: vi.fn(() => new Promise<never>(() => undefined)),
      cancel: vi.fn(() => cancelPending),
      releaseLock: vi.fn(),
    };
    const port = {
      open: vi.fn(async () => undefined),
      setSignals: vi.fn(async () => undefined),
      close: vi.fn(async () => undefined),
      readable: { getReader: () => reader },
      writable: null,
    } as unknown as SerialPort;
    const link = new BeatboxLink() as unknown as LinkWithPrivateOpen & {
      getState(): { connected: boolean; deviceName: string };
    };

    await link.openPort(port);
    const disconnecting = link.disconnect();

    expect(link.getState().connected).toBe(false);
    expect(link.getState().deviceName).toBe("未连接");

    finishCancel();
    await disconnecting;
    expect(port.close).toHaveBeenCalledTimes(1);
  });
});

describe("BeatboxLink transport routing", () => {
  it("shares a slow authorized-device discovery between reconnect callers", async () => {
    makeWindowTimerStub();
    let release!: (devices: BluetoothDevice[]) => void;
    const getDevices = vi.fn(() => new Promise<BluetoothDevice[]>((resolve) => { release = resolve; }));
    vi.stubGlobal("navigator", { bluetooth: { getDevices } });
    const link = new BeatboxLink() as unknown as LinkWithPrivateOpen;
    const first = link.tryExistingConnections();
    const second = link.tryExistingConnections();
    expect(getDevices).toHaveBeenCalledTimes(1);
    release([]);
    await Promise.all([first, second]);
  });
  it("does not auto-open an authorized USB port while BLE is preferred", async () => {
    makeWindowTimerStub();
    const getPorts = vi.fn(async () => [] as SerialPort[]);
    vi.stubGlobal("navigator", {
      bluetooth: { getDevices: vi.fn(async () => []) },
      serial: { getPorts },
    });
    const link = new BeatboxLink() as unknown as LinkWithPrivateOpen;

    await link.tryExistingConnections();

    expect(getPorts).not.toHaveBeenCalled();
  });

  it("auto-opens USB only after USB has been explicitly selected", async () => {
    makeWindowTimerStub();
    const port = {
      open: vi.fn(async () => undefined),
      setSignals: vi.fn(async () => undefined),
      close: vi.fn(async () => undefined),
      getInfo: () => ({ usbVendorId: 0x303a }),
      readable: null,
      writable: null,
    } as unknown as SerialPort;
    const getPorts = vi.fn(async () => [port]);
    vi.stubGlobal("navigator", {
      bluetooth: { getDevices: vi.fn(async () => []) },
      serial: { getPorts },
    });
    const link = new BeatboxLink() as unknown as LinkWithPrivateOpen;
    link.preferredTransport = "serial";

    await link.tryExistingConnections();

    expect(getPorts).toHaveBeenCalledTimes(1);
    expect(port.open).toHaveBeenCalledTimes(1);
    await link.disconnect();
  });
});

describe("BeatboxLink Bluetooth diagnostics", () => {
  it("reports the exact stage and browser error for service discovery failures", async () => {
    makeWindowTimerStub();
    const browserFailure = Object.assign(new Error("GATT operation failed"), {
      name: "NetworkError",
    });
    const server = {
      connected: true,
      connect: vi.fn(),
      disconnect: vi.fn(),
      getPrimaryService: vi.fn(async () => { throw browserFailure; }),
    } as unknown as BluetoothRemoteGATTServer;
    const device = {
      id: "service-failure-device",
      name: "EasyInput Beatbox",
      gatt: {
        connected: false,
        connect: vi.fn(async () => server),
        disconnect: vi.fn(),
        getPrimaryService: vi.fn(),
      },
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
    } as unknown as BluetoothDevice;
    const link = new BeatboxLink() as unknown as LinkWithPrivateOpen;

    let captured: unknown;
    try {
      await link.openBleDevice(device);
    } catch (error) {
      captured = error;
    }

    expect(captured).toBeInstanceOf(BluetoothConnectionError);
    expect(captured).toMatchObject({
      stage: "service-discovery",
      browserErrorName: "NetworkError",
      browserErrorMessage: "GATT operation failed",
    });
    expect(describeBluetoothConnectionError(captured)).toBe(
      "蓝牙连接失败｜阶段：Beatbox 服务发现｜浏览器：NetworkError: GATT operation failed",
    );
  });

  it("enrolls a browser during the S7 window before sending the protocol ping", async () => {
    const storedCredentials = makeWindowTimerStub();
    let notificationListener: ((event: Event) => void) | undefined;
    const responseWrites: Uint8Array[] = [];
    const commandWrites: Uint8Array[] = [];
    const requestDecoder = new BleFrameDecoder();
    const completeRequests: string[] = [];
    const notifyLine = (line: string) => {
      for (const packet of encodeBleLine(line)) {
        queueMicrotask(() => notificationListener!({
          target: Object.assign(tx, { value: new DataView(packet.buffer as ArrayBuffer) }),
        } as unknown as Event));
      }
    };
    const rx = {
      writeValueWithoutResponse: vi.fn(async (value: ArrayBuffer) => {
        commandWrites.push(new Uint8Array(value));
      }),
      writeValueWithResponse: vi.fn(async (value: ArrayBuffer) => {
        const packet = new Uint8Array(value);
        responseWrites.push(packet);
        const line = requestDecoder.push(packet);
        if (!line) return;
        completeRequests.push(line);
        const message = JSON.parse(line) as { t?: string };
        if (message.t === "ble_enroll") {
          notifyLine('{"t":"ble_auth_ok","mode":"enrolled","v":1}');
        } else if (message.t === "ping") {
          notifyLine('{"t":"hello","v":3}');
        }
      }),
    } as unknown as BluetoothRemoteGATTCharacteristic;
    const tx = {
      addEventListener: vi.fn((_type: string, listener: EventListenerOrEventListenerObject) => {
        notificationListener = listener as (event: Event) => void;
      }),
      removeEventListener: vi.fn(),
      startNotifications: vi.fn(async () => {
        notifyLine('{"t":"ble_auth_challenge","nonce":"00112233445566778899aabbccddeeff","pairing":1,"v":1}');
        return undefined;
      }),
      stopNotifications: vi.fn(async () => undefined),
    } as unknown as BluetoothRemoteGATTCharacteristic;
    const service = {
      getCharacteristic: vi.fn()
        .mockResolvedValueOnce(rx)
        .mockResolvedValueOnce(tx),
    } as unknown as BluetoothRemoteGATTService;
    const server = {
      connected: true,
      getPrimaryService: vi.fn(async () => service),
    } as unknown as BluetoothRemoteGATTServer;
    const gatt = {
      connected: true,
      connect: vi.fn(async () => server),
      disconnect: vi.fn(),
    } as unknown as BluetoothRemoteGATTServer;
    const device = {
      id: "beatbox-browser-enrollment",
      name: "EasyInput Beatbox",
      gatt,
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
    } as unknown as BluetoothDevice;
    const link = new BeatboxLink() as unknown as LinkWithPrivateOpen;

    const opening = link.openBleDevice(device);
    await vi.waitFor(() => expect(notificationListener).toBeDefined());
    await opening;

    expect(responseWrites.length).toBeGreaterThanOrEqual(2);
    expect(completeRequests.map((line) => JSON.parse(line).t)).toEqual([
      "ble_enroll",
      "ping",
    ]);
    expect(commandWrites).toHaveLength(0);
    expect(storedCredentials.size).toBe(1);
    await link.disconnect();
  });
});
