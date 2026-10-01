import { afterEach, describe, expect, it, vi } from "vitest";

import {
  BeatboxLink,
  BluetoothConnectionError,
  describeBluetoothConnectionError,
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
          target: { value: new DataView(packet.buffer as ArrayBuffer) },
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
