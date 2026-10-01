import { app, BrowserWindow, session } from "electron";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const isDev = process.argv.includes("--dev");
const ESPRESSIF_VID = 0x303a;
const DEV_URL = "http://127.0.0.1:5173";
const BEATBOX_BLE_NAME = "EasyInput Beatbox";
const DEVICE_PERMISSIONS = new Set(["serial", "bluetooth", "bluetooth-scanning"]);

/** @type {BrowserWindow | null} */
let mainWindow = null;

function isTrustedRendererOrigin(origin) {
  return origin === "file://" || origin.startsWith("file:///") || origin.startsWith(DEV_URL);
}

function wireDevicePermissions() {
  const ses = session.defaultSession;

  ses.setPermissionCheckHandler((_wc, permission, requestingOrigin) => {
    return DEVICE_PERMISSIONS.has(permission) && isTrustedRendererOrigin(requestingOrigin);
  });

  ses.setPermissionRequestHandler((webContents, permission, callback) => {
    callback(DEVICE_PERMISSIONS.has(permission) && isTrustedRendererOrigin(webContents.getURL()));
  });

  ses.setDevicePermissionHandler(
    (details) => details.deviceType === "serial" && isTrustedRendererOrigin(details.origin),
  );

  ses.on("select-serial-port", (event, portList, _webContents, callback) => {
    event.preventDefault();
    if (!portList.length) {
      callback("");
      return;
    }
    const preferred =
      portList.find((port) => {
        const vid = Number(port.vendorId);
        return vid === ESPRESSIF_VID;
      }) ?? portList[0];
    callback(preferred.portId);
  });
}

async function loadRenderer(win) {
  if (isDev) {
    let lastError = null;
    for (let attempt = 0; attempt < 60; attempt++) {
      try {
        await win.loadURL(DEV_URL);
        return;
      } catch (err) {
        lastError = err;
        await new Promise((r) => setTimeout(r, 250));
      }
    }
    throw lastError ?? new Error(`Dev server not reachable at ${DEV_URL}. Run pnpm dev first.`);
  }

  const indexHtml = path.join(__dirname, "../app/dist/index.html");
  await win.loadFile(indexHtml);
}

async function createWindow() {
  const isMac = process.platform === "darwin";

  /* Fixed fit for the Beatbox layout (header + dual panels); no free resize. */
  const winWidth = 1140;
  const winHeight = 710;

  mainWindow = new BrowserWindow({
    width: winWidth,
    height: winHeight,
    minWidth: winWidth,
    maxWidth: winWidth,
    minHeight: winHeight,
    maxHeight: winHeight,
    resizable: false,
    maximizable: false,
    fullscreenable: false,
    title: "EasyInput Beatbox",
    backgroundColor: "#0e1116",
    /* macOS: content under titlebar, no tall chrome strip. */
    ...(isMac
      ? {
          titleBarStyle: "hiddenInset",
          /* Sit in the reserved spacer row above the title lockup. */
          trafficLightPosition: { x: 14, y: 12 },
        }
      : {}),
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
    show: false,
  });

  let pendingBluetoothSelection = null;
  const finishBluetoothSelection = (deviceId = "") => {
    if (!pendingBluetoothSelection) return;
    clearTimeout(pendingBluetoothSelection.timer);
    const { callback } = pendingBluetoothSelection;
    pendingBluetoothSelection = null;
    callback(deviceId);
  };

  mainWindow.webContents.on("select-bluetooth-device", (event, deviceList, callback) => {
    event.preventDefault();
    if (!pendingBluetoothSelection) {
      pendingBluetoothSelection = {
        callback,
        timer: setTimeout(() => finishBluetoothSelection(), 15000),
      };
    }
    const target = deviceList.find((device) => device.deviceName?.startsWith(BEATBOX_BLE_NAME));
    if (target) finishBluetoothSelection(target.deviceId);
  });

  mainWindow.once("ready-to-show", () => {
    /* Force exact bounds — main-process edits need a full Electron restart. */
    mainWindow?.setSize(winWidth, winHeight);
    mainWindow?.show();
  });

  mainWindow.webContents.on("did-finish-load", () => {
    void mainWindow?.webContents.executeJavaScript(
      `document.documentElement.classList.add('electron-shell');`,
    );
  });

  await loadRenderer(mainWindow);

  if (isDev) {
    mainWindow.webContents.openDevTools({ mode: "detach" });
  }

  mainWindow.on("closed", () => {
    finishBluetoothSelection();
    mainWindow = null;
  });
}

app.whenReady().then(async () => {
  wireDevicePermissions();
  await createWindow();

  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      void createWindow();
    }
  });
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit();
});
