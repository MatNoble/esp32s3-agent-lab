#include <Arduino.h>
#include "esp_chip_info.h"

#ifndef RGB_BUILTIN
#define RGB_BUILTIN 48
#endif

uint32_t counter = 0;

void setup() {
    // 原生 USB 关键配置：超时设为 0，防止无终端读取时阻塞 CPU
    Serial.setTxTimeoutMs(0);
    Serial.begin(115200);

    // 允许 USB 建立稳定连接
    delay(1000);

    Serial.println("\n==============================================");
    Serial.println("⚡ ESP32-S3 原生 USB (Native USB CDC) 已连接成功！");
    Serial.println("==============================================");
    Serial.println("📌 当前物理通道: 原生 USB JTAG/Serial Debug Unit (0x303a:0x1001)");
    Serial.printf("🌡️ 芯片内部裸晶温度: %.2f °C\n", temperatureRead());
    Serial.println("💡 状态指示灯: 开启蓝色呼吸测试 (专属原生 USB)");
    Serial.println("==============================================\n");
}

void loop() {
    counter++;

    // 专属原生 USB 模式：低饱和柔和淡冰蓝微光闪烁（低亮度，不刺眼）
    if (counter % 2 == 0) {
        rgbLedWrite(RGB_BUILTIN, 1, 2, 4); // 柔和淡冰蓝微光
    } else {
        rgbLedWrite(RGB_BUILTIN, 0, 0, 0); // 熄灭
    }


    // 通过原生 USB 向 Mac 持续发送心跳
    Serial.printf("[原生 USB 心跳 #%04lu] 运行时间: %lu s | 芯片内部温度: %.1f °C | 空闲内存: %lu KB\n",
                  (unsigned long)counter,
                  (unsigned long)(millis() / 1000),
                  temperatureRead(),
                  (unsigned long)(ESP.getFreeHeap() / 1024));

    delay(1000);
}
