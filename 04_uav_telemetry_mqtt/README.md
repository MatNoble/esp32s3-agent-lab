# 04_uav_telemetry_mqtt — 契约 v1.0 遥测上云节点（母机 UAV-LAB-01）

对应《实验平台执行任务书》T1.3：WiFi + MQTT 按契约上云——**全项目第一条真实硬件遥测链路**。

| 状态 | 说明 |
|------|------|
| 2026-10-20 | 骨架完成，`pio run` 编译通过；板未插，尚未烧录。待：填凭据 → 烧录 → 树莓派在线后验收 |
| 2026-10-22 | BME280 到货接入：I2C（SDA=8/SCL=9），`alt` 由 null → 实测气压高度；传感器验收走根目录 `05_i2c_bme280`（`./run.sh bme`） |
| 2026-10-22（T1.2） | **fw 0.6.0**：MPU6050 裸寄存器驱动接入（roll/pitch 实测）、OLED 本机状态屏上线。烧录 `./run.sh telemetry`，看板识别凭证 = fw 字段变 0.6.0 |

## 接线

BME280（3.3V 款）四线：母对母直连，或面包板 + 公对公跳线均可（接法细节见根目录 `05_i2c_bme280` README；6 焊盘款的 CSB/SDO 悬空）：

| BME280 | ESP32-S3 | 备注 |
|--------|----------|------|
| VCC | 3V3 | 3.3V 款无稳压，严禁 5V |
| GND | GND | 共地 |
| SCL | GPIO 9 | 四传感器共享 I2C 总线 |
| SDA | GPIO 8 | |

## 首次使用

1. 编辑 `src/wifi_secrets.h`（已 gitignore），填三个值：
   - `WIFI_SSID / WIFI_PASS`：家庭 WiFi（**必须 2.4GHz**，ESP32 不支持 5G）
   - `MQTT_HOST`：树莓派局域网 IP（在 Pi 上 `hostname -I` 查）。ESP32 对 `raspberrypi.local` 的 mDNS 解析不可靠，优先写 IP，或在路由器上做 DHCP 地址保留
2. 插板（UART/COM 口），烧录 + 监视：

   ```bash
   ./run.sh telemetry
   ```

3. 串口出现 `[遥测 #000001] seq=1 已发布` 即链路打通。

## 验收（任务书 T1.3 标准）

- S0 看板（https://uav.matnoble.top）出现 UAV-LAB-01 曲线实时跟动；
- 或在 Pi 上 `uav-lab/venv/bin/python tools/validate_telemetry.py --broker <Pi-IP>`，观察条数递增、seq 零缺口、延迟毫秒级。

## 当前边界（后续任务接入点）

- **alt 已接入（2026-10-22）**：BME280 气压高度，按契约 §2 报"相对上电零点"（开机连采 8 次自动定零），传感器缺席自动回退 null；
- **roll/pitch 已实测（T1.2，fw 0.6.0）**：MPU6050 裸寄存器驱动（自 06 搬运），加速度计静止姿态角（atan2 重力投影）；晃动会漂属预期——互补/卡尔曼滤波是毕设 #4 的正戏，届时本驱动即对照组；
- **yaw 暂报 null**：需陀螺仪积分+滤波（自由漂移），落地前按契约报 null 不冒充；
- **OLED 本机状态屏**：seq/RSSI/MQTT/姿态/alt 现场显示，不上云；GFX 默认字库仅 ASCII，勿写中文；
- **06 与 04 的分工**：`06_i2c_sensor_suite` 是传感器台架（无 WiFi/MQTT，不上云），04 才是上云节点——大屏只认 04；
- **QoS**：PubSubClient 固定 QoS0 发布（契约 §7 注记允许，丢包靠 seq 缺口度量）；确需 QoS1 时换 ArduinoMqttClient，报文格式不变；
- **S2 断连补传**：`mode` 目前恒为 `NORMAL`；环形缓冲 + BUFFERING/DEGRADED 状态机在 S2 实现；
- **NTP 门槛**：对时成功前不发布（契约要求 ts 为有效 Unix 毫秒），串口会提示。
