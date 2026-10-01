# EasyInput Beatbox 主机协议

## 结论

对这套板子，**当前日常主链路是 BLE GATT + Web Bluetooth**，USB-Serial/JTAG + Web Serial 保留为首次烧录、排错与应急连接。
消息是**领域命令/事件**；它们可以映射到 MIDI 语义（见 `midi-protocol.md`），但 JSON 本身不是 MIDI 字节流。

为什么不先上 USB MIDI class device：

- ESP32-S3 的 USB-OTG 与 USB-Serial/JTAG **共用一根 PHY**
- macOS 上 ROM 会先枚举成 JTAG/Serial；TinyUSB 再抢口时经常失败或需拔插/烧 eFuse
- EasyInput 日常烧录也依赖该串口；先保证配套 UI 稳定自动连接更重要

## 时钟合同

| 域 | 分辨率 | 说明 |
| --- | --- | --- |
| 板端内部 | **96 PPQN** | 音频采样时钟派生；唯一实时主时钟 |
| 对外 MIDI Clock | **24 PPQN** | 1 MIDI Clock = 4 内部 tick |
| 16 分音符 | 24 内部 tick | 1 MIDI Beat (SPP) = 6 MIDI Clock = 24 tick |
| 四分音符 | 96 内部 tick | 节拍器 accent 周期 |

板端永远是音频主时钟。主机不得驱动发声时基。

## 传输与帧格式

应用层统一使用 UTF-8、一条 JSON 一帧。USB 以 `\n` 结尾；BLE 因 MTU 较小，在每个 GATT 包前加 2 字节分片头：`flags`、`sequence`。`flags` 的 bit0 表示首片，bit1 表示末片。BLE 重组后交给与 USB 相同的 JSON 处理器。

协议版本：`hello.v = 3`。v1/v2 客户端仍可读 `state` / `beat` / `start` / `stop`。

BLE GATT UUID：

| 用途 | UUID |
| --- | --- |
| Service | `8ab6c845-23e4-4f36-91df-8ac820b58101` |
| Host → Device（Write） | `8ab6c845-23e4-4f36-91df-8ac820b58102` |
| Device → Host（Notify） | `8ab6c845-23e4-4f36-91df-8ac820b58103` |

### BLE 浏览器授权（应用层）

BLE 连接、发现特征并订阅 Notify 后，设备先发送随机 challenge。未授权时，RX 只接受下面的登记/认证消息，普通 Beatbox 命令不进入主机协议队列。

| 方向 | 消息 | 含义 |
| --- | --- | --- |
| Device → Host | `{"t":"ble_auth_challenge","nonce":"<32 hex>","pairing":0,"v":1}` | 16 字节新鲜随机数；`pairing=1` 表示 S7 登记窗口已开 |
| Host → Device | `{"t":"ble_auth","id":"<16 hex>","proof":"<64 hex>"}` | 已登记浏览器发送 `HMAC-SHA256(key, raw_nonce)` |
| Host → Device | `{"t":"ble_enroll","id":"<16 hex>","key":"<64 hex>"}` | 只在 S7 窗口内登记新浏览器 |
| Device → Host | `{"t":"ble_auth_ok","mode":"known|enrolled","v":1}` | 认证/登记成功，随后才允许 `ping` 和其他命令 |
| Device → Host | `{"t":"ble_auth_error","code":"...","pairing":0}` | 未知 client、proof 不匹配、challenge 无效或登记窗口未开 |

浏览器使用 Web Crypto 生成 8 字节 client id 和 32 字节 key，并按 `BluetoothDevice.id` 保存在当前网站的 `localStorage`。设备在 NVS `beatbox_ble` 命名空间保存最多三份 `client0..client2`，超出后轮转替换。每次连接使用新 challenge，不在日常重连时重发 key。

这是用实体 S7 限制新浏览器登记的应用层访问控制，不依赖 Windows `PairAsync` / BLE bonding。它可防止旧 proof 被直接重放，但本版的登记 key 与普通协议流量未在应用层加密；不能把这个认证机制表述为链路加密。

### Device → Host

| 消息 | 含义 |
| --- | --- |
| `{"t":"hello","v":3,"name":"EasyInput Beatbox","caps":["drum","pattern","swing","volume","record","ble_direct"]}` | 身份与能力 |
| `{"t":"state","bpm":120,"run":0,"beat":0,"step":0,"bar":0,"tick":0,"swing":50,"var":0,"fill":0,"rev":1,"click":1,"mode":0,"vol":100}` | 完整状态快照（约 2 Hz + 事件） |
| `{"t":"position","bar":0,"step":0,"beat":0,"tick":0,"accent":1}` | 步进位置（播放中） |
| `{"t":"beat","accent":1,"beat":0,"step":0}` | 四分拍点（v1 兼容；含 step0–15） |
| `{"t":"note","n":36,"v":127}` | 实时打击（Live Pad / 序列触发回显） |
| `{"t":"key","i":0,"v":1}` | 物理键反馈：`i`=0..7 对应 S1..S8，`v`=按下/抬起 |
| `{"t":"pattern","bank":0,"rev":1,"p":"<192 hex>"}` | 单 bank Pattern（96 字节 velocity 的 hex）；全量同步时连发 bank 0/1/2 三行，避免长行截断 |
| `{"t":"start"}` / `{"t":"continue"}` / `{"t":"stop"}` | 运输变化 |
| `{"t":"ack","cmd":"pattern_set","ok":1,"rev":2}` | 命令确认 |
| `{"t":"error","cmd":"...","msg":"..."}` | 命令失败 |
| `{"t":"ble_diag","event":"...",...}` | USB-only 白盒诊断帧；记录 BLE 连接、订阅、应用授权、旧 SMP 事件与断开原因，不通过 BLE 回传、不主动改写旧 bond |

字段约定：

- `beat`：小节内四分拍 `0..3`
- `step`：16 步网格位置 `0..15`
- `tick`：小节内 96 PPQN 位置 `0..383`（4 拍 × 96）
- `swing`：`50..75`（百分数；50 = 直拍）
- `var`：`0=A` / `1=B`
- `fill`：`0/1` Fill 按住态
- `rev`：Pattern 修订号，仅用于显示与同步排序；设备采用最后写入者生效，不向用户暴露冲突
- `mode`：鼓机层开关，`0` 关 / `1` 开；上电默认 `0`（可与节拍器同时开）
- `vol`：总音量 `0..127`（默认 `100`）
- `click`：节拍器 click 层开关，与鼓机层独立；上电默认 `1`
- `p` / `a` / `b` / `f`：`6 × 16 = 96` 字节，按轨串联（Kick / Snare / CHH / OHH / Clap / Rim），每字节 velocity `0`=关、`1..127`=开

### Host → Device

| 消息 | 含义 |
| --- | --- |
| `{"t":"start"}` | 从 step 0 / tick 0 开始 |
| `{"t":"continue"}` | 从停止位置继续 |
| `{"t":"stop"}` | 停止并保留位置 |
| `{"t":"bpm","v":128}` | 设置 BPM（60–240） |
| `{"t":"swing","v":66}` | 设置 Swing（50–75） |
| `{"t":"variation","v":0}` | 选择 A/B（量化到下一 16 分边界生效） |
| `{"t":"fill","v":1}` | 进入/退出 Fill 覆盖 |
| `{"t":"note","n":36,"v":127}` | Live Pad 触发 |
| `{"t":"click","v":1}` | 启用/关闭节拍器 click 层 |
| `{"t":"mode","v":0}` | 启用/关闭鼓机 Pattern 层 |
| `{"t":"volume","v":100}` | 设置总音量（0–127） |
| `{"t":"pattern_get"}` | 请求 `pattern_dump` |
| `{"t":"pattern_set","bank":0,"rev":1,"p":"<192 hex>"}` | 写入单 bank；设备采用最后写入者生效 |
| `{"t":"save"}` | 请求将 Pattern 标记为已保存（MVP：bump rev + ack；NVS 后续） |
| `{"t":"record","v":1}` | 主机叠录武装：`1` 时锁定 S7 / S8 / 编码器启停；`0` 解除 |
| `{"t":"ping"}` | 请求 `hello` + 紧随 `state` + `pattern_dump` |

## 运输语义

| 命令 | 行为 |
| --- | --- |
| Start | 位置归零，下一音频块起跑 |
| Stop | 停止发声调度，保留 `bar/step/tick` |
| Continue | 从保留位置继续（不归零） |

## 配套 UI

`app/` 优先使用 Web Bluetooth：

1. 停止播放后按住开发板 S7 三秒，蓝灯亮起，开放 60 秒新浏览器登记窗口；
2. 网页点击「蓝牙连接」，在浏览器选择 `EasyInput Beatbox`；不再输入设备配网码；
3. 网页生成随机浏览器凭据，并只在 S7 窗口内写入 Beatbox 的三槽应用信任表；窗口外拒绝未登记浏览器；
4. 浏览器已授权过的设备可通过 `navigator.bluetooth.getDevices()` 尝试自动重连；浏览器的首次设备选择仍必须由用户点击触发；
5. USB 备用按钮继续使用 Espressif VID `0x303A`，便于恢复和排错；
6. 仅在收到合法 `hello` + `state` 后进入 `synced`；Pattern 编辑使用本地 draft + `rev` 提交。

Beatbox 的应用级浏览器凭据与 EasyInput 键盘模式的 BLE HID 配对记录分开管理；Beatbox 不主动擦除或替换共享 NimBLE bond 数据。

## 与 MIDI 的对应

见 `midi-protocol.md`。摘要：

| 领域事件 | 标准 MIDI |
| --- | --- |
| start / continue / stop | `0xFA` / `0xFB` / `0xFC` |
| 内部 tick ÷ 4 | `0xF8` Clock（24 PPQN） |
| step 位置 | Song Position Pointer（1 MIDI Beat = 1/16 音符） |
| note n/v | Ch.10 Note On/Off |
| bpm / pattern / swing / rev | **设备管理命令**（Serial JSON）；不是通用 MIDI CC |

不要再声称 CC16/17 是标准 BPM。
