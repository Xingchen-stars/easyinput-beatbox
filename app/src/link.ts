import { BleFrameDecoder, encodeBleLine } from "./ble-framing";
import { BleOperationQueue } from "./ble-operation-queue";
import {
  applyLocalBpmDraft,
  applyLocalPattern,
  applyLocalVolumeDraft,
  clearBpmDraft,
  clearVolumeDraft,
  createInitialState,
  markConnecting,
  markDisconnected,
  reduceHostLine,
  type DeviceLink,
  type DeviceState,
} from "./device-store";
import { bankHex, type PatternBanks } from "./pattern";
import { clampBpm, clampSwing, clampVolume, parseHostLine } from "./protocol";

export type { DeviceState as BeatboxState };

type Listener = (state: DeviceState) => void;

const ESPRESSIF_VID = 0x303a;
const BLE_AUTH_TIMEOUT_MS = 12_000;
const BLE_READY_POLL_MS = 50;
const BLE_CREDENTIAL_PREFIX = "easyinput-beatbox-ble-v1:";
export const BEATBOX_BLE_SERVICE = "8ab6c845-23e4-4f36-91df-8ac820b58101";
export const BEATBOX_BLE_RX = "8ab6c845-23e4-4f36-91df-8ac820b58102";
export const BEATBOX_BLE_TX = "8ab6c845-23e4-4f36-91df-8ac820b58103";

export type BluetoothConnectionStage =
  | "gatt-connect"
  | "service-discovery"
  | "rx-characteristic"
  | "tx-characteristic"
  | "notification-subscribe"
  | "device-authorization"
  | "protocol-ping";

const BLUETOOTH_STAGE_LABELS: Record<BluetoothConnectionStage, string> = {
  "gatt-connect": "GATT 连接",
  "service-discovery": "Beatbox 服务发现",
  "rx-characteristic": "写入特征发现",
  "tx-characteristic": "通知特征发现",
  "notification-subscribe": "通知订阅",
  "device-authorization": "开发板授权",
  "protocol-ping": "协议握手",
};

type BleCredential = { id: string; key: string };

function bytesToHex(bytes: Uint8Array): string {
  return Array.from(bytes, (value) => value.toString(16).padStart(2, "0")).join("");
}

function hexToBytes(hex: string): Uint8Array<ArrayBuffer> {
  if (hex.length % 2 !== 0 || !/^[0-9a-f]+$/i.test(hex)) {
    throw new Error("设备授权数据格式无效。");
  }
  const bytes = new Uint8Array(hex.length / 2);
  for (let i = 0; i < bytes.length; i += 1) {
    bytes[i] = Number.parseInt(hex.slice(i * 2, i * 2 + 2), 16);
  }
  return bytes;
}

function makeCredential(): BleCredential {
  const id = new Uint8Array(8);
  const key = new Uint8Array(32);
  globalThis.crypto.getRandomValues(id);
  globalThis.crypto.getRandomValues(key);
  return { id: bytesToHex(id), key: bytesToHex(key) };
}

function credentialStorageKey(deviceId: string): string {
  return `${BLE_CREDENTIAL_PREFIX}${deviceId}`;
}

function loadCredential(deviceId: string): BleCredential | null {
  try {
    const raw = window.localStorage.getItem(credentialStorageKey(deviceId));
    if (!raw) return null;
    const value = JSON.parse(raw) as Partial<BleCredential>;
    if (
      typeof value.id !== "string" || !/^[0-9a-f]{16}$/i.test(value.id) ||
      typeof value.key !== "string" || !/^[0-9a-f]{64}$/i.test(value.key)
    ) return null;
    return { id: value.id.toLowerCase(), key: value.key.toLowerCase() };
  } catch {
    return null;
  }
}

function storeCredential(deviceId: string, credential: BleCredential) {
  window.localStorage.setItem(credentialStorageKey(deviceId), JSON.stringify(credential));
}

async function hmacProof(keyHex: string, nonceHex: string): Promise<string> {
  const key = await globalThis.crypto.subtle.importKey(
    "raw",
    hexToBytes(keyHex),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"],
  );
  const signature = await globalThis.crypto.subtle.sign("HMAC", key, hexToBytes(nonceHex));
  return bytesToHex(new Uint8Array(signature));
}

function browserErrorDetails(cause: unknown): { name: string; message: string } {
  if (cause && typeof cause === "object") {
    const candidate = cause as { name?: unknown; message?: unknown };
    return {
      name: typeof candidate.name === "string" ? candidate.name : "Error",
      message: typeof candidate.message === "string" ? candidate.message : String(cause),
    };
  }
  return { name: "Error", message: String(cause) };
}

function emitBluetoothDiagnostic(level: "info" | "error", message: string) {
  const entry = { at: new Date().toISOString(), level, message };
  bluetoothDiagnostics.push(entry);
  if (bluetoothDiagnostics.length > 256) bluetoothDiagnostics.shift();
  const rendered = `[Beatbox BLE] ${message}`;
  if (level === "error") console.error(rendered);
  else console.info(rendered);

  try {
    if (typeof window === "undefined") return;
    // Safe, bounded, in-memory evidence is also available on GitHub Pages.
    // No browser IDs, keys, challenge values or complete command payloads are logged.
    Object.defineProperty(window, "__beatboxBleDiagnostics", {
      configurable: true,
      get: readBluetoothDiagnostics,
    });
    if (new URLSearchParams(window.location.search).get("ble-diag") !== "1") return;
    if (!["127.0.0.1", "localhost", "[::1]"].includes(window.location.hostname)) return;
    void fetch("/__beatbox_ble_log", {
      method: "POST",
      cache: "no-store",
      body: JSON.stringify({
        at: entry.at,
        level,
        message,
        page: window.location.href,
      }),
    }).catch(() => {
      /* The optional local listener may be stopped; diagnostics must not affect BLE. */
    });
  } catch {
    /* Diagnostics are deliberately best-effort and never change connection behavior. */
  }
}

type BluetoothDiagnostic = { at: string; level: "info" | "error"; message: string };
const bluetoothDiagnostics: BluetoothDiagnostic[] = [];
export function readBluetoothDiagnostics(): BluetoothDiagnostic[] {
  return bluetoothDiagnostics.map((entry) => ({ ...entry }));
}

function parseCommandType(line: string): string {
  try {
    const { t } = JSON.parse(line) as { t?: unknown };
    return typeof t === "string" && /^[a-z_]{1,32}$/.test(t) ? t : "unknown";
  } catch { return "unknown"; }
}

export class BluetoothConnectionError extends Error {
  readonly stage: BluetoothConnectionStage;
  readonly browserErrorName: string;
  readonly browserErrorMessage: string;

  constructor(stage: BluetoothConnectionStage, cause: unknown) {
    const details = browserErrorDetails(cause);
    super(`${BLUETOOTH_STAGE_LABELS[stage]}：${details.name}: ${details.message}`, { cause });
    this.name = "BluetoothConnectionError";
    this.stage = stage;
    this.browserErrorName = details.name;
    this.browserErrorMessage = details.message;
  }
}

export function describeBluetoothConnectionError(error: unknown): string {
  if (error instanceof BluetoothConnectionError) {
    return `蓝牙连接失败｜阶段：${BLUETOOTH_STAGE_LABELS[error.stage]}｜浏览器：${error.browserErrorName}: ${error.browserErrorMessage}`;
  }
  if (error instanceof Error) return error.message;
  return String(error);
}

export class BeatboxLink {
  private listeners = new Set<Listener>();
  private state: DeviceState = createInitialState();
  private transport: Exclude<DeviceLink, "none" | "midi"> | null = null;

  private bleDevice: BluetoothDevice | null = null;
  private bleServer: BluetoothRemoteGATTServer | null = null;
  private bleRx: BluetoothRemoteGATTCharacteristic | null = null;
  private bleTx: BluetoothRemoteGATTCharacteristic | null = null;
  private bleDecoder = new BleFrameDecoder();
  private bleOperations: BleOperationQueue | null = null;
  private bleAuthorized = false;
  private bleAuthorizationFailure: Error | null = null;
  private bleAuthorizationBusy = false;
  private blePairingOpen = false;
  private pendingEnrollment: BleCredential | null = null;
  private bleProtocolSignalSeen = false;
  private lastProtocolStateDiagnostic = "";

  private port: SerialPort | null = null;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  private readLoopActive = false;
  private rxText = "";
  private encoder = new TextEncoder();
  private decoder = new TextDecoder();

  private reconnectTimer: number | null = null;
  private userDisconnected = false;
  /** BLE is the daily link. USB reconnect is enabled only after an explicit USB click. */
  private preferredTransport: "ble" | "serial" = "ble";
  private closingPromise: Promise<void> | null = null;
  private reconnectPromise: Promise<void> | null = null;
  private openingPromise: Promise<void> | null = null;
  private requestPromise: Promise<void> | null = null;
  private signalRecoveryPromise: Promise<void> | null = null;
  private staleTimer: number | null = null;
  private commitTimer: number | null = null;
  private pendingCommitBank: 0 | 1 | 2 | null = null;

  getState() {
    return this.state;
  }

  getTransport() {
    return this.transport;
  }

  subscribe(fn: Listener) {
    this.listeners.add(fn);
    fn(this.state);
    return () => this.listeners.delete(fn);
  }

  private setState(next: DeviceState) {
    this.state = next;
    for (const fn of this.listeners) fn(this.state);
  }

  private patch(reducer: (s: DeviceState) => DeviceState) {
    this.setState(reducer(this.state));
  }

  async connect() {
    emitBluetoothDiagnostic(
      "info",
      `page:init bluetooth=${"bluetooth" in navigator} serial=${"serial" in navigator}`,
    );
    if (!("bluetooth" in navigator) && !("serial" in navigator)) {
      throw new Error("当前浏览器不支持蓝牙或 USB 连接。请使用最新版 Chrome / Edge。");
    }
    this.scheduleAutoReconnect();
    await this.tryExistingConnections();
  }

  /** Must be called from a click: the browser shows its own Bluetooth picker. */
  async requestBluetoothDevice() {
    if (this.requestPromise) return this.requestPromise;
    const request = this.requestBluetoothDeviceOnce();
    this.requestPromise = request;
    try {
      await request;
    } finally {
      if (this.requestPromise === request) this.requestPromise = null;
    }
  }

  private async requestBluetoothDeviceOnce() {
    if (!("bluetooth" in navigator)) {
      throw new Error("当前浏览器不支持 Web Bluetooth，请使用最新版 Chrome / Edge。");
    }
    this.preferredTransport = "ble";
    this.userDisconnected = false;
    emitBluetoothDiagnostic("info", "device-picker:start");
    let device: BluetoothDevice;
    try {
      device = await navigator.bluetooth.requestDevice({
        filters: [{ namePrefix: "EasyInput Beatbox" }],
        optionalServices: [BEATBOX_BLE_SERVICE],
      });
    } catch (error) {
      const details = browserErrorDetails(error);
      emitBluetoothDiagnostic("error", `device-picker:error ${details.name}: ${details.message}`);
      throw error;
    }
    emitBluetoothDiagnostic("info", `device-picker:selected ${device.name ?? "unnamed"}`);
    await this.openBleDevice(device);
  }

  async requestPort() {
    if (this.requestPromise) return this.requestPromise;
    const request = this.requestPortOnce();
    this.requestPromise = request;
    try {
      await request;
    } finally {
      if (this.requestPromise === request) this.requestPromise = null;
    }
  }

  private async requestPortOnce() {
    if (!("serial" in navigator)) throw new Error("当前浏览器不支持 Web Serial。");
    this.preferredTransport = "serial";
    this.userDisconnected = false;
    const port = await navigator.serial.requestPort({
      filters: [{ usbVendorId: ESPRESSIF_VID }],
    });
    await this.openPort(port);
  }

  async disconnect() {
    this.userDisconnected = true;
    /* Reflect the user's action immediately, even if a browser USB driver
       takes a moment to cancel a pending read and release the COM port. */
    this.patch((s) => markDisconnected(s, "未连接"));
    await this.closeConnection();
  }

  private scheduleAutoReconnect() {
    if (this.reconnectTimer != null) return;
    this.reconnectTimer = window.setInterval(() => {
      if (!this.state.connected && !this.userDisconnected) void this.tryExistingConnections();
    }, 1500);
  }

  private async tryExistingConnections() {
    if (this.reconnectPromise) return this.reconnectPromise;
    const reconnecting = this.tryExistingConnectionsOnce();
    this.reconnectPromise = reconnecting;
    try { await reconnecting; }
    finally {
      if (this.reconnectPromise === reconnecting) this.reconnectPromise = null;
    }
  }

  private async tryExistingConnectionsOnce() {
    if (
      this.userDisconnected ||
      this.state.connected ||
      this.readLoopActive ||
      this.openingPromise != null ||
      this.requestPromise != null ||
      this.closingPromise != null
    ) return;

    if (
      this.preferredTransport === "ble" &&
      "bluetooth" in navigator &&
      typeof navigator.bluetooth.getDevices === "function"
    ) {
      try {
        const devices = await navigator.bluetooth.getDevices();
        if (this.userDisconnected || this.requestPromise != null) return;
        emitBluetoothDiagnostic("info", `auto-reconnect:authorized-devices=${devices.length}`);
        for (const device of devices) {
          if (!device.name?.startsWith("EasyInput Beatbox")) continue;
          emitBluetoothDiagnostic("info", `auto-reconnect:trying ${device.name}`);
          try {
            await this.openBleDevice(device);
            return;
          } catch (error) {
            const details = browserErrorDetails(error);
            emitBluetoothDiagnostic(
              "error",
              `auto-reconnect:error ${details.name}: ${details.message}`,
            );
            /* try the next previously-authorized device */
          }
        }
      } catch (error) {
        const details = browserErrorDetails(error);
        emitBluetoothDiagnostic(
          "error",
          `auto-reconnect:get-devices-error ${details.name}: ${details.message}`,
        );
        /* browser may not expose persistent-device discovery */
      }
    }

    if (this.preferredTransport === "serial" && "serial" in navigator) {
      const ports = await navigator.serial.getPorts();
      if (this.userDisconnected || this.requestPromise != null) return;
      for (const port of ports) {
        const info = port.getInfo();
        if (info.usbVendorId != null && info.usbVendorId !== ESPRESSIF_VID) continue;
        try {
          await this.openPort(port);
          return;
        } catch {
          /* try next */
        }
      }
    }
    if (!this.state.connected) {
      const label = this.preferredTransport === "ble"
        ? "等待设备…点击「蓝牙连接」"
        : "USB 已断开，点击「USB备用」重连";
      this.patch((s) => markDisconnected(s, label));
    }
  }

  private async runBluetoothStage<T>(
    stage: BluetoothConnectionStage,
    operation: () => Promise<T>,
  ): Promise<T> {
    emitBluetoothDiagnostic("info", `${stage}:start`);
    try {
      const result = await operation();
      emitBluetoothDiagnostic("info", `${stage}:ok`);
      return result;
    } catch (cause) {
      if (cause instanceof BluetoothConnectionError) throw cause;
      const error = new BluetoothConnectionError(stage, cause);
      emitBluetoothDiagnostic(
        "error",
        `${stage}:error ${error.browserErrorName}: ${error.browserErrorMessage}`,
      );
      throw error;
    }
  }

  private async openBleDevice(device: BluetoothDevice) {
    if (this.bleDevice === device && this.state.connected) return;
    if (this.openingPromise) {
      await this.openingPromise;
      return;
    }
    const opening = this.openBleDeviceOnce(device);
    this.openingPromise = opening;
    try {
      await opening;
    } finally {
      if (this.openingPromise === opening) this.openingPromise = null;
    }
  }

  private async openBleDeviceOnce(device: BluetoothDevice) {
    await this.closeConnection();
    if (!device.gatt) {
      throw new BluetoothConnectionError(
        "gatt-connect",
        new Error("所选设备没有可用的蓝牙 GATT 服务。"),
      );
    }
    this.bleDevice = device;
    const operations = new BleOperationQueue((message) => emitBluetoothDiagnostic("info", message));
    this.bleOperations = operations;
    device.addEventListener("gattserverdisconnected", this.handleBleDisconnected);
    try {
      this.bleServer = await this.runBluetoothStage("gatt-connect", () =>
        operations.run("connect", () => device.gatt!.connect()));
      const service = await this.runBluetoothStage("service-discovery", () =>
        operations.run("service-discovery", () => this.bleServer!.getPrimaryService(BEATBOX_BLE_SERVICE)));
      this.bleRx = await this.runBluetoothStage("rx-characteristic", () =>
        operations.run("rx-characteristic", () => service.getCharacteristic(BEATBOX_BLE_RX)));
      this.bleTx = await this.runBluetoothStage("tx-characteristic", () =>
        operations.run("tx-characteristic", () => service.getCharacteristic(BEATBOX_BLE_TX)));
      this.bleDecoder.reset();
      this.bleAuthorized = false;
      this.bleAuthorizationFailure = null;
      this.bleAuthorizationBusy = false;
      this.blePairingOpen = false;
      this.pendingEnrollment = null;
      this.bleProtocolSignalSeen = false;
      this.bleTx.addEventListener("characteristicvaluechanged", this.handleBleNotification);
      await this.runBluetoothStage("notification-subscribe", () =>
        operations.run("notification-subscribe", () => this.bleTx!.startNotifications()));
      this.transport = "ble";
      this.patch((s) => markConnecting(s, "ble"));
      await this.runBluetoothStage("device-authorization", () =>
        this.waitForBleAuthorization());
      await this.runBluetoothStage("protocol-ping", async () => {
        await this.writeBleLineRaw('{"t":"ping"}', true);
        await this.waitForBleProtocolSignal();
      });
      emitBluetoothDiagnostic("info", "transport:ble-ready-awaiting-device-state");
    } catch (error) {
      await this.closeConnection();
      this.patch((s) => markDisconnected(s, "蓝牙连接失败"));
      throw error;
    }
    this.armStaleWatch();
  }

  private handleBleNotification = (event: Event) => {
    const characteristic = event.target as unknown as BluetoothRemoteGATTCharacteristic;
    if (characteristic !== this.bleTx || !this.bleOperations?.isActive) return;
    if (!characteristic.value) return;
    const line = this.bleDecoder.push(characteristic.value);
    if (!line) return;
    const trimmed = line.trim();
    if (trimmed.includes('"t":"ble_auth_')) {
      const operations = this.bleOperations;
      void this.handleBleAuthorizationLine(trimmed).catch((cause) => {
        if (this.bleOperations === operations && operations.isActive) {
          this.bleAuthorizationFailure = cause instanceof Error ? cause : new Error(String(cause));
        }
      });
      return;
    }
    const message = parseHostLine(trimmed);
    // A diagnostic notification is not proof that the actual protocol is ready.
    if (!this.bleAuthorized) return;
    if (message?.t === "hello" || message?.t === "state") this.bleProtocolSignalSeen = true;
    this.onLine(trimmed);
  };

  private async handleBleAuthorizationLine(line: string) {
    const operations = this.bleOperations;
    const isCurrent = () => operations != null && operations.isActive && this.bleOperations === operations;
    if (!isCurrent()) return;
    let message: {
      t?: string;
      nonce?: string;
      pairing?: number;
      mode?: string;
      code?: string;
    };
    try {
      message = JSON.parse(line) as typeof message;
    } catch {
      throw new Error("开发板返回了无效的授权消息。");
    }

    if (message.t === "ble_auth_challenge") {
      if (this.bleAuthorizationBusy) return;
      if (typeof message.nonce !== "string" || !/^[0-9a-f]{32}$/i.test(message.nonce)) {
        throw new Error("开发板授权挑战格式无效。");
      }
      this.blePairingOpen = message.pairing === 1;
      const deviceId = this.bleDevice?.id;
      if (!deviceId) throw new Error("浏览器没有提供稳定的蓝牙设备标识。");
      this.bleAuthorizationBusy = true;
      try {
        const credential = loadCredential(deviceId);
        if (credential) {
          const proof = await hmacProof(credential.key, message.nonce);
          if (!isCurrent()) return;
          emitBluetoothDiagnostic("info", "app-auth:challenge-response");
          await this.writeBleLineRaw(JSON.stringify({
            t: "ble_auth",
            id: credential.id,
            proof,
          }), true);
          return;
        }
        if (!this.blePairingOpen) {
          throw new Error("这台浏览器尚未登记。请停止播放，按住开发板 S7 三秒，看到蓝灯后重试。");
        }
        const enrollment = makeCredential();
        this.pendingEnrollment = enrollment;
        emitBluetoothDiagnostic("info", "app-auth:enrolling-new-browser");
        await this.writeBleLineRaw(JSON.stringify({
          t: "ble_enroll",
          id: enrollment.id,
          key: enrollment.key,
        }), true);
      } finally {
        if (isCurrent()) this.bleAuthorizationBusy = false;
      }
      return;
    }

    if (message.t === "ble_auth_error") {
      if (
        this.blePairingOpen &&
        (message.code === "unknown_client" || message.code === "proof_mismatch")
      ) {
        const deviceId = this.bleDevice?.id;
        if (!deviceId) throw new Error("浏览器没有提供稳定的蓝牙设备标识。");
        const enrollment = loadCredential(deviceId) ?? makeCredential();
        this.pendingEnrollment = enrollment;
        emitBluetoothDiagnostic("info", `app-auth:repairing-${message.code}`);
        this.bleAuthorizationBusy = true;
        try {
          await this.writeBleLineRaw(JSON.stringify({
            t: "ble_enroll",
            id: enrollment.id,
            key: enrollment.key,
          }), true);
        } finally {
          if (isCurrent()) this.bleAuthorizationBusy = false;
        }
        return;
      }
      throw new Error(
        message.code === "pairing_closed"
          ? "开发板的 S7 授权窗口已关闭，请按住 S7 三秒后重试。"
          : `开发板拒绝浏览器授权（${message.code ?? "unknown"}）。`,
      );
    }

    if (message.t === "ble_auth_ok") {
      const deviceId = this.bleDevice?.id;
      if (message.mode === "enrolled" && deviceId && this.pendingEnrollment) {
        storeCredential(deviceId, this.pendingEnrollment);
        emitBluetoothDiagnostic("info", "app-auth:credential-saved");
      }
      this.pendingEnrollment = null;
      this.bleAuthorized = true;
      emitBluetoothDiagnostic("info", `app-auth:ok mode=${message.mode ?? "unknown"}`);
    }
  }

  private handleBleDisconnected = () => {
    emitBluetoothDiagnostic("error", "gatt:disconnected");
    this.bleOperations?.invalidate();
    this.bleOperations = null;
    this.bleTx?.removeEventListener("characteristicvaluechanged", this.handleBleNotification);
    this.transport = null;
    this.bleRx = null;
    this.bleTx = null;
    this.bleServer = null;
    this.bleAuthorized = false;
    this.bleAuthorizationFailure = null;
    this.bleAuthorizationBusy = false;
    this.blePairingOpen = false;
    this.pendingEnrollment = null;
    this.bleProtocolSignalSeen = false;
    this.bleDecoder.reset();
    this.patch((s) =>
      markDisconnected(s, this.userDisconnected ? "未连接" : "蓝牙已断开，正在重连…"),
    );
  };

  private async openPort(port: SerialPort) {
    if (this.port === port && this.state.connected) return;
    if (this.openingPromise) {
      await this.openingPromise;
      return;
    }
    const opening = this.openPortOnce(port);
    this.openingPromise = opening;
    try {
      await opening;
    } finally {
      if (this.openingPromise === opening) this.openingPromise = null;
    }
  }

  private async openPortOnce(port: SerialPort) {
    if (this.port === port && this.state.connected) return;
    await this.closeConnection();
    this.port = port;
    try {
      await port.open({ baudRate: 115200 });
      await port.setSignals({ dataTerminalReady: true, requestToSend: false });
    } catch (error) {
      await this.closeConnection();
      throw error;
    }
    this.writer = port.writable?.getWriter() ?? null;
    this.reader = port.readable?.getReader() ?? null;
    this.readLoopActive = true;
    this.rxText = "";
    this.transport = "serial";
    this.patch((s) => markConnecting(s, "serial"));
    void this.writeLine('{"t":"ping"}');
    void this.readLoop();
    this.armStaleWatch();
  }

  private armStaleWatch() {
    if (this.staleTimer != null) window.clearInterval(this.staleTimer);
    this.staleTimer = window.setInterval(() => {
      if (!this.state.connected) return;
      const silentFor = Date.now() - this.state.updatedAt;
      if (this.state.sync === "connecting" && silentFor > 1500) {
        void this.recoverStaleSignals();
      } else if (silentFor > 4000 && this.state.sync === "synced") {
        this.patch((s) => ({ ...s, sync: "stale", updatedAt: Date.now() }));
        void this.recoverStaleSignals();
      } else if (this.state.sync === "stale") {
        void this.recoverStaleSignals();
      }
    }, 1000);
  }

  private async recoverStaleSignals() {
    if (this.signalRecoveryPromise || !this.state.connected) return;
    const recovery = (async () => {
      try {
        if (this.transport === "serial" && this.port) {
          await this.port.setSignals({ dataTerminalReady: true, requestToSend: false });
        }
        await this.writeLineOrThrow('{"t":"ping"}');
      } catch {
        this.patch((s) => markDisconnected(s, "连接无响应，正在重连…"));
        void this.closeConnection();
      }
    })();
    this.signalRecoveryPromise = recovery;
    try {
      await recovery;
    } finally {
      if (this.signalRecoveryPromise === recovery) this.signalRecoveryPromise = null;
    }
  }

  private async closeConnection() {
    if (this.closingPromise) return this.closingPromise;
    const closing = this.closeConnectionOnce();
    this.closingPromise = closing;
    try { await closing; }
    finally {
      if (this.closingPromise === closing) this.closingPromise = null;
    }
  }

  private async closeConnectionOnce() {
    if (this.staleTimer != null) {
      window.clearInterval(this.staleTimer);
      this.staleTimer = null;
    }
    this.readLoopActive = false;
    const reader = this.reader;
    const writer = this.writer;
    const port = this.port;
    const device = this.bleDevice;
    const tx = this.bleTx;
    this.bleOperations?.invalidate();
    this.bleOperations = null;
    this.reader = null;
    this.writer = null;
    this.port = null;
    this.bleDevice = null;
    this.bleServer = null;
    this.bleRx = null;
    this.bleTx = null;
    this.transport = null;
    this.bleAuthorized = false;
    this.bleAuthorizationFailure = null;
    this.bleAuthorizationBusy = false;
    this.blePairingOpen = false;
    this.pendingEnrollment = null;
    this.bleProtocolSignalSeen = false;
    this.bleDecoder.reset();
    try { await reader?.cancel(); } catch { /* ignore */ }
    try { reader?.releaseLock(); writer?.releaseLock(); } catch { /* ignore */ }
    try { await port?.close(); } catch { /* ignore */ }
    // Disconnect terminates notifications. Calling stopNotifications here could
    // itself overlap an in-flight write and needlessly delay releasing the link.
    tx?.removeEventListener("characteristicvaluechanged", this.handleBleNotification);
    device?.removeEventListener("gattserverdisconnected", this.handleBleDisconnected);
    try { device?.gatt?.disconnect(); } catch { /* ignore */ }
  }

  /** Kept for focused serial tests and the wired recovery path. */
  private async closePort() { await this.closeConnection(); }

  private async readLoop() {
    const reader = this.reader;
    if (!reader) return;
    try {
      while (this.readLoopActive) {
        const { value, done } = await reader.read();
        if (done) break;
        if (!value) continue;
        this.rxText += this.decoder.decode(value, { stream: true });
        let nl: number;
        while ((nl = this.rxText.indexOf("\n")) >= 0) {
          const line = this.rxText.slice(0, nl).trim();
          this.rxText = this.rxText.slice(nl + 1);
          if (line) this.onLine(line);
        }
      }
    } catch { /* disconnect */ }
    finally {
      this.readLoopActive = false;
      this.patch((s) =>
        markDisconnected(s, this.userDisconnected ? "未连接" : "USB 已断开，正在重连…"),
      );
      void this.closePort();
    }
  }

  private onLine(line: string) {
    this.patch((s) => reduceHostLine(s, line));
    // Observe received data and the state used by the view, without logging credentials
    // or producing a request for every sequencer tick.
    if (
      this.transport !== "ble" || typeof window === "undefined"
    ) return;
    const message = parseHostLine(line);
    if (!message) return;
    const state = this.state;
    if (message.t === "state") {
      const summary = JSON.stringify({
        received: {
          bpm: message.bpm, run: message.run, variation: message.var,
          fill: message.fill, click: message.click, drumMode: message.mode,
        },
        applied: {
          bpm: state.bpm, run: state.running, variation: state.variation,
          fill: state.fill, click: state.click, drumMode: state.drumMode,
          connected: state.connected, sync: state.sync, link: state.link,
        },
      });
      if (summary !== this.lastProtocolStateDiagnostic) {
        this.lastProtocolStateDiagnostic = summary;
        emitBluetoothDiagnostic("info", `protocol:state ${summary}`);
      }
    } else if (message.t === "key") {
      emitBluetoothDiagnostic("info", `protocol:key ${JSON.stringify({
        index: message.i, value: message.v,
        keysDown: state.keysDown,
        flashing: state.keyFlashUntil.map((until) => until > Date.now()),
      })}`);
    } else if (message.t === "hello") {
      emitBluetoothDiagnostic("info", `protocol:hello v=${message.v} name=${message.name} link=${state.link}`);
    } else if (message.t === "pattern") {
      emitBluetoothDiagnostic("info", `protocol:pattern bank=${message.bank} revision=${message.rev} sync=${state.sync}`);
    }
  }

  private async writeBleLineRaw(line: string, withResponse = false) {
    const rx = this.bleRx;
    const operations = this.bleOperations;
    if (!rx || !operations) throw new Error("蓝牙写入特征尚未准备好。");
    const command = parseCommandType(line);
    // Queue the entire JSON message so packets from two commands cannot interleave.
    await operations.run(`write:${command}:${withResponse ? "ack" : "no-ack"}`, async () => {
      for (const packet of encodeBleLine(line)) {
        if (this.bleOperations !== operations || !operations.isActive || this.bleRx !== rx) {
          throw Object.assign(new Error("Bluetooth connection changed during write."), { name: "AbortError" });
        }
        if (withResponse) {
          await rx.writeValueWithResponse(packet.buffer as ArrayBuffer);
        } else {
          await rx.writeValueWithoutResponse(packet.buffer as ArrayBuffer);
        }
      }
    });
  }

  private async waitForBleAuthorization() {
    emitBluetoothDiagnostic("info", "app-auth:waiting-for-challenge");
    const deadline = Date.now() + BLE_AUTH_TIMEOUT_MS;
    while (!this.bleAuthorized || this.bleAuthorizationBusy) {
      if (this.bleAuthorizationFailure) throw this.bleAuthorizationFailure;
      if (!this.bleServer?.connected) {
        throw Object.assign(new Error("GATT disconnected during device authorization."), {
          name: "NetworkError",
        });
      }
      if (Date.now() >= deadline) {
        throw Object.assign(new Error("Timed out waiting for device authorization."), {
          name: "NetworkError",
        });
      }
      await new Promise<void>((resolve) => window.setTimeout(resolve, BLE_READY_POLL_MS));
    }
    if (this.bleAuthorizationFailure) throw this.bleAuthorizationFailure;
    emitBluetoothDiagnostic("info", "app-auth:ready");
  }

  private async waitForBleProtocolSignal() {
    emitBluetoothDiagnostic("info", "protocol:waiting-for-device-ready");
    const deadline = Date.now() + BLE_AUTH_TIMEOUT_MS;
    while (!this.bleProtocolSignalSeen) {
      if (!this.bleServer?.connected) {
        throw Object.assign(new Error("GATT disconnected while the protocol handshake was pending."), {
          name: "NetworkError",
        });
      }
      if (Date.now() >= deadline) {
        throw Object.assign(new Error("Timed out waiting for the Beatbox protocol signal."), {
          name: "NetworkError",
        });
      }
      await new Promise<void>((resolve) => window.setTimeout(resolve, BLE_READY_POLL_MS));
    }
    emitBluetoothDiagnostic("info", "protocol:device-ready");
  }

  private async writeLineOrThrow(line: string) {
    if (this.transport === "ble" && this.bleRx) {
      const operations = this.bleOperations;
      try {
        await this.writeBleLineRaw(line);
        return;
      } catch (cause) {
        if (this.bleOperations === operations) {
          this.patch((s) => markDisconnected(s, "蓝牙写入失败，正在重连…"));
          void this.closeConnection();
        }
        throw new Error("向设备发送蓝牙命令失败，请重新连接后再试。", { cause });
      }
    }
    if (this.transport === "serial" && this.writer) {
      try {
        await this.writer.write(this.encoder.encode(line + "\n"));
        return;
      } catch (cause) {
        this.patch((s) => markDisconnected(s, "USB 写入失败，正在重连…"));
        void this.closeConnection();
        throw new Error("向设备发送 USB 命令失败，请重新连接后再试。", { cause });
      }
    }
    throw new Error("设备尚未连接。");
  }

  private async writeLine(line: string) {
    try { await this.writeLineOrThrow(line); } catch { /* reconnect loop handles it */ }
  }

  sendStart() { void this.writeLine('{"t":"start"}'); }
  sendContinue() { void this.writeLine('{"t":"continue"}'); }
  sendStop() { void this.writeLine('{"t":"stop"}'); }

  sendBpm(bpm: number) {
    const v = clampBpm(bpm);
    this.patch((s) => applyLocalBpmDraft(s, v));
    void this.writeLine(`{"t":"bpm","v":${v}}`);
    window.setTimeout(() => this.patch((s) => clearBpmDraft(s)), 400);
  }

  sendSwing(swing: number) {
    const v = clampSwing(swing);
    this.patch((s) => ({ ...s, swing: v, updatedAt: Date.now() }));
    void this.writeLine(`{"t":"swing","v":${v}}`);
  }

  sendVariation(varIndex: number) {
    const v = varIndex ? 1 : 0;
    this.patch((s) => ({ ...s, variation: v, updatedAt: Date.now() }));
    void this.writeLine(`{"t":"variation","v":${v}}`);
  }

  sendFill(held: boolean) {
    this.patch((s) => ({ ...s, fill: held, updatedAt: Date.now() }));
    void this.writeLine(`{"t":"fill","v":${held ? 1 : 0}}`);
  }

  sendRecord(armed: boolean) { void this.writeLine(`{"t":"record","v":${armed ? 1 : 0}}`); }

  sendNote(note: number, velocity = 127) {
    const n = note & 0x7f;
    const v = Math.max(1, velocity & 0x7f);
    this.patch((s) => ({ ...s, lastPadNote: n, updatedAt: Date.now() }));
    void this.writeLine(`{"t":"note","n":${n},"v":${v}}`);
  }

  sendClick(enabled: boolean) {
    this.patch((s) => ({ ...s, click: enabled, updatedAt: Date.now() }));
    void this.writeLine(`{"t":"click","v":${enabled ? 1 : 0}}`);
  }

  sendMode(drumMode: boolean) {
    this.patch((s) => ({ ...s, drumMode, updatedAt: Date.now() }));
    void this.writeLine(`{"t":"mode","v":${drumMode ? 1 : 0}}`);
  }

  sendVolume(volume: number) {
    const v = clampVolume(volume);
    this.patch((s) => applyLocalVolumeDraft(s, v));
    void this.writeLine(`{"t":"volume","v":${v}}`);
    window.setTimeout(() => this.patch((s) => clearVolumeDraft(s)), 400);
  }

  requestPattern() { void this.writeLine('{"t":"pattern_get"}'); }

  setLocalPattern(pattern: PatternBanks, bank: 0 | 1 | 2 = 0) {
    this.patch((s) => applyLocalPattern(s, pattern));
    this.pendingCommitBank = bank;
    if (this.commitTimer != null) window.clearTimeout(this.commitTimer);
    this.commitTimer = window.setTimeout(() => {
      const target = this.pendingCommitBank ?? 0;
      this.pendingCommitBank = null;
      this.commitPatternBank(target);
    }, 180);
  }

  setLocalPatternAll(pattern: PatternBanks) {
    this.patch((s) => applyLocalPattern(s, pattern));
    if (this.commitTimer != null) window.clearTimeout(this.commitTimer);
    this.pendingCommitBank = null;
    this.commitTimer = window.setTimeout(() => {
      this.commitPatternBank(0);
      this.commitPatternBank(1);
      this.commitPatternBank(2);
    }, 180);
  }

  commitPatternBank(bank: 0 | 1 | 2) {
    const s = this.state;
    if (!s.connected) return;
    const hex = bankHex(s.pattern, bank);
    void this.writeLine(`{"t":"pattern_set","bank":${bank},"rev":${s.revision},"p":"${hex}"}`);
  }

  sendSave() { void this.writeLine('{"t":"save"}'); }
}
