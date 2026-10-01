# EasyInput Beatbox v1.0.0 安全更新包

> 历史候选包，保留用于追溯，不是 2026-10-01 固定标签的浏览器钥匙版本。当前保存版本请使用 [release-v1.0.0-browser-hmac](../release-v1.0.0-browser-hmac/README.md)，不要混用本目录的旧镜像或校验文件。

这是双模式 EasyInput V2.0 开发板的 **Beatbox 分区更新包**。它只更新 `beatbox / ota_0`，不会主动覆盖 EasyInput 键盘模式、NVS、声音资源、Bootloader 或分区表。

> 当前状态：固件已经通过 ESP-IDF 5.5.5 构建，等待真实开发板最终验收。验收完成前，此目录按候选发布包管理，不应标记为已验证正式版。

## 文件

| 文件 | 用途 |
| --- | --- |
| `easyinput_beatbox_v1.0.0.bin` | Beatbox 应用镜像，只能写入 `0x430000` |
| `partitions.csv` | 双模式分区表文字参考，供 `otatool.py` 按名称找到 `beatbox` |
| `flash_args.beatbox-update` | 只包含 `0x430000` 的安全写入参数 |
| `SHA256SUMS.txt` | 发布文件 SHA-256 校验值 |
| `easyinput-beatbox-firmware-v1.0.0.zip` | GitHub Release 固件资产，包含本表前四项和本说明 |
| `easyinput-beatbox-web-v1.0.0.zip` | 可独立托管的网页静态文件 |
| `RELEASE-ASSETS.sha256` | 两个 GitHub Release ZIP 的 SHA-256 校验值 |

## 重要安全边界

- 不要对这个双模式包执行普通的 `idf.py flash`。
- 不要把 `easyinput_beatbox_v1.0.0.bin` 写到 `0x10000`；那里是保留的 EasyInput 键盘模式。
- 不要擦除整片 Flash。
- 不要改写 `0x9000` NVS；BLE 绑定和其它设备设置位于共享 NVS 中。
- 烧录前重新核对目标开发板、ESP32-S3 芯片、MAC、串口和分区布局。

## 写入命令

在已经激活 ESP-IDF 5.5.5 环境的终端中，把 `<PORT>` 换成当次识别到的串口：

```bash
python -m esptool --chip esp32s3 --port <PORT> --baud 460800 \
  write_flash @flash_args.beatbox-update
```

如果设备没有自动进入 Beatbox 槽位，再把启动选择切到名为 `beatbox` 的分区：

```bash
python %IDF_PATH%/components/app_update/otatool.py \
  --port <PORT> --partition-table-file partitions.csv \
  switch_ota_partition --name beatbox
```

最后完全断电再开机，并按 [`docs/release-v1.0.0-checklist.md`](../../docs/release-v1.0.0-checklist.md) 做实机验收。
