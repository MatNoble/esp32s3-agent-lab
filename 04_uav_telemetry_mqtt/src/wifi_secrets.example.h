#pragma once

// 凭据模板：复制为 wifi_secrets.h 后填写真实值（wifi_secrets.h 已 gitignore，不入库）
// fw 0.7.0 起：本文件只是"出厂默认值"——设备首次上电把它播种进 NVS，之后一切以
// NVS 为准（配网热点改配置不再需要重烧；按住 BOOT 键上电可强制重新配网）。
#define WIFI_SSID "CHANGE_ME"          // 家庭 WiFi，必须 2.4GHz（ESP32 不支持 5G）
#define WIFI_PASS "CHANGE_ME"
#define MQTT_HOST "pi-matnoble.local"  // 树莓派 mDNS 主机名（换网不用改 IP；也可填局域网 IP）
#define MQTT_PORT 1883
