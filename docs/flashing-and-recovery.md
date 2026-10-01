# v1.0.0 烧录与恢复

目标是恢复固定的“浏览器钥匙版”，不把新构建的其他镜像误当作已验证的这一版。本指南针对已经具备下述双模式分区表的 EasyInput V2.0；不适用于任意空白 ESP32-S3。

## 1. 选择正确的包

从 GitHub Release 下载 `easyinput-beatbox-firmware-v1.0.0-browser-hmac.zip`，或使用恢复修正标签 `v1.0.0-backup.1` 里的 `artifacts/release-v1.0.0-browser-hmac/`。本版镜像：

```text
easyinput_beatbox_v1.0.0_browser_hmac.bin
985264 bytes
SHA-256: dea3720fbe7e20b45eaed2784bea83cc235f8046dd11ec80223afb1b1fe784f8
```

PowerShell 检查文件：

```powershell
Get-FileHash -Algorithm SHA256 .\easyinput_beatbox_v1.0.0_browser_hmac.bin
```

有完整源码时可运行 `scripts/verify_release.ps1` 校验固定目录中的全部文件。哈希不符时停止使用该包。

## 2. 核对硬件与分区

准备 ESP-IDF 5.5.5 / esptool 4.x，以及支持数据传输的 USB-C 线。烧录前停止所有串口监视器，并断开浏览器 USB 连接。重新识别当次串口、ESP32-S3 芯片和设备 MAC，确认是要更新的板子。

| 分区 | 地址 | 大小 | 本版更新是否写入 |
| --- | --- | --- | --- |
| NVS | `0x9000` | `0x6000` | 否 |
| 射频数据 | `0xf000` | `0x1000` | 否 |
| factory / EasyInput 键盘 | `0x10000` | `0x300000` | 否 |
| 声音 A | `0x310000` | `0x90000` | 否 |
| 声音 B | `0x3a0000` | `0x90000` | 否 |
| beatbox / ota_0 | `0x430000` | `0x200000` | 是，仅应用镜像 |
| OTA 启动选择 | `0x630000` | `0x2000` | 只在明确需要切换启动槽时更新 |

包中的 `partitions.csv` 只是文字参考，不会自动写分区表。不要对双模式板直接执行普通 `idf.py flash`；它的默认应用地址可能覆盖 `0x10000` 的键盘模式。不要整片擦除，也不要把应用镜像写到 `0x10000`。

## 3. 进入下载模式并只更新 Beatbox

当前 EasyInput 板的操作是：**保持开机，短按并松开一次 BOOT**。等待下载端口出现，再核对身份。不要按住 BOOT 再上电，板上也没有独立 RESET 键。

在解压后的固件目录执行；`<PORT>` 换成当次确认的端口：

```bash
python -m esptool --chip esp32s3 --port <PORT> --baud 460800 --before no_reset --after no_reset write_flash @flash_args.beatbox-update
```

本包参数只包含 `0x430000` 和本版应用镜像。看到 `Hash of data verified` 才说明写入后的数据校验通过。`--after no_reset` 会保持下载状态，稍后需要正常重新上电。

esptool 5 的命令名使用连字符（例如 `write-flash`、`no-reset`）；本指南按实际验证的 ESP-IDF 5.5.5 / esptool 4.x 记录，不混用版本语法。

## 4. 必要时切换启动槽

已从 Beatbox 启动的板子，只更新应用即可。仍处于 EasyInput factory 的板子，可以使用两模式中已有的旋钮长按五秒菜单切到“鼓机模式”。

需要直接维护启动选择时，在已激活 ESP-IDF 5.5.5 的终端中使用其 `otatool.py`：

```powershell
python "$env:IDF_PATH\components\app_update\otatool.py" --port <PORT> --partition-table-file .\partitions.csv switch_ota_partition --name beatbox
```

```bash
python "$IDF_PATH/components/app_update/otatool.py" --port <PORT> --partition-table-file partitions.csv switch_ota_partition --name beatbox
```

这一步会写 OTA 启动选择，不能把它理解成纯查询。以设备实际分区表为准；CSV 不匹配时不要执行。不要使用 `erase_otadata` 代替选择 Beatbox。

## 5. 恢复正常启动

所有烧录、启动选择与最终身份复核结束后，用板上开关**关机，再正常开机，不要按 BOOT**。确认所有供电条件确实完成正常重启。

正常上电以后用应用协议验证，不再运行 esptool 的设备识别来检查启动状态：识别工具会重新扰动复位/下载状态。

```bash
python scripts/verify_beatbox_serial.py --port <PORT> --timeout 12
```

该脚本在仓库根目录执行；使用带 pyserial 的 Python。预期得到协议 v3、`ble_direct`、状态以及 Pattern bank 0/1/2。脚本结束会关闭串口，随后才能在网页连接 USB。

## 6. 网页与蓝牙

使用 Chrome / Edge 打开 HTTPS 网站或本地开发服务器。首次或换网站后：停止播放 → S7 按住三秒 → 蓝灯 → 网页蓝牙连接 → 选择 `EasyInput Beatbox`。同一浏览器再次连接用保存的钥匙自动完成挑战应答。

发布包的网页文件可以托管到 GitHub Pages，也可在源码中运行 `pnpm install`、`pnpm dev`。网页需要 HTTP/HTTPS；不要依赖 `file://` 下的蓝牙权限。

## 7. 常见问题

| 现象 | 检查与处理 |
| --- | --- |
| `Failed to open serial port` | 停止 Python/ESP-IDF 串口监听；断开其他浏览器标签中的 USB，保持只一个使用者 |
| 选得到蓝牙但新浏览器被拒绝 | 停止播放后打开 S7 三秒登记窗口；不要输入旧设备配网码 |
| 换网站以后需要再按 S7 | 网站地址不同，浏览器本地钥匙是分开保存的，属于正常的新登记 |
| 烧录校验通过却没有应用状态 | 按当前板的“关机 → 开机”恢复；不要再次按 BOOT 或直接断言程序坏了 |
| 看不到串口 | 检查数据线、开关、供电和系统端口列表；端口名不是永久设备身份 |
| 要找回这一版 | 使用 `v1.0.0` 标签和固定哈希的发布镜像，不用最新分支代替 |

USB 供电与 USB 网页连接是两回事。只插着线供电并不会必然抢占串口；打开串口监听或网页 USB 连接才会占用。

## 8. 恢复范围

本版备份保存程序和发布文件，不上传共享 NVS、账号、网站钥匙或 Wi-Fi 密码。它不是整块设备的私有 Flash 镜像。空白板、损坏的 Bootloader、不同分区布局或缺少 factory 的板子，需要另行核对匹配的底座恢复材料；不要将本包冒充整机恢复包。

`easyinput-factory/` 保存键盘模式集成源码，许可证和状态单独列明。历史双模式二进制保留在本机旧目录中，不会悄悄混入本次已验证应用的默认烧录参数。
