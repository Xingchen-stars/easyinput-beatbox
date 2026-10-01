# EasyInput Beatbox 产品合同

硬件事实以 `easyinput-board-cy` 为准；本文件描述产品层行为。

## 音色目标

参考录音室 click / clave / 木块实践：

- 干声、短瞬态、锐利起音（当前 click 约 9 ms）
- 主体约 2.0–2.5 kHz clave 感，而不是长鸣正弦或刺耳方波
- 削去过高频能量，降低长时间听感疲劳
- 重拍与普通拍以音高/音色区分，音量差保持克制
- 鼓机默认音色：public-domain TR-707 Kick/Snare/CHH/OHH/Clap，离线转换为 32 kHz/16-bit/mono PCM 并嵌入固件

板端合成实现见 `firmware/main/audio/audio_click.c`。

## 时钟与音频链路

- I2S `32000 Hz` 采样时钟是唯一节拍时间源；UI、USB 与灯光不得驱动声音触发
- 连续渲染 `128 frame`（4 ms）音频块，不在每拍临时启动/停止 DMA
- 内部调度分辨率 **96 PPQN**（由采样游标 Q32 相位累计派生）
- 对外 MIDI Clock 为 **24 PPQN**（每 4 个内部 tick 导出一发）
- 16 步网格：每步 = 24 tick；Swing 仅延迟奇数 16 分音符
- 外部音符进入队列；预分配 voice 独立播放并在 `int32` 累加器中混音
- 音频线程不分配内存、不等待 UI/USB 锁，也不执行灯带更新
- `scripts/check_timing_model.py` 必须覆盖 60–240 BPM 与 Swing；允许误差不超过 1 sample

## 交互目标

### 整机模式

- 当前顶层模式为「鼓机模式」与「EasyInput 键盘模式」；选择器使用模式表，后续可以继续增加模式
- 鼓机模式下，旋钮长按满 5 秒进入整机模式选择；进入时播报当前模式，旋转时播报候选模式，短按确认
- EasyInput 键盘模式保留原来的平台选择：按住 3～5 秒后松开进入 Windows/macOS 平台选择；继续按满 5 秒则进入整机模式选择
- 进入整机模式选择的那次长按松开会被吞掉，不得同时触发播放、BPM 选择或 EasyInput 普通旋钮动作
- 10 秒无操作退出整机模式选择，当前模式保持不变
- 确认另一个模式后，先释放当前输入与音频资源，再选择目标应用分区并重启；不是重新烧录
- EasyInput 原配置继续保存在 `nvs` 的 `ai_keyboard` 命名空间；整机切换只写 `otadata`，不得改写按键配置

### 双应用分区

| 分区 | 用途 |
| --- | --- |
| `factory` | 完整 EasyInput 键盘固件（USB HID + BLE HID） |
| `beatbox` / `ota_0` | 完整鼓机固件（USB Serial/JTAG + Web Serial） |
| `otadata` | 记录下一次启动哪个完整应用 |

分区表保留原 EasyInput 的 `nvs / phy_init / factory / sound_a / sound_b` 起始地址，只在未使用的 Flash 区域追加 `beatbox` 与 `otadata`。目标镜像不存在或校验失败时，不更新启动选择，也不重启。

### 节拍器

- 板端可独立使用：旋钮 BPM + 编码器/S8 启停
- 编码器双击进入三档速度选择；旋转时板载语音预览「慢速练习 90 / 原版 120 / 快速节奏 140」，短按确认，5 秒无操作取消
- 选择模式只改变候选值；确认后才更新板端 BPM，并通过常规 `state` 同步网页
- 为识别双击，编码器单击启停最多等待 350 ms；S8 启停保持立即响应
- 上电默认为「仅节拍器」模式：不运行鼓序列，只播放 click
- click 开关关闭后节拍器模式静音；鼓机模式下该开关只控制是否叠加 click
- 视觉可预期下一拍（灯来回走；电脑端拍点条）
- 电脑端连接后自动识别设备，显示 BPM、运行态、拍号
- 键盘：空格启停，方向键微调 BPM

### 鼓机（P2）

| 丝印 | 功能 |
| --- | --- |
| S1 | Closed HH（GM 42） |
| S2 | Open HH（GM 46） |
| S3 | Clap（GM 39） |
| S4 | Rimshot（GM 37） |
| S5 | Kick（GM 36） |
| S6 | Snare（GM 38） |
| S7 | 短按 A/B；播放中长按 Fill；停止时按住 3 秒开放新浏览器登记 |
| S8 / 编码器按压 | Play / Stop |

4×2 排布遵循常见 finger-drumming / MPC 习惯：底排放 Kick·Snare 根基，上排放镲片与打击乐。

- 默认 4/4、16 步、6 轨（Kick / Snare / CHH / OHH / Clap / Rim）
- Swing 50–75%（主机滑块；编码器按住旋转后续可接）
- Pattern revision 双向同步；冲突时设备权威
- 主机右栏为 16 步编辑器 + Live Pad，不是 8 个鼓音色假 Pad
- **网页 REC 叠录**：只在配套 UI；写入当前编辑的 A 或 B，边播边录、不先清空；录音中锁定板端 S7/S8/编码器启停，避免误触
- Pattern JSON 导入/导出（含 A/B/Fill hex）

## 连接

- **当前主通道**：BLE GATT + Web Bluetooth + 应用层 HMAC 挑战认证（`host-protocol.md`）
- **备用通道**：USB Serial + Web Serial，用于恢复、排错与不支持 Web Bluetooth 的环境
- **新浏览器授权**：只在停止播放且 S7 按住 3 秒后开放 60 秒窗口；无需配网码或 OS 配对码；最多三份 Beatbox 浏览器凭据
- **记录隔离**：Beatbox 的 `client0..client2` 应用凭据不删除、不改写 EasyInput 键盘模式的 BLE HID bond
- **安全边界**：新鲜随机 challenge 可防止直接重放旧 proof，但本版未对普通 BLE 控制流量提供链路层或应用层加密
- **语义合同**：MIDI 运输 / GM Ch.10（`midi-protocol.md`）
- **适配层**：领域事件 ↔ MIDI 字节（`app/src/midi-adapter.ts`）
- 选择原因：BLE 不占用电脑 Wi-Fi，也不要求日常插线；USB Serial 仍可稳定烧录和救援
- USB MIDI class 可行性见 `usb-midi-spike.md`；未过门槛前不替换 Serial

## 阶段

| 阶段 | 状态目标 |
| --- | --- |
| P1 | 独立节拍器 + 主机链路 + 配套 UI |
| P2 | 八键鼓机 / 16 步 / Swing / A/B / Fill，复用通道 10 语义 |
| P3 | UI 深度编辑 / Kit；可选 USB MIDI class 或桌面桥接 |
