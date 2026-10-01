#!/usr/bin/env node
/** Continuously capture Beatbox browser BLE stages through local Edge CDP. */

const args = process.argv.slice(2);
const valueAfter = (name, fallback) => {
  const index = args.indexOf(name);
  return index >= 0 && args[index + 1] ? args[index + 1] : fallback;
};
const port = Number(valueAfter("--port", "9225"));
const timeoutSeconds = Number(valueAfter("--timeout", "900"));
const targetUrl = valueAfter("--url", "http://127.0.0.1:5173/");

if (!Number.isInteger(port) || port < 1 || port > 65535 || !Number.isFinite(timeoutSeconds)) {
  console.error("ERROR=bad_arguments");
  process.exit(64);
}

const deadline = Date.now() + timeoutSeconds * 1000;
const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

async function findTarget() {
  while (Date.now() < deadline) {
    try {
      const response = await fetch(`http://127.0.0.1:${port}/json/list`);
      const targets = await response.json();
      const match = targets.find((item) => item.type === "page" && item.url?.startsWith(targetUrl));
      if (match?.webSocketDebuggerUrl) return match;
    } catch {
      // Edge may still be starting.
    }
    await delay(500);
  }
  throw new Error("Beatbox page was not exposed by Edge before timeout");
}

const target = await findTarget();
const socket = new WebSocket(target.webSocketDebuggerUrl);
let nextId = 1;
let lastState = "";
const pending = new Map();

function send(method, params = {}) {
  const id = nextId++;
  socket.send(JSON.stringify({ id, method, params }));
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
  });
}

function renderedArgument(argument) {
  if (typeof argument.value === "string") return argument.value;
  if (argument.value != null) return JSON.stringify(argument.value);
  return argument.description ?? argument.type ?? "";
}

socket.addEventListener("message", (event) => {
  const message = JSON.parse(event.data);
  if (message.id && pending.has(message.id)) {
    const request = pending.get(message.id);
    pending.delete(message.id);
    if (message.error) request.reject(new Error(message.error.message));
    else request.resolve(message.result);
    return;
  }
  if (message.method === "Runtime.consoleAPICalled") {
    const text = message.params.args.map(renderedArgument).join(" ");
    if (text.includes("[Beatbox BLE]")) {
      console.log(`BROWSER ${message.params.type.toUpperCase()} ${text}`);
    }
  } else if (message.method === "Runtime.exceptionThrown") {
    console.log(`BROWSER EXCEPTION ${message.params.exceptionDetails.text}`);
  } else if (message.method === "Log.entryAdded") {
    const entry = message.params.entry;
    if (entry.level === "error" || entry.level === "warning") {
      console.log(`BROWSER ${entry.level.toUpperCase()} ${entry.text}`);
    }
  }
});

await new Promise((resolve, reject) => {
  socket.addEventListener("open", resolve, { once: true });
  socket.addEventListener("error", reject, { once: true });
});
await send("Runtime.enable");
await send("Log.enable");
console.log(`BROWSER_LISTENING=yes URL=${target.url} TIMEOUT=${timeoutSeconds}s`);

while (Date.now() < deadline && socket.readyState === WebSocket.OPEN) {
  const result = await send("Runtime.evaluate", {
    expression: `JSON.stringify({
      label: document.querySelector('#connLabel')?.textContent ?? '',
      error: document.querySelector('#err')?.hidden ? '' : document.querySelector('#err')?.textContent ?? '',
      bluetooth: Boolean(navigator.bluetooth),
      serial: Boolean(navigator.serial)
    })`,
    returnByValue: true,
  });
  const state = result?.result?.value ?? "";
  if (state && state !== lastState) {
    lastState = state;
    console.log(`PAGE_STATE ${state}`);
  }
  await delay(500);
}

socket.close();
console.log("BROWSER_CAPTURE_COMPLETE=yes");
