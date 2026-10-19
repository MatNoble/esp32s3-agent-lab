# ESP32-S3 实战开发与 Agent 避坑指南 (Developer Guide)

> **核心定位**：**面向 AI Coding Agent（编程智能体）与人类开发者**。  
> **设计目标**：基于官方电路原理图（`ESP32-S3-SCH-V1.4`）与实测硬件特征，提供规范化项目结构、避坑规则、精准引脚定义与编译烧录 SOP，**彻底消除 Agent 在自动化编码与构建中可能出现的硬件级错误**。  
> **工作区路径**：`/Users/matnoble/Codes/ESP32`  
> **环境系统**：Mac (Apple Silicon M系列) + macOS Sequoia / Sonoma

---

## 1. 原理图级关键引脚分配与硬件约束（Agent 编程必读）

根据板载原理图 `ESP32-S3-SCH-V1.4`，本开发板的外设引脚具有严格的硬件连接。**任何 Agent 在编写硬件驱动代码时，必须优先查阅本节约束，严禁发生管脚复用冲突**。

### 1.1 内部硬件占用管脚（受限/专用，禁止任意用作普通外设）

| 物理引脚 | 原理图网络标号 | 硬件连接与功能说明 | Agent 编程约束与注意事项 |
| :--- | :--- | :--- | :--- |
| **GPIO 43** | `U0TXD` | 直连板载 **CH343P** 芯片 RXD，并联 TX 状态指示灯 | 默认作为系统主控制台串口，`00_hello_world_uart` 输出通道 |
| **GPIO 44** | `U0RXD` | 直连板载 **CH343P** 芯片 TXD，并联 RX 状态指示灯 | 默认作为系统主控制台串口输入 |
| **GPIO 19** | `USB_D-` | 直连原生 USB 接口的 **D- 差分信号线** | 使用原生 USB 通道时禁止用作 GPIO，否则 USB 通信立即中断 |
| **GPIO 20** | `USB_D+` | 直连原生 USB 接口的 **D+ 差分信号线** | 同上，差分传输通道 |
| **GPIO 48** | `RGB_CTRL` | 直连板载 **XL-5050RGBC-WS2812B** 彩灯数据脚 `DI` | **该灯珠直接接在 5V 电源轨上**，发光极其强烈！代码中亮度必须限制在 `1 ~ 4`，严禁满饱和度直射 |
| **GPIO 0** | `GPIO0` | 直连板载 **BOOT 按键** (ST-1185S) 及 Q2 自动下载电路 | 启动 Strapping 引脚。上电瞬间必须为高电平才能正常启动用户固件 |
| **CHIP_PU**| `EN` | 直连板载 **RST 按键** (ST-1185S) 及 Q1 自动复位电路 | 片上总复位脚，外接 10k 上拉与 0.1uF 滤波电容 |

### 1.2 外部排针用户可用推荐管脚 (User Safe GPIOs)

开发板引出两排 22-Pin 标准 2.54mm 排针（J1 与 J2，共 44 引脚），适合连接面包板或外设：

* **左排 J1 推荐可用引脚**：
  * `GPIO 4, 5, 6, 7`（支持普通 IO、ADC、低功耗 RTC）
  * `GPIO 15, 16, 17, 18`
  * `GPIO 8, 3, 46, 9, 10, 11, 12, 13, 14`
* **右排 J2 推荐可用引脚**：
  * `GPIO 1, 2`（支持模拟 ADC / 触控 Touch）
  * `GPIO 42, 41, 40, 39, 38, 37, 36, 35`
  * `GPIO 45, 47, 21`
* **供电引脚说明**：
  * **`5V`**：直接来自 USB 输入（经过防倒灌肖特基二极管 D1/D2），适合给舵机、外置大功率传感器供电。
  * **`3V3`**：来自板载 **CJ6107A33GW** 高性能 LDO 稳压器，最大输出电流约 500mA，注意避免短路。
  * **`GND`**：左右排针均有引出，布线时需共地。

---

## 2. 项目目录规范与双端口架构

工作区采用**模块化独立工程架构**，每个子工程均为一个独立自治的 PlatformIO 项目：

```text
/Users/matnoble/Codes/ESP32/
├── HARDWARE_SPEC.md            # 硬件规格与系统架构特性文档（供查阅或喂给 Agent）
├── DEVELOPER_GUIDE.md          # 本开发指南（AI Agent 与开发者首要读取文档）
├── run.sh                      # 一键自动化构建、烧录与监视运维脚本
├── hardware_docs/              # 官方解压资料（含 SCH V1.4 原理图、引脚尺寸 PDF）
│
├── 00_hello_world_uart/        # 【基准工程 1】CH343 硬件串口工程（日常稳定主力）
│   ├── platformio.ini          # 纯净硬件 UART0 (GPIO43/44) 配置
│   └── src/
│       └── main.cpp            # 芯片体检 + 淡粉/淡薄荷低饱和微光 + 串口心跳
│
├── 01_hello_world_usb/         # 【基准工程 2】原生 USB 端口工程（高速通信/JTAG）
│   ├── platformio.ini          # 原生 USB CDC (JTAG/Serial debug unit) 专用配置
│   └── src/
│       └── main.cpp            # 原生 USB 协议流 + 淡冰蓝低饱和微光 + 非阻塞心跳
│
└── [后续科研规划工程]/
    ├── 02_wifi_csi_radar/      # 基于底层 Wi-Fi CSI 的无感知人体入侵雷达
    └── 03_uav_mavlink/         # 无人机 MAVLink 飞控遥测与地面站仿真链路
```

---

## 3. 双端口实操烧录与监视指南

根据当前 Type-C 数据线插在哪个接口，选择对应的工程与配置：

### 场景 A：插在标有「UART / COM」的接口 (CH343 芯片)
* **对应工程**：[`00_hello_world_uart`](file:///Users/matnoble/Codes/ESP32/00_hello_world_uart)
* **设备特征**：Mac 识别为 `/dev/cu.usbmodem5D2F*` 或 `/dev/cu.wchusbserial*`
* **硬件机制**：原理图内嵌 **Q1/Q2 (SS8050-G) 双三极管自动下载电路**，由 DTR/RTS 控制复位，烧录完成百分之百自动重启，**绝不掉线**。
* **PlatformIO 黄金配置法则**：
  * **严禁**添加 `-DARDUINO_USB_CDC_ON_BOOT=1`（否则日志会被劫持至虚空的原生 USB）。
  * 强制指定 `board_build.flash_mode = dio`。
* **执行命令**：
  ```bash
  ./run.sh uart
  # 或手动执行:
  cd /Users/matnoble/Codes/ESP32/00_hello_world_uart && pio run -t upload && pio device monitor
  ```

### 场景 B：插在标有「USB」的接口 (ESP32-S3 原生 USB)
* **对应工程**：[`01_hello_world_usb`](file:///Users/matnoble/Codes/ESP32/01_hello_world_usb)
* **设备特征**：识别为带有芯片 MAC 地址的设备，如 `/dev/cu.usbmodem84C7BB7AC2D41`
* **硬件机制**：DP/DN 差分引脚直通芯片内部（GPIO 19/20），无三极管复位控制。烧录后若停在 ROM 下载等待模式，需轻按一下单片机上的物理 `RST` 键。
* **PlatformIO 黄金配置法则**：
  * 必须加入 `build_flags = -DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1`（`USB_MODE=1` = 硬件 CDC+JTAG 外设，即 0x303a:0x1001 设备；板级默认即此值，显式写出更稳妥）。
  * 仅在做 U 盘 / HID / DFU 等 TinyUSB 应用时才改用 `-DARDUINO_USB_MODE=0`（软件协议栈，会占用 OTG 外设并失去 JTAG 调试能力）。
  * 必须开启 `monitor_dtr = 1` 与 `monitor_rts = 1`。
  * C++ 代码的 `setup()` 中务必调用 `Serial.setTxTimeoutMs(0);` 消除缓冲区死锁。
* **执行命令**：
  ```bash
  ./run.sh usb
  # 或手动执行:
  cd /Users/matnoble/Codes/ESP32/01_hello_world_usb && pio run -t upload && pio device monitor
  ```

---

## 4. 命令行常用指令备忘录 (CLI CheatSheet)

| 操作需求 | 完整命令 | 适用场景 |
| :--- | :--- | :--- |
| **一键运行 UART 项目** | `./run.sh uart` | 自动完成编译、烧录并开启 115200 监视 |
| **一键运行 USB 项目** | `./run.sh usb` | 自动完成编译、烧录并开启 115200 监视 |
| **仅编译代码** | `pio run` | 验证语法与依赖，不连接单片机 |
| **全量清理重新构建** | `pio run -t clean && pio run` | 修改了 `build_flags` 宏定义后的必做操作 |
| **强制指定端口烧录** | `pio run -t upload --upload-port /dev/cu.xxx` | 多设备连接时防烧错目标 |
| **直接打开监视器** | `pio device monitor -b 115200` | 锁定 115200 速率防乱码 |
| **检查当前连接的端口** | `./run.sh list` | 快速查看当前单片机端口名 |

---

## 5. Agent 自动化诊断与故障排查清单 (Troubleshooting Flowchart)

### 故障 1：烧录后卡死在 `entry 0x...` 且无后续输出
* **诊断原因**：
  1. `flash_mode` 误设为 `qio`，而板子是 `dio`，在读取 Flash 时挂死；
  2. 使用了 CH343 口，但错误开启了 `CDC_ON_BOOT=1`，导致日志去了未连接的原生 USB；
  3. 使用了原生 USB 口，固件写入后未触发硬件复位，仍停在 ROM 下载模式。
* **处理方案**：
  * 检查 `platformio.ini` 确保指定 `board_build.flash_mode = dio`；
  * 如果插的是原生口，按一下板上的 `RST` 物理微动开关。

### 故障 2：串口监视器输出全是 `R~gBR~BRg...` 乱码
* **诊断原因**：波特率不匹配（默认 9600 读取了单片机 115200 发出的数据）。
* **处理方案**：运行命令时显式带上 `-b 115200` 参数。

### 故障 3：终端提示 `zsh: command not found: pio`
* **处理方案**：执行 `source ~/.zshrc` 刷新环境。
