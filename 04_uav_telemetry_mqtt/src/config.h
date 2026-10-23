#pragma once

// —— 节点身份（契约 §3 注册表：批一 5 块 ESP32 的 01 号 = 母机）——
#define NODE_ID     "UAV-LAB-01"
#define FW_VERSION  "0.7.2"   // 0.7.1 基础上：家庭锚点补第二台 AP（同名 SSID 多物理 AP 场景）+ [WiFiPOS] 日志带 SSID

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

// —— WiFi 粗定位兜底（GPS 到货前 / 失锁时的位置源，粒度 = AP 的位置）——
// 原理：把"哪个 AP(BSSID/MAC) 在哪个坐标"记成表，板子连上已知 AP 就报该坐标；表里没有 → 报 null。
// 真实坐标属位置隐私：表在 wifi_pos_private.h（已 gitignore 不入库），模板 = wifi_pos_private.example.h。
// 坐标拾取：高德坐标拾取器 lbs.amap.com/tools/picker 长按取点；BSSID 看串口 [WiFiPOS] 行。
typedef struct { const char *bssid; float lat; float lon; } wifi_pos_t;
#if __has_include("wifi_pos_private.h")
#include "wifi_pos_private.h"                    // 只需维护表行（坐标一律 WGS-84）
#else
static const wifi_pos_t WIFI_POS_TABLE[] = {};   // 未建表：所有 AP 未命中 → lat/lon 按契约报 null
#endif
