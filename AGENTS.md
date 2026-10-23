# AGENTS.md - AI Coding Agent 核心开发守则与系统指引

> **适用对象**：所有接入本工作区进行代码编写、构建调试、硬件驱动或文档维护的 AI Coding Agent。  
> **核心准则**：硬件在席，操作有痕；先阅约束，后落代码；严格防护，杜绝烧蚀。

---

## 1. 项目基础元数据

* **工作区根目录**：`/Users/matnoble/Codes/ESP32`
* **主控模组**：ESP32-S3-WROOM-1-N16R8（Xtensa® 32-bit LX7 双核，主频最高 240MHz）
* **存储规格**：16MB Flash（Quad 器件，实测仅 **`DIO`** 模式可启动）+ 8MB PSRAM（**`Octal SPI / OPI`** 八线高速）
* **板载物理接口**：双 Type-C 独立接口，以丝印标签识别——「USB」口为原生 USB 直连 SoC，「UART/COM」口经板载 CH343P 串口芯片转接（不要依赖左右方位判断端口）
* **宿主开发环境**：Mac (Apple Silicon) + macOS + PlatformIO CLI（安装路径：`~/.platformio/penv/bin/pio`）
* **自动化脚本**：项目根目录 [`run.sh`](file:///Users/matnoble/Codes/ESP32/run.sh)

---

## 2. 知识库导航与查阅顺序

任何 Agent 在执行具体任务前，**必须**按以下层级读取或索引对应文档：

```text
       ┌──────────────┐
       │  AGENTS.md   │  <-- 【第一入口】本守则：行动准则、安全红线、架构蓝图
       └──────┬───────┘
              │
       ┌──────┴───────────────┐
       ▼                      ▼
┌──────────────────┐   ┌─────────────────┐
│DEVELOPER_GUIDE.md│   │HARDWARE_SPEC.md │
│   实战避坑与SOP   │   │ 电路原理与硬件参数 │
└──────────────────┘   └─────────────────┘
```

1. **[`AGENTS.md`](file:///Users/matnoble/Codes/ESP32/AGENTS.md)**（当前文件）：确立安全红线与编译规则。
2. **[`DEVELOPER_GUIDE.md`](file:///Users/matnoble/Codes/ESP32/DEVELOPER_GUIDE.md)**：包含原理图级引脚定义、双端口切换逻辑、避坑清单及故障树。
3. **[`HARDWARE_SPEC.md`](file:///Users/matnoble/Codes/ESP32/HARDWARE_SPEC.md)**：包含芯片 VID:PID、物理尺寸、稳压电路及元器件选型详情。
4. **`hardware_docs/`（未纳入 git，仅本地保留）**：包含硬件厂商 PDF 原理图（`ESP32-S3-SCH-V1.4.pdf`）与机械尺寸图，属厂商版权资料。
5. **遥测契约（外部权威文档）**：`/Users/matnoble/Documents/JG/11 科研/校级孵化项目/02 无人机-低空经济/遥测契约/遥测数据格式说明.md`——04 上云报文 14 键格式与 null 规则的唯一仲裁标准（同目录含 JSON Schema 与校验器）。

---

## 3. 绝对红线（Hardware Constraints & Safety Guardrails）

### 3.1 专用管脚严禁复用 (Reserved GPIOs)
* **`GPIO 43 / 44`**：板载 CH343P 串口专用（U0TXD / U0RXD）。严禁将其重映射为普通 GPIO 或挂载其他传感器。
* **`GPIO 19 / 20`**：原生 USB 差分数据线（D- / D+）。使用原生 USB 通道通信时禁止改作他用。
* **`GPIO 0`**：芯片 Strapping 引脚 / BOOT 按键。开机瞬间必须保持高电平以进入正常运行状态。
* **`GPIO 3 / 45 / 46`**：Strapping 配置脚。外接器件前必须核对上电电平影响，能不用就不用。
* **`GPIO 26–32`**：Flash/PSRAM 内部总线，未引出排针，天然不可用。
* ⚠️ **本板排布与 Espressif 官方 DevKitC-1 通用图不同**（46 已引出、TX/RX 位于左排顶部、右排末端有 19/20 等）。引脚物理位置的**唯一权威 = `DEVELOPER_GUIDE.md` §1.2 定稿表**（依据 `hardware_docs/ESP32-S3-SCH-V1.4.pdf` 原理图逐脚核对）。禁止照搬网络通用引脚图。
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
* **禁止将 `flash_mode` 设为 `qio`**（本板实测 Flash 仅能以 `DIO` 时序访问，设为 `qio` 会导致启动在 `entry 0x...` 瞬间死锁）。
* **禁止遗漏 `memory_type = dio_opi`**（默认 4 线 QSPI 会导致 8 线物理 PSRAM 握手失败，使 `psramFound() == false`）。
* 本四件套的权威定义位于 `HARDWARE_SPEC.md` §3，各文档如有出入，以该处为准。

### 3.4 I2C 总线定脚与器件地址（外设红线）
* **SDA = GPIO 8，SCL = GPIO 9**，全部外设共享同一条总线；新工程一律复用定脚，禁止另选引脚。
* 器件地址注册表（验证手段 = 05/06 工程的开机总线扫描）：

| 器件 | 地址 | 状态 |
|------|------|------|
| MPU6050（GY-521） | 0x68（AD0 拉高时 0x69） | 已接入（04/06） |
| BME280 | 0x76（SDO 拉高时 0x77） | 已接入（04/05/06） |
| OLED 0.96" SSD1306 | 0x3C（个别模块 0x3D） | 已接入（04/06） |
| INA219 | 0x40 | 待到货 |

* MPU6050 使用**裸寄存器驱动**（04/06 已有资产：唤醒 0x6B、量程 0x1C/0x1B、DLPF 低通 0x1A←3=44Hz〔静噪关键，默认 0=关闭〕、连续读 14 字节 @0x3B；BME280 同理开 IIR FILTER_X16），不使用第三方库——对国产兼容芯免疫且不卡 WHO_AM_I 校验；新增 I2C 器件优先参考此模式。
* **接线唯一权威图**：`04_uav_telemetry_mqtt/docs/circuit-uav-lab-01.svg`（论文级电路连接图：实线 = 已接调试通过，虚线 = INA219 / NEO-7M 到货后的预留）。新器件先对上方地址表、再对图接线；器件到货接线后把对应虚线转实线出一版新图。
* **实测静止基线（2026-10-23，fw 0.7.2 桌面实测，判断传感器健康用）**：roll/pitch 抖动 std ≈ 0.1°（静止读数本身 = 桌面真实倾角，非噪声）；alt 逐点抖动 std ≈ 7 cm；rssi 波动 ±8 dB 内。几分钟尺度的 alt 慢漂移属气压固有现象——室内机械排风（油烟机/新风）曾致 alt 单调"爬升"3 m 而姿态纹丝不动：**判板子是否位移看 roll/pitch，勿只看 alt**。另注意看板三图纵轴为自适应 min/max 铺满面板，波动的视觉幅度 ≠ 真实幅度，比较以轴刻度为准。

### 3.5 供电红线
* 3V3 轨由板载 LDO 供给（上限 ~500mA），I2C 外设一律从 3V3 取电；大功率器件才允许走 5V 轨。
* **锂电池（标称 3.7V）严禁直连 3V3 引脚**——超过 BME280 等器件 3.6V 上限会烧毁；须经 TP4056+升压板升至 5V，从 5V 引脚或 USB 口馈入。
* 脱机部署形态：充电头/充电宝插「UART/COM」口即可——程序存于板载 Flash，断电不丢、上电自启，电脑仅烧录与看日志时需要。

---

## 4. 双 Type-C 端口架构与构建规范

| 物理端口 | 对应工程模板 | 端口宏定义原则 | 复位机制与烧录行为 |
| :--- | :--- | :--- | :--- |
| **UART 接口**<br>(丝印「UART/COM」，CH343P 芯片) | [`00_hello_world_uart`](file:///Users/matnoble/Codes/ESP32/00_hello_world_uart) | **严禁**添加 `-DARDUINO_USB_CDC_ON_BOOT=1`<br>日志走硬件串口 0 | 原理图内嵌 Q1/Q2 双三极管电路；**100% 自动复位**，开箱即用 |
| **USB 接口**<br>(丝印「USB」，原生 CDC/JTAG) | [`01_hello_world_usb`](file:///Users/matnoble/Codes/ESP32/01_hello_world_usb) | **必须**定义：<br>`-DARDUINO_USB_CDC_ON_BOOT=1`<br>`-DARDUINO_USB_MODE=1` | 无三极管电路；若烧录后停留在 ROM 下载模式，需**手动按一次板载 RST 键** |

---

## 5. 项目工程组织与命名约定

每个子工程保持完全独立、自治，便于复用与隔离。工程分两轨：**台架轨**（05/06，无 WiFi/MQTT，专门验证传感器，不上云）与**上云轨**（04，唯一对外发遥测的节点——看板/数据库只认 04 的数据流）：

```text
/Users/matnoble/Codes/ESP32/
├── 00_hello_world_uart/     # 串口基准固件 (CH343 控制台 / 硬件探针)
├── 01_hello_world_usb/      # 原生 USB 基准固件 (CDC 虚拟串口 / JTAG)
├── 02_wifi_csi_radar/       # 【规划中，尚未创建】基于底层 Wi-Fi CSI 物理层的人体无感探测雷达
├── 03_uav_mavlink/          # 【规划中，尚未创建】无人机 MAVLink 飞控遥测与地面站仿真
├── 04_uav_telemetry_mqtt/   # 【上云轨】契约 v1.0 遥测节点 UAV-LAB-01（fw 0.7.2：BME280 alt + MPU6050 roll/pitch + OLED 状态屏 + MQTT 上云 + WiFi 粗定位 + SoftAP 配网零烧录）
├── 05_i2c_bme280/           # 【台架轨】I2C 总线扫描 + BME280 读数 + 真芯判别（ID 0x60 正品 / 0x58 假芯）
├── 06_i2c_sensor_suite/     # 【台架轨】三器件共线首发：MPU6050 裸寄存器驱动 + OLED 点亮（INA219 到货挂 0x40 即四件套）
└── [xx_project_name]/       # 遵循双位数字递增前缀：XX_功能简述
```

### 新工程创建清单 (Agent Checklist)
1. 复制基准工程的 `platformio.ini`（根据目标接口选择以 UART 还是 USB 为底座）。
2. 核对并锁定 N16R8 四件套（`flash_mode = dio`、`dio_opi`、`default_16MB.csv`、`-DBOARD_HAS_PSRAM`）。
3. 遵循淡光微亮 RGB 控制规范（GPIO 48，数值 ≤ 4）。
4. 涉及 I2C 外设：沿用 §3.4 定脚（SDA=8/SCL=9）与器件地址表，驱动参考 06 的裸寄存器模式。
5. 涉及 MQTT 上云：报文必须满足遥测契约（§2 第 5 条，14 键齐全、无传感器报 null），凭据放 `src/wifi_secrets.h`（已 gitignore，模板 = `wifi_secrets.example.h`）。**fw 0.7.0 起该文件仅为出厂默认值**：连接类配置（WiFi/broker 地址端口）实际存 NVS，换网/换 broker 走 SoftAP 配网热点 `UAV-LAB-01-Setup`（连不上网 45s 自动开，或按住 BOOT 键上电强制开），broker 地址支持 mDNS 主机名（本实验室 Pi = `pi-matnoble.local`，avahi 应答）——不再需要重烧。WiFi 粗定位查表在 `04 src/wifi_pos_private.h`（BSSID→WGS-84 坐标，已 gitignore 不入库、模板 = `wifi_pos_private.example.h`，缺文件编译为空表→报 null；同名 SSID 的多台物理 AP 每台一行）。
6. 在 [`run.sh`](file:///Users/matnoble/Codes/ESP32/run.sh) 中注册快捷调用命令。

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

# 4. 编译并烧录 04 遥测节点（上云轨，fw 0.7.2）
./run.sh telemetry

# 5. 编译并烧录 05 BME280 台架（I2C 扫描 + 真芯判别）
./run.sh bme

# 6. 编译并烧录 06 三传感器台架（共线首发）
./run.sh sensors

# 7. 单独开启 115200 波特率监视器
./run.sh monitor

# 8. 清理构建缓存（修改 build_flags 后建议执行；PlatformIO 通常能自动检测配置变化并重建）
pio run -d <project_dir> -t clean
```
