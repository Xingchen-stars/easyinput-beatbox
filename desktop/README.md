# EasyInput Beatbox Desktop

Electron 套壳：复用 `app/` 的 Web Bluetooth / Web Serial UI。日常控制优先使用直连蓝牙，USB 作为备用；Electron 同时处理这两类设备授权。

## 开发

先起 Vite，再开桌面窗：

```bash
# terminal 1
pnpm dev

# terminal 2
pnpm desktop:dev
```

或一条命令（会等 5173 就绪）：

```bash
pnpm desktop
```

## 生产加载

先构建网页资源，再启动 Electron 加载 `app/dist`：

```bash
pnpm build
pnpm desktop:start
```

macOS 窗口为 `hiddenInset`（无高大标题栏，仅红绿灯）。默认约 1180×760，最小宽 1040，最大高 840。
