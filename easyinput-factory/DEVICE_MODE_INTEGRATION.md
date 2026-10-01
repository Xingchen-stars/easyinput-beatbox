# EasyInput / Beatbox 双模式集成说明

## 来源与边界

`easyinput-factory` 是从现有 EasyInput 工作树复制出的集成快照，用来承载双模式改动。原始工作树未被本次集成修改；其中已有的 Host Action 等未提交内容也没有被清理或覆盖。

EasyInput 保持原来的 USB HID、BLE HID、按键、编码器、灯光、麦克风、扬声器、NVS 和声音资源布局。Beatbox 作为第二个应用分区加入，而不是把 EasyInput 功能重写到鼓机固件里。

## 交互规则

| 操作 | 结果 |
| --- | --- |
| 旋钮短按 | 当前模式原有短按行为 |
| 按住 3–5 秒后松开 | EasyInput 原有 Windows / macOS 平台选择 |
| 持续按住达到 5 秒 | 进入顶层设备模式选择 |
| 选择中旋转 | 在“鼓机模式 / EasyInput 键盘模式”之间循环并播放对应语音 |
| 选择中短按 | 确认目标；校验目标镜像后设置启动分区并重启 |
| 10 秒无操作或按其他键 | 取消选择，留在当前模式 |

两个模式都实现了相同的顶层选择入口，所以可以从 EasyInput 切到 Beatbox，也能从 Beatbox 切回 EasyInput。设备模式被独立封装为枚举、选择状态机和启动分区适配层；后续增加更多模式时，可以扩充这些入口，而不用改动普通按键/HID或鼓机节拍逻辑。

## Flash 布局

| 分区 | 地址 | 大小 | 用途 |
| --- | ---: | ---: | --- |
| `nvs` | `0x9000` | `0x6000` | EasyInput 配置 |
| `phy_init` | `0xf000` | `0x1000` | 射频校准数据 |
| `factory` | `0x10000` | `0x300000` | EasyInput 键盘模式 |
| `sound_a` | `0x310000` | `0x90000` | EasyInput 声音资源 A |
| `sound_b` | `0x3a0000` | `0x90000` | EasyInput 声音资源 B |
| `beatbox` | `0x430000` | `0x200000` | 鼓机模式 |
| `otadata` | `0x630000` | `0x2000` | 当前启动模式 |

原 EasyInput 的 NVS、factory 和两块声音资源地址保持不变。新增空间位于 16 MB Flash 的后段。

## 语音与硬件安全

顶层模式名称使用两套现有音频链路分别编码：Beatbox 使用 32 kHz PCM，EasyInput 使用 48 kHz EIAD/IMA-ADPCM。EasyInput 侧通过原有 `SpeakerOutput`、音频仲裁器和 GPIO8 电源交接播放，没有绕开麦克风与扬声器共享电源的生命周期。

新增语音：

- “鼓机模式”：约 836 ms；
- “Easy Input 键盘模式”：约 1,775 ms。

## 当前验证结果

- 设备模式选择纯逻辑宿主测试通过；
- 原编码器长按手势回归测试通过；
- Beatbox 设备模式和速度模式宿主测试通过；
- EasyInput 与 Beatbox 均使用 ESP-IDF 5.5.5 完整构建成功；
- 两个镜像均小于对应应用分区；
- 双模式组合包已生成在 `artifacts/dual-mode-2026-09-04`。

尚未执行实机烧录和硬件在环验证。USB/BLE 枚举、语音、电源共享、两方向重启切换，以及键盘/鼓机的完整回归仍需在用户确认烧录后逐项观察。
