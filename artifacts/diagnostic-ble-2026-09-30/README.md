# Beatbox BLE 白盒诊断固件

- 状态：已编译并于 2026-09-30 写入目标开发板；重新上电后的应用协议验收通过，等待 BLE 连接复现。
- 文件：`easyinput_beatbox_ble_diag.bin`
- 目标芯片：ESP32-S3
- 项目版本：1.0.0
- ESP-IDF：v5.5.5
- 文件大小：980112 bytes
- SHA-256：`09002F9FCAE7A8065FA16F6B0395E96A4325A03811EE37AA81D1C21C39DEFB10`
- 镜像校验：esptool 识别为 ESP32-S3，checksum 与 validation hash 均有效。
- 计划写入地址：`0x430000`（Beatbox OTA 分区）；不得覆盖 `0x10000` 的 factory 分区。

## 300 ms 延时安全握手候选版

- 状态：已写入目标板并完成白盒复现；**300 ms 延时假设已被实测排除**。
- 文件：`easyinput_beatbox_ble_delay_300ms_diag.bin`
- 文件大小：980752 bytes
- SHA-256：`50E3D576DCC65A56A5C01E3AF93ABE28EAE50BA8D2A6B79FBA3CE7C0511175B3`
- 镜像校验：esptool 识别为 ESP32-S3，checksum 与 validation hash 均有效。
- 计划写入地址：`0x430000`（只更新 Beatbox OTA 分区）。
- 行为变化：Chrome 完成通知订阅后，不再在 GAP 订阅回调中立即启动 SMP；固件记录连接句柄并延迟 300 ms，由主循环发起安全握手。断开、取消订阅或提前完成加密时会取消待执行动作。
- 新增白盒事件：`security_schedule`、带 `delay_ms` 的 `security_initiate`，以及状态变化时的 `security_cancel`。
- 数据边界：不删除 NimBLE bond、不清空 NVS、不改写 `factory / 0x10000`。
- 验证：ESP-IDF 5.5.5 编译通过；应用镜像在 2 MiB 分区中剩余 53%；网页 Vitest 28/28 通过；Vite 生产构建通过；开发板静态合同检查 20 PASS、2 WARN、0 FAIL。两个 WARN 为既有 GPIO8 极性/上电时序证据缺口，与本次 BLE 时序改动无关。
- 真机日志顺序：`connect status=0` → `stored_bond=1 / trusted=0` → `security_deferred` → `subscribe notify=1` → `security_schedule delay_ms=300` → `security_initiate rc=0` → `subscribe notify=0` → `disconnect reason=531`。
- 结论：延时动作确实执行，但 Windows 仍在 `ble_gap_security_initiate()` 后约 70 ms 主动终止连接；没有出现 `ENC_CHANGE` 或 `REPEAT_PAIRING`。所以问题不是“订阅回调里启动安全握手太早”，继续调整延时没有证据支持。

## Windows 主导配对诊断版

- 状态：已编译、归档、写入目标板并完成两次 BLE 白盒复现；**Windows 主导配对假设已被实测排除**。
- 文件：`easyinput_beatbox_host_driven_pairing_diag.bin`
- 文件大小：980032 bytes
- SHA-256：`6CBC7D0D95BDD849D90EF56F236BCBC055526D738B773B19742E211987FDD5BA`
- 镜像校验：esptool 识别为 ESP32-S3；checksum `96` 与 validation hash `faf74589879cae4fb75d770b19b968a1ecd5bfa096abe05c422a5f2f3d03548f` 均有效。
- 计划写入地址：`0x430000`（只更新 Beatbox OTA 分区）。
- 网页行为：通知订阅完成后，第一条协议 `ping` 改用对加密 RX 特征的 `writeValueWithResponse`。该 ATT 写请求应由 Windows / Web Bluetooth 触发加密或配对；网页不再等待固件先发送通知。
- 固件行为：不再调用 `ble_gap_security_initiate()`，连接与订阅时只记录 `security_wait_host`，等待电脑通过加密写请求启动 SMP。
- 旧 bond 处理：仅在用户按住 S7 打开的物理配对窗口内，若 NimBLE 收到 `REPEAT_PAIRING`，只删除**当前连接电脑**的旧 bond 并返回 `BLE_GAP_REPEAT_PAIRING_RETRY`；其他电脑的 bond 不动，窗口外不替换。
- 数据边界：不清空 NVS、不整片擦除、不改写 `factory / 0x10000`，也未修改 Windows 的配对记录。
- 验证：ESP-IDF 5.5.5 编译通过，应用镜像在 2 MiB 分区中剩余 53%；网页 Vitest 28/28 通过；Vite 生产构建通过；开发板静态合同检查 20 PASS、2 WARN、0 FAIL。两个 WARN 仍为既有 GPIO8 极性/上电时序证据缺口。
- 白盒结果：两次都完成选择器、GATT 连接、Service/Characteristic 发现和 Notify 订阅；随后 Chrome 对加密 RX 的 `writeValueWithResponse` 在 12 秒内连续 28 次返回 `NetworkError: GATT Error: Not paired.`。固件侧未见 `REPEAT_PAIRING` 或 `ENC_CHANGE`，最后仅见电脑主动断开（reason 531 / HCI 0x13）。因此当前 Windows Web Bluetooth 路径没有把这个加密 GATT 写自动升级成 OS 配对，继续改 SMP 时序没有证据支持。
- 烧录记录：写入前与写入后的 fresh esptool 读取均确认芯片与用户授权的目标身份一致；公开资料不记录设备唯一标识。只写入 `0x430000`，实际擦写范围为 `0x00430000–0x0051FFFF`；esptool 报告数据哈希校验通过。未清 NVS、未改写 factory、未改写 OTA 数据。写后设备按计划保持在 bootloader，等待一次“关机 → 开机”恢复正常启动。
- 重新上电验收：未再调用 esptool；通过普通 USB 应用协议收到 Beatbox protocol v3、`ble_direct` 能力、`bpm=120 run=0 variation=0 mode=0` 状态以及 Pattern bank 0/1/2。随后诊断监听收到 `sync identity_rc=0 bond_count=1 adv_rc=0`，证明新版应用、NimBLE 身份、既有 bond 读取与 BLE 广播均已启动。

## 浏览器凭据 + HMAC 挑战诊断版

- 状态：已按用户确认写入目标开发板；2026-10-01 重新上电后的 USB 应用协议、BLE 广播、浏览器首次登记、已登记浏览器 HMAC 再连接和协议握手通过。完整实体按键联动、自动重连等仍待真机验证；固定备份见 [v1.0.0 浏览器钥匙包](../release-v1.0.0-browser-hmac/README.md)。
- 文件：`easyinput_beatbox_browser_hmac_auth_diag.bin`
- 文件大小：985264 bytes
- SHA-256：`DEA3720FBE7E20B45EAED2784BEA83CC235F8046DD11EC80223AFB1B1FE784F8`
- 镜像校验：esptool 识别为 ESP32-S3；checksum `e6` 与 validation hash `0c8ac8ff0a92e34801509d00e309fa6fe59cdec2532b51992ce2d55b8d5c2c6b` 均有效。
- 计划写入地址：`0x430000`（只更新 Beatbox OTA 分区）。
- 认证流程：设备为每次连接生成 16 字节随机 challenge；已登记浏览器用本地 32 字节 key 返回 HMAC-SHA256 proof。新浏览器只能在 S7 的 60 秒实体窗口内登记。
- 存储边界：固件在既有 `beatbox_ble` NVS 命名空间中新增 `client0..client2` 和 `client_next`；不清 NVS，不删除旧 NimBLE bond，不改写 `factory / 0x10000` 或 OTA 数据。
- 网页行为：凭据只在收到 `ble_auth_ok` 后保存；普通 `ping` 只在应用授权成功后发送。当前 Vitest 27/27 通过，Vite 生产构建通过。
- 固件构建：ESP-IDF 5.5.5 完整编译/链接通过；应用镜像 `0xf08b0` bytes，2 MiB 分区剩余 `0x10f750` bytes（53%）。
- 安全边界：新 challenge 可防止直接重放旧 proof，但登记 key 与普通 Beatbox 流量未加密；本诊断版不等同于 BLE 链路加密，不抵御登记窗口内的无线窃听或中间人。
- 烧录记录：写入前与最终物理恢复前均通过 esptool 核对 ESP32-S3 芯片与用户授权的目标身份；公开资料不记录设备唯一标识。只写入 `0x430000`，实际擦写范围 `0x00430000–0x00520FFF`，985264 bytes 写入后数据哈希校验通过。不清 NVS、不删除旧 bond、不覆盖 factory、不改写 OTA 数据。
- 重新上电应用检查（2026-10-01）：未再调用 esptool；`scripts/verify_beatbox_serial.py --port COM3 --timeout 12` 返回 PASS，收到协议 v3、`ble_direct` 能力、`bpm=120 run=0 variation=0 mode=0` 与 Pattern bank 0/1/2。
- 新版 BLE 启动证据：串口监听收到 `sync identity_rc=0 addr_type=1 legacy_bond_count_rc=0 legacy_bond_count=1 client_count=0 adv_rc=0`，说明新版浏览器凭据固件已运行，旧 bond 保留，当前尚无浏览器登记，BLE 广播成功启动。
- 网页检查：`http://127.0.0.1:5174/?ble-diag=1&build=browser-hmac` 和 `/src/link.ts` 均返回 HTTP 200，实际服务的模块包含 HMAC、浏览器登记和挑战响应流程。查询参数只用于标记本轮页面；不作为固件或源码身份的证明。
- 首次 BLE 登记真机证据（2026-10-01 09:24，Asia/Shanghai）：串口依次记录 `pairing_window open` → `connect status=0 pairing_open=1 client_count=0` → `app_auth_wait` → `subscribe notify=1` → `app_auth_challenge sent=1` → `app_auth_success mode=enrolled client_count=1`。网页同时记录 `gatt-connect:ok`、`app-auth:credential-saved`、`app-auth:ok mode=enrolled`、`device-authorization:ok`、`protocol:device-ready`、`protocol-ping:ok`。首次连接和凭据登记已通过；此时未验证已登记浏览器的 HMAC 重连，也未验证每个节奏字段和实体键的网页同步。
- 已登记浏览器再次连接（2026-10-01 09:33）：网页记录 `app-auth:challenge-response` → `app-auth:ok mode=known` → 协议 v3 → Pattern bank 0/1/2 → `sync=synced` → `protocol-ping:ok`。运行/停止状态也进入网页状态模型；这是手动再次选择已登记设备的验证，不等于自动重连、断电后信任保持或完整实体键反馈全部通过。
- USB 备用恢复：串口监听及其他网页会独占同一端口。释放占用后用户确认可用；不能将 `Failed to open serial port` 直接解释为固件损坏。测试网页 USB 时不同时开启串口监听。
- 继续验证：诊断采集支持 `--protocol`，可同时观察 `hello/state/pattern/key`；诊断网页仅在 `ble-diag=1` 时发出 `protocol:state`、`protocol:key`、`protocol:hello` 和 `protocol:pattern`。完整实体键、自动重连等保持未完成。

## 诊断范围

该固件只增加通过 USB 主机链路输出的 `ble_diag` JSON 事件，用来观察：

- BLE 连接与断开原因；
- 已保存 bond 是否存在；
- 安全配对发起与加密结果；
- repeat-pairing 事件及当时固件采取的 `ignore` 或“仅当前 peer 重试”动作；
- S7 配对窗口状态；
- NimBLE 同步、复位和广播结果。

本诊断版不删除 bond、不清空 NVS，也不改变现有配对决策。只有完成烧录并在真机上复现后，才能据日志确定根因。

## 烧录记录

- 目标：经过用户确认并在恢复正常启动前完成身份复核的 ESP32-S3；端口和 MAC 不作为公开发布材料保存。
- 应用镜像只写入 `0x430000`，esptool 报告写入数据哈希校验通过。
- OTA 数据只写入 `0x630000`；写后复读的两个冗余记录均为 `OTA_SEQ=0x00000001`、`CRC=0x4743989a`，对应唯一的 `beatbox / ota_0`。
- 未改写 `factory / 0x10000`，未擦除 NVS 或整片 Flash。
- 自动复位后的首次协议检查未收到串口帧；这不证明应用失败，也不能证明仍在下载模式。需按当前实板合同执行一次“关机 → 开机”后继续验证。
- 用户完成“关机 → 开机”后，USB 实机检查通过：收到协议 v3、`ble_direct` 能力、状态以及 Pattern 0/1/2。
- BLE 白盒监听启动时收到 `sync`：`identity_rc=0`、`bond_count=1`、`adv_rc=0`，说明 NimBLE 身份同步、现有一条 bond 读取和广播启动均成功。

## 构建说明

构建使用当前工作区中的诊断源文件。由于 ESP-IDF 工具链在未使用的 `esp_lcd` 组件上发生 GCC 内部崩溃，临时构建副本限定为 `main` 及其声明依赖；工作区顶层 CMake 配置未因此修改。Beatbox 主程序及其全部声明依赖编译和链接成功，应用分区剩余 53%。
