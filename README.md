# EasyInput Beatbox

基于 EasyInput V2.0 与 ESP32-S3 制作的独立节拍器 / 鼓机：板端负责稳定走拍、实时发声和现场演奏，电脑端负责可视化、Pattern 编辑、叠录与同步。

> 板子守着拍子和声音。电脑只负责看见、改谱、同步。

本项目建立在 [CY-CHENYUE/easyinput-board-cy](https://github.com/CY-CHENYUE/easyinput-board-cy) 开发板项目之上。该仓库提供 EasyInput V2.0 的硬件资料、板级安全边界和 AI 开发上下文；本仓库在这套硬件基础上实现 Beatbox 的固件、音频引擎、音序器、网页应用与课程资料。

本仓库是 [Zhaohan-Wang/easyinput-beatbox](https://github.com/Zhaohan-Wang/easyinput-beatbox) 的个人课程改造版。固定版本 **v1.0.0 / 浏览器钥匙版 / 2026-10-01** 保存了已经烧录并完成首次 BLE 登记、HMAC 再连接和 USB 备用恢复的这一版。下载入口见 [GitHub Releases](https://github.com/Xingchen-stars/easyinput-beatbox/releases)，完整范围见 [版本说明](docs/releases/v1.0.0.md)，恢复操作见 [烧录与恢复指南](docs/flashing-and-recovery.md)。

发布资产包含已验证的 Beatbox 固件、网页静态包、源码 ZIP 和 Git 历史备份；固件同时保存在 Git 标签下的 [固定更新包](artifacts/release-v1.0.0-browser-hmac/README.md)。这次按可恢复备份版本发布，完整按键回归、自动重连和长时间运行仍保留验收清单，不把它们写成已经全部验证。

在线网页：[Beatbox 网页版](https://xingchen-stars.github.io/easyinput-beatbox/)。首次在这个新网站连接，要按 S7 重新登记；本地网站的浏览器钥匙不会自动转移过去。

恢复源码请优先使用 **`v1.0.0-backup.1`** 标签或 Release 中带 `backup1` 的源码 ZIP / Git bundle。第一次离线恢复检查发现 `v1.0.0` 的 Git 源码备份漏掉了一份被忽略的发布依赖锁，因此增加此恢复资料修正标签；原 `v1.0.0` 和旧附件保留不覆盖，固件与网页程序没有修改。已发布的固件 ZIP 从一开始就包含这份锁文件。详见 [备份修正说明](docs/releases/v1.0.0-backup1.md)。

![EasyInput Beatbox 网页控制界面](docs/course/figures/png/07-beatbox-web-app.png)

## 项目是什么

EasyInput Beatbox 不是一个把按键事件发送给电脑后才发声的控制器，也不是浏览器里的节拍器网页。拔掉电脑后，开发板仍能独立完成节拍调度、声音播放、灯光反馈和 Live Pad 演奏；连接电脑后，网页或桌面应用会成为它的可视化编辑器。

| 阶段 | 目标 | 可验收的结果 |
| --- | --- | --- |
| P0 | 安全上电与第一声 click | RGB 正常点亮，扬声器播放短 click |
| P1 | 独立节拍器 | 编码器调 BPM，按键启停，灯光与声音同步 |
| P2 | 鼓机与音序器 | 六种鼓音、八键演奏、16 步、A/B、Fill、Swing |
| P3 | 电脑联动 | Pattern 编辑、状态同步、网页 REC 叠录与 MIDI 语义映射 |

核心原则是先建立可信的音频时间轴，再在同一时间轴上增加 click、Pattern、Live Pad、灯光和主机同步。

## 当前能力

- 双顶层模式：旋钮持续按住 5 秒，可在“鼓机模式”和“EasyInput 键盘模式”之间有声选择；确认后重启进入所选固件。
- 60–240 BPM 独立节拍器，默认 120 BPM；编码器和 S8 均可控制播放。
- 三档有声速度预设：慢速练习 90、原版 120、快速节奏 140 BPM。
- Click 与 Drum Pattern 是两个独立播放层，可以同时启用；Live Pad 不依赖 Pattern 开关。
- I2S `32 kHz` 采样时钟驱动内部 `96 PPQN` 时间轴，不用普通 `delay()` 推进节拍。
- 六种鼓音：Kick、Snare、Closed Hi-hat、Open Hi-hat、Clap、Rimshot。
- 4×2 实体键位、六轨 × 16 步 Pattern、A/B Variation、Fill 与 50%–75% Swing。
- 多 voice 混音；Live Pad 不会因为触发新音符而粗暴截断前一个声音。
- 网页端节拍器面板、实体键位映射、16 步编辑器、Pattern JSON 导入与导出。
- 网页 REC 叠录：边播边打，把演奏量化写入当前 A/B Pattern，而不是录制麦克风音频。
- BLE 双向同步、浏览器本地凭据认证、已授权设备自动重连、Pattern revision 冲突处理；USB Serial 保留为备用与恢复通道。
- MIDI Clock、运输控制和 GM Channel 10 语义映射；当前传输层仍是易调试的 JSON Lines。

## 系统架构

![EasyInput Beatbox 系统架构](docs/course/figures/png/03-board-host-architecture.png)

节拍与声音对实时性要求最高，因此由开发板独立完成。网页刷新、USB 收包或 LED 更新不能改变声音的触发时刻。

- **板端**：读取按键与编码器，维护 Start / Stop / Continue，运行 I2S 音频时钟、Pattern、Live Pad 与多 voice 混音。
- **电脑端**：展示状态、编辑 Pattern、控制录音武装、导入导出数据。
- **LED 与 BLE/USB**：消费时间轴事件用于显示和同步，不反过来驱动音频时序。

当前使用 **BLE GATT + Web Bluetooth** 作为日常主通道，USB Serial + Web Serial 作为备用。蓝牙连接后，浏览器先用本地保存的随机密钥回答开发板的一次性 HMAC-SHA256 挑战，通过后才开放 Beatbox 命令。两个传输共用同一套 JSON 主机协议；消息表达的是音符、运输、BPM、Pattern 和位置等领域语义，可以映射为 MIDI，但 JSON 消息本身并不是 MIDI 字节。

## 硬件交互

| 操作 | 行为 |
| --- | --- |
| 持续按住编码器 5 秒 | 进入顶层设备模式选择；旋转预选并播报，短按确认 |
| 旋转编码器 | 正常模式调整 BPM，范围 60–240 |
| 短按编码器或 S8 | Play / Stop（旋钮短按会等待双击判断；S8 立即响应） |
| 双击编码器 | 进入三档速度选择模式 |
| 选择模式中旋转编码器 | 循环预选 90 / 120 / 140 BPM，并播报档位名称 |
| 选择模式中短按编码器 | 确认预选档位；5 秒无操作则取消 |
| S1 | Closed Hi-hat（GM 42） |
| S2 | Open Hi-hat（GM 46） |
| S3 | Clap（GM 39） |
| S4 | Rimshot（GM 37） |
| S5 | Kick（GM 36） |
| S6 | Snare（GM 38） |
| S7 短按 | 切换 Pattern A / B |
| 停止时 S7 按住 3 秒 | 开放 60 秒新浏览器登记窗口（蓝灯常亮） |
| 播放时 S7 长按 | 按住进入 Fill，松开返回 |

五颗 RGB LED 用来表示拍点、重拍和运行状态。麦克风目前不进入实时音频链路。

## 快速开始

### 需要准备

- [EasyInput V2.0 开发板](https://github.com/CY-CHENYUE/easyinput-board-cy)；
- 支持数据传输的 USB-C 线；
- ESP-IDF 5.5.5，目标芯片为 `esp32s3`；
- Node.js 与 pnpm；
- Chrome 或 Edge（使用网页版本时）。

### 构建与烧录固件

```bash
cd firmware
. ~/esp/v5.5.5/esp-idf/export.sh
idf.py set-target esp32s3
idf.py -D COMPONENTS=main build
```

这个项目保留了两套完整应用：EasyInput 键盘模式在 `factory / 0x10000`，Beatbox 在 `beatbox / ota_0 / 0x430000`。因此双模式开发板上**不要直接执行 `idf.py flash`**，它会按单工程默认地址把 Beatbox 写到 `0x10000`，覆盖键盘模式。

开发板保持开机，短按一次 BOOT 并松开；确认下载端口和芯片身份后，只更新 Beatbox 分区：

```bash
python -m esptool --chip esp32s3 --port <PORT> --baud 460800 \
  --before no_reset --after no_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x430000 build/easyinput_beatbox.bin
```

`COMPONENTS=main` 与本版成功构建时的参数相同，仍会包含主程序声明的全部依赖。它避免编译未使用的 ESP-IDF 组件；没有改写运行时功能。重新构建的镜像因时间戳或工具链差异可能不与固定发布镜像逐字节相同，恢复这一版时优先使用发布包。

如果当前启动选择不在 Beatbox 槽位，再使用 ESP-IDF 5.5.5 的 `otatool.py` 把启动分区切换到名为 `beatbox` 的 OTA 分区。烧录前必须重新核对开发板、芯片、端口和分区表；端口名称会随电脑环境变化，不要把某个 COM 或 `usbmodem` 编号写死。固定更新包和校验值见 [`artifacts/release-v1.0.0-browser-hmac`](artifacts/release-v1.0.0-browser-hmac/README.md)。

### 启动网页应用

回到仓库根目录：

```bash
pnpm install
pnpm dev
```

使用 Chrome 或 Edge 打开终端显示的本地地址。新电脑或清除过网站存储的浏览器第一次连接时：先停止播放，按住开发板 S7 三秒，看到蓝灯后点击网页的“蓝牙连接”，选择 `EasyInput Beatbox`。网页会自动生成一份只保存在当前浏览器里的随机凭据；不需要设备配网码或 Windows 蓝牙配对码，也不会占用电脑原来的 Wi-Fi。浏览器授权过的设备以后会优先尝试自动重连。

> 安全边界：这套方案是“实体 S7 许可 + 应用层挑战应答”，不是 Windows 的 BLE bonding，也不声称普通控制流量具有链路层加密。它能拒绝未登记浏览器并防止直接重放旧认证答案；不能抵御登记窗口内的无线窃听、中间人或普通 Beatbox 命令窃听。

若蓝牙不可用，可点击“USB 备用”，在串口授权窗口中选择 Espressif 设备。应用收到设备的 `hello` 与 `state` 后进入同步状态。

USB 串口同一时间只能由一个程序打开。使用“USB 备用”前，停止 Python/ESP-IDF 串口监听，并断开其他浏览器标签中的 USB 连接。USB 仅作为供电线插着时仍可使用蓝牙；不要为了供电而在网页打开 USB。换到 GitHub Pages、换浏览器或清除网站存储时属于新的浏览器身份，需要重新按 S7 登记。

在线网页的后续修正通过新提交保存，不覆盖固定的 1.0.0 固件和下载包。蓝牙操作队列、`GATT operation already in progress` 排查及双端日志使用方法，见 [蓝牙连接与白盒诊断](docs/ble-connection.md)。

运行网页测试：

```bash
pnpm test
```

### 启动桌面应用

macOS 系统 WebView 不提供 Web Serial，因此桌面版本使用 Electron 封装网页应用。

```bash
# 终端 1：启动网页开发服务器
pnpm dev

# 终端 2：启动 Electron
pnpm desktop:dev
```

也可以构建网页后直接加载：

```bash
pnpm desktop:start
```

## Pattern 与叠录

Pattern 由六条鼓轨和十六个 Step 组成。A、B 是两份完整 Pattern；Fill 是按住 S7 时临时覆盖的加花谱。

网页中的 REC 不会录制音频波形。录音武装后，实体 Pad 仍由板端立即发声，同时将 note 事件发送给网页；网页根据当前位置量化到最近的 Step，再写回当前 A/B Pattern。已有格子会保留，因此可以边播边叠加新的鼓层。

Pattern 支持 JSON 导入与导出，适合保存、分享或版本管理节奏数据。

## 为什么暂时不是 USB MIDI Class

ESP32-S3 的原生 USB PHY 同时承担烧录、Serial/JTAG 与 USB MIDI Class 需求。在当前 macOS 链路中，直接切换到 TinyUSB MIDI 会带来枚举与烧录稳定性问题。

因此本项目采用两层设计：

1. 先稳定产品语义：音符、Clock、Start / Stop / Continue、BPM、Pattern 和位置；
2. 当前用 USB Serial + JSON Lines 承载，未来满足门槛后可以替换为 USB MIDI Class 或桌面桥接。

设备管理命令不会用不存在的“标准 CC”假装实现 BPM 或 Pattern。详细决策见 [`docs/usb-midi-spike.md`](docs/usb-midi-spike.md)。

## 仓库结构

```text
easyinput-beatbox/
├── easyinput-factory/  EasyInput 键盘模式集成快照
├── firmware/          ESP-IDF 固件
│   └── main/
│       ├── board/     按键、板级常量与共享电源
│       ├── audio/     I2S、click、采样播放与混音
│       ├── seq/       时钟、Pattern 与音序器
│       ├── ui/        RGB 状态显示与实体登记手势
│       ├── ble/       BLE GATT、浏览器凭据、挑战认证与分片传输
│       └── host/      BLE/USB 共用主机协议
├── app/               Vite + Web Bluetooth / Web Serial 网页应用
├── desktop/           Electron 桌面封装
├── assets/samples/    鼓组采样资源
├── docs/              产品、协议与课程文档
├── artifacts/         已校验的双模式固件组合包
└── scripts/           检查与辅助脚本
```

## 文档导航

- [完整制作讲义](docs/course/waytoagi-making-process.md)：从识别硬件到节拍器、鼓机、网页联动和 MIDI 语义的完整实操流程。
- [课程 PPT 分页稿](docs/course/waytoagi-ppt-outline.md)：90–120 分钟课程的页面结构和讲师口播。
- [产品合同](docs/product-contract.md)：音色、时钟、交互、连接和阶段目标。
- [主机协议](docs/host-protocol.md)：BLE/USB 共用 JSON 消息、分片与同步规则。
- [v1.0.0 固定版本说明](docs/releases/v1.0.0.md)：本版功能、实际验证、备份文件和剩余边界。
- [烧录与恢复指南](docs/flashing-and-recovery.md)：文件校验、地址、BOOT、启动分区、USB 占用和版本回退。
- [MIDI 语义](docs/midi-protocol.md)：Clock、运输控制、GM Channel 10 与设备管理命令。
- [USB MIDI 可行性记录](docs/usb-midi-spike.md)：为什么当前保留 Serial，以及替换运输层需要满足的门槛。

## 开发与验收

```bash
pnpm build
pnpm test
./scripts/check_board_baseline.sh
./scripts/check_timing_model.py
```

静态检查通过不等于真机验收完成。发声、灯光、60/120/240 BPM、Swing、长时间漂移、S7 实体登记窗口、BLE 断线重连、USB 备用和 Pattern 同步仍应在真实硬件上验证。

## AI / Agent 协作

项目把两类知识分开维护：上游 [easyinput-board-cy](https://github.com/CY-CHENYUE/easyinput-board-cy) 描述引脚、电源、信号方向和安全边界等不可随意改变的板级事实；本仓库的 Beatbox 产品约定描述键位、节拍、Pattern 与协议等产品行为。

```bash
./scripts/setup_skills.sh
```

修改硬件脚位、BOOT、GPIO8 或外设代码前，应先核对上游开发板资料；修改键位、BPM、Pattern 或协议时，以 Beatbox 产品约定为准。两个仓库分别维护各自的代码与授权条款。

## 许可

Beatbox 自有代码沿用原作者的 [MIT License](LICENSE)，原作者版权声明保留。

`easyinput-factory/` 是独立的键盘模式集成快照，遵守该目录自己的 [PolyForm Noncommercial 1.0.0](easyinput-factory/LICENSE) 与 [第三方声明](easyinput-factory/THIRD_PARTY_NOTICES.md)，不因被放入本仓库而变成 MIT。快照范围和构建状态见 [集成说明](easyinput-factory/DEVICE_MODE_INTEGRATION.md)；它不是本轮重新验证的完整 Maker 发布包。

仓库中来自第三方的图标、音频采样及其他素材，继续遵循其所在目录中标注的原始许可条款。
