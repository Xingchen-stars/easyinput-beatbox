# v1.0.0 浏览器钥匙版：固定 Beatbox 更新包

这是 2026-10-01 保存的已烧录版本。与 `diagnostic-ble-2026-09-30` 中最终成功的浏览器 HMAC 镜像逐字节相同。旧 `release-v1.0.0` 候选包不是这一版，继续保留但不要混用。

| 项目 | 值 |
| --- | --- |
| 芯片 / 项目版本 | ESP32-S3 / 1.0.0 |
| ESP-IDF | 5.5.5 |
| 镜像 | `easyinput_beatbox_v1.0.0_browser_hmac.bin` |
| 大小 | 985264 bytes |
| SHA-256 | `dea3720fbe7e20b45eaed2784bea83cc235f8046dd11ec80223afb1b1fe784f8` |
| 写入地址 / 分区大小 | `0x430000` / `0x200000` |
| 镜像校验 | checksum 与 validation hash 有效 |

## 文件

- `.bin`：实际验证版本，既保存在 Git 标签中，也在固件 ZIP 内。
- `partitions.csv`：文字分区参考，不是自动执行的分区表更新。
- `flash_args.beatbox-update`：只包含 Beatbox 地址，不包含 factory/NVS/Bootloader。
- `sdkconfig.build`：生成固定镜像时实际使用的 ESP-IDF 构建配置，只作为构建参考。
- `dependencies.lock`：构建时锁定的 led_strip 2.5.5 与 ESP-IDF 5.5.5。
- `FIRMWARE-SOURCE.sha256`：对应板端源文件的指纹。
- `manifest.json`：公开版本信息与验证边界，不包含设备 MAC、账号或钥匙。
- `SHA256SUMS.txt`：包内文件校验。
- 固件、网页、源码 ZIP 与 Git bundle：在 GitHub Release 和本机此目录保存；ZIP/bundle 不重复纳入 Git 对象。
- `RELEASE-ASSETS.sha256`：上传资产校验。

## 最短更新流程

1. 停止串口监听和网页 USB 连接；核对开发板、ESP32-S3、当次端口和分区。
2. 保持开机，短按并松开 BOOT 一次；重新核对目标身份。
3. 在本目录执行下列命令，替换 `<PORT>`。
4. 哈希验证通过后，必要时切换到 `beatbox` 启动分区。
5. 最终关机，再正常开机；不要再按 BOOT。

```bash
python -m esptool --chip esp32s3 --port <PORT> --baud 460800 --before no_reset --after no_reset write_flash @flash_args.beatbox-update
```

详细的 Windows/其他系统步骤、启动分区切换和 USB 冲突说明见 [烧录与恢复指南](../../docs/flashing-and-recovery.md)。只有固件 ZIP 时，包内 `flashing-and-recovery.md` 是同一份说明。

本包不会恢复空白板的 Bootloader、factory 和声音资源，也不保存 NVS 私有数据。不要写 `0x10000`、不要整片擦除、不要对双模式板直接使用单工程的 `idf.py flash`。

## 验证范围

已确认：源码匹配、构建、镜像校验、写入校验、普通应用协议、广播、S7 窗口内首次登记、已登记浏览器 HMAC 再连接和用户确认 USB 备用恢复。完整键位/速度回归、自动重连和长期运行仍需单独验收。本版保留明文登记/控制流量的安全边界。
