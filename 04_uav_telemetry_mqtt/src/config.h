#pragma once

// —— 节点身份（契约 §3 注册表：批一 5 块 ESP32 的 01 号 = 母机）——
#define NODE_ID     "UAV-LAB-01"
#define FW_VERSION  "0.6.0"   // T1.2：MPU6050 姿态角实测 + OLED 本机状态屏

// 契约 §1：发布主题 uav/{uas_id}/telemetry；uav/{uas_id}/status 与 /cmd 是 v1.1 预留，不得占用
#define TOPIC_TELEM "uav/" NODE_ID "/telemetry"

// 上报频率：教学档 1Hz / 科研档 10Hz（契约不校验频率，由实验场景规定，如 E1 矩阵）
#define PUB_HZ 1.0f

// —— I2C 总线（BME280 已接入；MPU6050/INA219/OLED 到货后共用同一条总线）——
// GPIO8/9 既是 S3 的 Arduino 默认 I2C 脚，也在 DEVELOPER_GUIDE §1.2 J1 推荐可用清单内
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9

// 气压→高度折算基准（标准大气压）。alt 为相对上电零点（契约 §2），零点定标会抵消绝大部分
// 基准误差；该值主要影响会话内漂移，追求更稳可按当日天气 App 的海平面气压（hPa）改写。
#define SEALEVEL_HPA 1013.25f
