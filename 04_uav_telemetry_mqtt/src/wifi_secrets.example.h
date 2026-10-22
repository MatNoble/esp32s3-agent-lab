#pragma once

// 凭据模板：复制为 wifi_secrets.h 后填写真实值（wifi_secrets.h 已 gitignore，不入库）
#define WIFI_SSID "CHANGE_ME"          // 家庭 WiFi，必须 2.4GHz（ESP32 不支持 5G）
#define WIFI_PASS "CHANGE_ME"
#define MQTT_HOST "192.168.31.195"    // 树莓派局域网 IP：在 Pi 上执行 hostname -I 查得（当前值即此）
#define MQTT_PORT 1883
