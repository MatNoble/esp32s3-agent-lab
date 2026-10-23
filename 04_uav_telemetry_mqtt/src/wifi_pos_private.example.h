#pragma once

// 位置表模板：复制为 wifi_pos_private.h 后按真实 AP 填写（wifi_pos_private.h 已 gitignore，不入库）。
// 不复制也能正常编译上云：无表时所有 AP 未命中，lat/lon 按契约报 null。
// 采集方法：烧录后看串口 [WiFiPOS] 行拿 BSSID；坐标用高德拾取器 lbs.amap.com/tools/picker 长按取点，
// 拾取器给的是 GCJ-02，须逆换算为 WGS-84 再填（可交 AI 代做），否则地图落点偏 300~600m。
static const wifi_pos_t WIFI_POS_TABLE[] = {
    // {"AA:BB:CC:DD:EE:FF", 24.513726f, 118.096211f},   // 备注写归属，如"实验楼 301"
    // {"11:22:33:44:55:66", 24.513726f, 118.096211f},   // 同名 SSID 的每台物理 AP 各占一行
};
