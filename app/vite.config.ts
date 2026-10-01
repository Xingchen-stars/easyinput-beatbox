import { defineConfig } from "vite";

export default defineConfig({
  /* Relative base so Electron can load app/dist via loadFile. */
  base: "./",
  plugins: [
    {
      name: "beatbox-ble-local-diagnostics",
      configureServer(server) {
        server.middlewares.use("/__beatbox_ble_log", (request, response, next) => {
          if (request.method !== "POST") {
            next();
            return;
          }
          const chunks: Buffer[] = [];
          request.on("data", (chunk) => chunks.push(Buffer.from(chunk)));
          request.on("end", () => {
            const raw = Buffer.concat(chunks).toString("utf8");
            try {
              const event = JSON.parse(raw) as {
                at?: unknown;
                level?: unknown;
                message?: unknown;
              };
              const at = String(event.at ?? new Date().toISOString());
              const level = String(event.level ?? "info").toUpperCase();
              const message = String(event.message ?? raw);
              console.log(`WEB_BLE ${at} ${level} ${message}`);
            } catch {
              console.log(`WEB_BLE ${new Date().toISOString()} RAW ${raw}`);
            }
            response.statusCode = 204;
            response.end();
          });
        });
      },
    },
  ],
  server: {
    port: 5173,
    host: "127.0.0.1",
    /* Desktop runner sets BROWSER=none; keep Chrome open for plain `pnpm dev`. */
    open: process.env.BROWSER !== "none",
  },
});
