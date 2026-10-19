#include <Arduino.h>
#include "esp_chip_info.h"

// 兼容 ESP32-S3 核心板常见板载 RGB / LED (GPIO 48 或 GPIO 38)
#ifndef RGB_BUILTIN
#define RGB_BUILTIN 48
#endif

uint32_t counter = 0;

void setup() {
    // 硬件串口 UART0 (直连板载 CH343 芯片)
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n==============================================");
    Serial.println("🚀 Welcome to ESP32-S3 (Hello World Initializer)!");
    Serial.println("==============================================");

    // 1. 打印芯片基础体检信息
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    Serial.printf("📌 芯片型号: ESP32-S3 (Revision v%d.%d)\n", 
                  chip_info.revision / 100, chip_info.revision % 100);
    Serial.printf("⚡ CPU 核心数: %d 核心, 当前运行主频: %lu MHz\n", 
                  chip_info.cores, (unsigned long)getCpuFrequencyMhz());
    Serial.printf("📡 芯片功能支持: %s %s\n", 
                  (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "[WiFi 2.4G]" : "",
                  (chip_info.features & CHIP_FEATURE_BLE) ? "[BLE 5.0]" : "");

    // 2. 内存情况 (SRAM 与 PSRAM)
    Serial.printf("💾 内部 SRAM 空闲: %lu KB / 总计: %lu KB\n", 
                  (unsigned long)(ESP.getFreeHeap() / 1024), 
                  (unsigned long)(ESP.getHeapSize() / 1024));

    if (psramFound()) {
        Serial.printf("🚀 外部 PSRAM 挂载成功: %lu MB 空闲 / 总计: %lu MB\n", 
                      (unsigned long)(ESP.getFreePsram() / (1024 * 1024)), 
                      (unsigned long)(ESP.getPsramSize() / (1024 * 1024)));
    } else {
        Serial.println("💡 PSRAM: 硬件内置 8MB (当前以基础模式运行)");
    }

    // 3. 芯片 Flash 容量
    Serial.printf("📦 板载 Flash 容量: %lu MB (运行速度: %lu MHz)\n", 
                  (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)), 
                  (unsigned long)(ESP.getFlashChipSpeed() / 1000000));

    // 4. 读取内置温度传感器 (无需外置传感器)
    Serial.printf("🌡️ 芯片内部裸晶温度: %.2f °C\n", temperatureRead());
    Serial.println("----------------------------------------------");
    Serial.println("💡 状态灯已启动呼吸闪烁 (RGB/LED 测试中)");
    Serial.println("==============================================\n");
}

void loop() {
    counter++;

    // 板载指示灯低饱和柔和微光闪烁（低亮度 + 混合暖色调，护眼不刺眼）
    if (counter % 2 == 0) {
        rgbLedWrite(RGB_BUILTIN, 3, 1, 1); // 柔和淡粉暖光 (亮度仅 3，直视不刺眼)
    } else {
        rgbLedWrite(RGB_BUILTIN, 1, 3, 2); // 柔和淡薄荷青光 (低饱和护眼)
    }


    // 每一秒打印一次实时心跳状态
    Serial.printf("[心跳 #%04lu] 运行时间: %lu s | 芯片内部温度: %.1f °C | 空闲内存: %lu KB\n",
                  (unsigned long)counter, 
                  (unsigned long)(millis() / 1000), 
                  temperatureRead(), 
                  (unsigned long)(ESP.getFreeHeap() / 1024));

    delay(1000);
}
