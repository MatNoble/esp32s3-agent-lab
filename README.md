# ESP32-S3 固件研发与智能体实践实验室 (ESP32-S3 Research Lab)

<p align="center">
  <img src="https://img.shields.io/badge/Platform-ESP32--S3-E7352C?logo=espressif&logoColor=white" alt="Platform">
  <img src="https://img.shields.io/badge/SoC-N16R8%20(16M%20Flash%20+%208M%20PSRAM)-007ACC" alt="Storage">
  <img src="https://img.shields.io/badge/OS-macOS%20Apple%20Silicon-black?logo=apple&logoColor=white" alt="macOS">
  <img src="https://img.shields.io/badge/Framework-PlatformIO%20%7C%20Arduino-orange?logo=platformio" alt="PlatformIO">
  <img src="https://img.shields.io/badge/License-MIT-green" alt="License">
</p>

本项目是专为 **ESP32-S3-WROOM-1 (N16R8)** 双 Type-C 开发板构建的高质量固件研发仓库。以**零外部传感器**为基准，深度挖掘芯片原生算力（双核 240MHz、矢量指令、8MB 高速 PSRAM、Wi-Fi 物理层 CSI、BLE 5.0、内部温度探针与板载 WS2812B），并提供面向人类开发者与 **AI Coding Agent** 的全链路工业级规范与自动化工具流。

---

## 🚀 核心硬件特性与架构

| 硬件维度 | 具体参数与拓扑结构 | 关键配置与注意点 |
| :--- | :--- | :--- |
| **主控芯片** | **ESP32-S3** (Xtensa® 32-bit LX7 双核 @ 240MHz, 包含 AI 矢量扩展指令) | 支持高性能 DSP 与边缘机器学习运算 |
| **片外 Flash** | **16MB** NOR Flash (Quad 器件，实测仅 `DIO` 模式可启动) | `board_build.flash_mode = dio`<br>`board_build.partitions = default_16MB.csv` |
| **片外 PSRAM** | **8MB** 高速 Pseudo-SRAM (**OPI** 八线并行总线，支持双沿 DDR 传输) | `board_build.arduino.memory_type = dio_opi`<br>`build_flags = -DBOARD_HAS_PSRAM` |
| **UART 接口** | 板载 **CH343P** 硬件 USB 转串口芯片，连接至 `GPIO 43/44` | 原理图内嵌 Q1/Q2 双三极管自动下载电路，烧录**100% 自动复位** |
| **原生 USB 接口** | SoC 原生 USB OTG / CDC 差分直连（`GPIO 19/20`） | 硬件 USB CDC + JTAG 调试器合一，支持超高速数据吞吐 |
| **板载状态灯** | **XL-5050RGBC-WS2812B** 可寻址幻彩 LED（直连 `GPIO 48`） | **直连 5V 电源轨**！代码内强制采用微光模式（RGB ≤ 4） |
| **扩展排针** | 双排 22-Pin 标准 2.54mm 排针（J1 与 J2，共 44 引脚） | 宽间距设计，适合直插面包板或引出杜邦线 |

---

## 📁 项目目录与工程规划

本仓库采用**全解耦独立工程架构**，每个子目录均包含独立的 `platformio.ini` 与源码：

```text
/Users/matnoble/Codes/ESP32/
├── README.md                   # 本文件（项目主页与快速上手）
├── AGENTS.md                   # AI Coding Agent 核心行为守则与安全红线
├── DEVELOPER_GUIDE.md          # 深度实战开发指南、引脚详表与故障排查树
├── HARDWARE_SPEC.md            # 电路原理图精细解析与硬件参数规格书
├── run.sh                      # 一键式编译、烧录、端口探测与监视脚本
├── hardware_docs/              # 硬件厂商原始技术文档（未入库，仅本地保留）
│
├── 00_hello_world_uart/        # 【基准工程 1】CH343 硬件串口工程（日常主力）
│   ├── platformio.ini          # 纯净硬件 UART0 (GPIO 43/44) 配置
│   └── src/main.cpp            # 芯片体检 + 8MB PSRAM 挂载 + 低饱和微光心跳
│
├── 01_hello_world_usb/         # 【基准工程 2】原生 USB 端口工程（高速通信/JTAG）
│   ├── platformio.ini          # 原生 USB CDC (JTAG/Serial debug unit) 专用配置
│   └── src/main.cpp            # 原生 USB 流 + 非阻塞心跳 + 淡冰蓝指示灯
│
├── 04_uav_telemetry_mqtt/      # 【已创建】契约 v1.0 MQTT 遥测上云节点（UAV-LAB-01）
├── 05_i2c_bme280/              # 【已创建】I2C 扫描 + BME280 温/压/湿/气压高度（首个外设传感器）
├── 06_i2c_sensor_suite/        # 【已创建】BME280+MPU6050+OLED 三器件共线首发（裸寄存器 MPU 驱动）
│
└── [后续科研规划里程碑]/
    ├── 02_wifi_csi_radar/      # 基于底层 Wi-Fi CSI 的室内无感人体呼吸与移动雷达
    └── 03_uav_mavlink/         # 无人机 MAVLink 飞控遥测仿真与双向通信链路
```

---

## ⚡️ 快速上手 (Quick Start)

### 1. 环境准备 (macOS Apple Silicon)
本项目推荐使用 [PlatformIO Core (CLI)](https://docs.platformio.org/en/latest/core/index.html)。在终端中确认 `pio` 命令行工具可用：
```bash
pio --version
```
> 若未添加环境变量，可执行 `source ~/.zshrc` 或将 `~/.platformio/penv/bin` 加入 `PATH`。

### 2. 检查开发板连接
通过双 Type-C 数据线将开发板接入 Mac，运行探针命令：
```bash
./run.sh list
```
> ⚠️ `/dev/cu.*` 的端口名由 macOS 自动分配（可能是 `usbmodem101` 这类位置名，也可能含芯片 MAC），**不能作为判别端口的依据**。请用下面的命令看 VID:PID：
```bash
pio device list
```
* `VID:PID=1A86:*` → **UART 口**（板载 CH343P 串口芯片）
* `VID:PID=303A:1001` → **USB 口**（ESP32-S3 原生 JTAG/Serial 单元）

### 3. 一键编译、烧录与监视
使用根目录提供的自动化运维脚本：

* **使用标有「UART」的接口**：
  ```bash
  ./run.sh uart
  ```
  自动编译 `00_hello_world_uart`，通过 CH343 硬件三极管自动复位并启动 115200 串口监视。

* **使用标有「USB」的接口**：
  ```bash
  ./run.sh usb
  ```
  自动编译 `01_hello_world_usb`，烧录后若停留在 ROM 模式，按一下板载物理 **RST** 键即可运行。

* **独立开启串口监视器**：
  ```bash
  ./run.sh monitor
  ```

---

## 🧭 文档索引系统

为了避免开发过程中的信息遗漏与硬件损坏，请遵循以下查阅路径：

1. **[`AGENTS.md`](file:///Users/matnoble/Codes/ESP32/AGENTS.md)**：面向 AI 编程智能体，详细阐述硬件不可动引脚、存储配置公式及安全避坑红线。
2. **[`DEVELOPER_GUIDE.md`](file:///Users/matnoble/Codes/ESP32/DEVELOPER_GUIDE.md)**：面向开发者与 Agent 的实操指南，收录 44 引脚完整推荐分配表、双接口黄金配置对比及故障自愈清单。
3. **[`HARDWARE_SPEC.md`](file:///Users/matnoble/Codes/ESP32/HARDWARE_SPEC.md)**：硬件工程师视角参数手册，包含原理图各功能单元标注、CJ6107A 稳压特性与尺寸图解。
4. **`hardware_docs/`（未入库，仅本地保留）**：包含原理图 `ESP32-S3-SCH-V1.4.pdf` 与标准尺寸 CAD 图，属厂商版权资料。

---

## 🛡️ 硬件级避坑与安全规范

1. **WS2812B 强光防护**：板载 RGB LED 直连 5V 电源，亮度极高。软件中务必限制 RGB 值为 `1 ~ 4`，杜绝满幅直射导致刺眼与发热。
2. **总线配置 (N16R8)**：必须使用 `board_build.flash_mode = dio` 与 `board_build.arduino.memory_type = dio_opi`。误设 `qio` 会导致启动死机；遗漏 `dio_opi` 则会导致 8MB PSRAM 挂载失效。
3. **引脚冲突隔离**：严禁将 `GPIO 43/44`（板载 CH343）与 `GPIO 19/20`（原生 USB）复用为普通外设引脚。

---

## 📜 许可证 (License)

本项目遵循 [MIT License](https://opensource.org/licenses/MIT) 开源协议。
