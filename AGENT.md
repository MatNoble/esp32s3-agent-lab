# AGENT.md - AI Coding Agent 核心开发守则与系统指引

> **适用对象**：所有接入本工作区进行代码编写、构建调试、硬件驱动或文档维护的 AI Coding Agent。  
> **核心准则**：硬件在席，操作有痕；先阅约束，后落代码；严格防护，杜绝烧蚀。

---

## 1. 项目基础元数据

* **工作区根目录**：`/Users/matnoble/Codes/ESP32`
* **主控模组**：ESP32-S3-WROOM-1-N16R8（Xtensa® 32-bit LX7 双核，主频最高 240MHz）
* **存储规格**：16MB Flash（Quad/Dual SPI，ROM 锁定 **`DIO`** 模式）+ 8MB PSRAM（**`Octal SPI / OPI`** 八线高速）
* **板载物理接口**：双 Type-C 独立接口（左侧原生 USB直连 SoC，右侧 CH343P 硬件串口转接）
* **宿主开发环境**：Mac (Apple Silicon) + macOS + PlatformIO CLI（安装路径：`~/.platformio/penv/bin/pio`）
* **自动化脚本**：项目根目录 [`run.sh`](file:///Users/matnoble/Codes/ESP32/run.sh)

---

## 2. 知识库导航与查阅顺序

任何 Agent 在执行具体任务前，**必须**按以下层级读取或索引对应文档：

```text
       ┌──────────────┐
       │   AGENT.md   │  <-- 【第一入口】本守则：行动准则、安全红线、架构蓝图
       └──────┬───────┘
              │
       ┌──────┴───────────────┐
       ▼                      ▼
┌──────────────────┐   ┌─────────────────┐
│DEVELOPER_GUIDE.md│   │HARDWARE_SPEC.md │
│   实战避坑与SOP   │   │ 电路原理与硬件参数 │
└──────────────────┘   └─────────────────┘
```

1. **[`AGENT.md`](file:///Users/matnoble/Codes/ESP32/AGENT.md)**（当前文件）：确立安全红线与编译规则。
2. **[`DEVELOPER_GUIDE.md`](file:///Users/matnoble/Codes/ESP32/DEVELOPER_GUIDE.md)**：包含原理图级引脚定义、双端口切换逻辑、避坑清单及故障树。
3. **[`HARDWARE_SPEC.md`](file:///Users/matnoble/Codes/ESP32/HARDWARE_SPEC.md)**：包含芯片 VID:PID、物理尺寸、稳压电路及元器件选型详情。
4. **[`hardware_docs/`](file:///Users/matnoble/Codes/ESP32/hardware_docs)**：包含硬件厂商 PDF 原理图（`ESP32-S3-SCH-V1.4.pdf`）与机械尺寸图。

---

## 3. 绝对红线（Hardware Constraints & Safety Guardrails）

### 3.1 专用管脚严禁复用 (Reserved GPIOs)
* **`GPIO 43 / 44`**：板载 CH343P 串口专用（U0TXD / U0RXD）。严禁将其重映射为普通 GPIO 或挂载其他传感器。
* **`GPIO 19 / 20`**：原生 USB 差分数据线（D- / D+）。使用原生 USB 通道通信时禁止改作他用。
* **`GPIO 0`**：芯片 Strapping 引脚 / BOOT 按键。开机瞬间必须保持高电平以进入正常运行状态。
* **`CHIP_PU`**：硬件复位引脚（EN）。

### 3.2 板载 WS2812B 彩灯防护（GPIO 48）
* **高危特征**：原理图显示板载 WS2812B（XL-5050RGBC）的供电脚 **直接连接到 5V 电源轨**（VBUS），且未配柔光遮罩。
* **亮度红线**：
  * **单色 RGB 数值严禁超过 `10`**！
  * **推荐亮度**：RGB 数值严格保持在 `1 ~ 4`（如淡粉 `(3, 1, 1)`、薄荷绿 `(1, 3, 2)`、淡冰蓝 `(1, 2, 4)`）。
  * 严禁满幅直射 `(255, 255, 255)`，防止高热损耗与刺眼强光。

### 3.3 存储总线配置法则 (N16R8 黄金法则)
所有新建工程的 `platformio.ini` **必须**显式指定以下配置，严禁遗漏：
```ini
; 16MB Flash (DIO 模式) + 8MB PSRAM (OPI 八线模式)
board_build.flash_mode = dio
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv
board_build.arduino.memory_type = dio_opi

build_flags = 
    -DBOARD_HAS_PSRAM
```
* **禁止将 `flash_mode` 设为 `qio`**（芯片 BootROM 锁定 `DIO`，设为 `qio` 会导致启动在 `entry 0x...` 瞬间死锁）。
* **禁止遗漏 `memory_type = dio_opi`**（默认 4 线 QSPI 会导致 8 线物理 PSRAM 握手失败，使 `psramFound() == false`）。

---

## 4. 双 Type-C 端口架构与构建规范

| 物理端口 | 对应工程模板 | 端口宏定义原则 | 复位机制与烧录行为 |
| :--- | :--- | :--- | :--- |
| **右侧 UART 接口**<br>(CH343P 芯片) | [`00_hello_world_uart`](file:///Users/matnoble/Codes/ESP32/00_hello_world_uart) | **严禁**添加 `-DARDUINO_USB_CDC_ON_BOOT=1`<br>日志走硬件串口 0 | 原理图内嵌 Q1/Q2 双三极管电路；**100% 自动复位**，开箱即用 |
| **左侧 USB 接口**<br>(原生 USB CDC/JTAG) | [`01_hello_world_usb`](file:///Users/matnoble/Codes/ESP32/01_hello_world_usb) | **必须**定义：<br>`-DARDUINO_USB_CDC_ON_BOOT=1`<br>`-DARDUINO_USB_MODE=1` | 无三极管电路；若烧录后停留在 ROM 下载模式，需**手动按一次板载 RST 键** |

---

## 5. 项目工程组织与命名约定

每个子工程保持完全独立、自治，便于复用与隔离：

```text
/Users/matnoble/Codes/ESP32/
├── 00_hello_world_uart/     # 串口基准固件 (CH343 控制台 / 硬件探针)
├── 01_hello_world_usb/      # 原生 USB 基准固件 (CDC 虚拟串口 / JTAG)
├── 02_wifi_csi_radar/       # 基于底层 Wi-Fi CSI 物理层的人体无感探测雷达
├── 03_uav_mavlink/          # 无人机 MAVLink 飞控遥测与地面站仿真
└── [xx_project_name]/       # 遵循双位数字递增前缀：XX_功能简述
```

### 新工程创建清单 (Agent Checklist)
1. 复制基准工程的 `platformio.ini`（根据目标接口选择以 UART 还是 USB 为底座）。
2. 核对并锁定 N16R8 四件套（`flash_mode = dio`、`dio_opi`、`default_16MB.csv`、`-DBOARD_HAS_PSRAM`）。
3. 遵循淡光微亮 RGB 控制规范（GPIO 48，数值 ≤ 4）。
4. 在 [`run.sh`](file:///Users/matnoble/Codes/ESP32/run.sh) 中注册快捷调用命令。

---

## 6. 标准工作流与常用操作

Agent 可直接调用项目根目录下的封装脚本执行标准运维：

```bash
# 1. 检查物理设备连接端口
./run.sh list

# 2. 编译并烧录 UART 串口工程（带自动监视）
./run.sh uart

# 3. 编译并烧录 原生 USB 工程（带自动监视）
./run.sh usb

# 4. 单独开启 115200 波特率监视器
./run.sh monitor

# 5. 清理构建缓存（修改 build_flags 后必须执行）
pio run -d <project_dir> -t clean
```
