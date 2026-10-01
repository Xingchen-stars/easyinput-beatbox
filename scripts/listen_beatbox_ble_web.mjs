#!/usr/bin/env node
/** Receive opt-in BLE diagnostics from the local Beatbox page. */

import http from "node:http";

const args = process.argv.slice(2);
const valueAfter = (name, fallback) => {
  const index = args.indexOf(name);
  return index >= 0 && args[index + 1] ? args[index + 1] : fallback;
};
const port = Number(valueAfter("--port", "8791"));
const timeoutSeconds = Number(valueAfter("--timeout", "900"));

if (!Number.isInteger(port) || port < 1 || port > 65535 || !Number.isFinite(timeoutSeconds)) {
  console.error("ERROR=bad_arguments");
  process.exit(64);
}

const server = http.createServer((request, response) => {
  response.setHeader("Access-Control-Allow-Origin", "*");
  response.setHeader("Cache-Control", "no-store");
  if (request.method === "OPTIONS") {
    response.writeHead(204);
    response.end();
    return;
  }
  if (request.method === "GET" && request.url === "/health") {
    response.writeHead(200, { "Content-Type": "text/plain; charset=utf-8" });
    response.end("ok");
    return;
  }
  if (request.method !== "POST" || request.url !== "/beatbox-ble-log") {
    response.writeHead(404);
    response.end();
    return;
  }

  const chunks = [];
  request.on("data", (chunk) => chunks.push(chunk));
  request.on("end", () => {
    const raw = Buffer.concat(chunks).toString("utf8");
    try {
      const event = JSON.parse(raw);
      const level = String(event.level ?? "info").toUpperCase();
      const at = String(event.at ?? new Date().toISOString());
      const message = String(event.message ?? raw);
      console.log(`WEB_BLE ${at} ${level} ${message}`);
    } catch {
      console.log(`WEB_BLE ${new Date().toISOString()} RAW ${raw}`);
    }
    response.writeHead(204);
    response.end();
  });
});

server.listen(port, "127.0.0.1", () => {
  console.log(`WEB_BLE_LISTENING=yes URL=http://127.0.0.1:${port}/beatbox-ble-log TIMEOUT=${timeoutSeconds}s`);
});

const timer = setTimeout(() => {
  server.close(() => {
    console.log("WEB_BLE_CAPTURE_COMPLETE=yes");
  });
}, timeoutSeconds * 1000);
timer.unref();

process.on("SIGINT", () => {
  server.close(() => process.exit(130));
});
