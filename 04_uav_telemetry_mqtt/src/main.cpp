#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "config.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#error "未找到 wifi_secrets.h！请复制 wifi_secrets.example.h 为 wifi_secrets.h 并填写真实 WiFi 与 MQTT 配置。"
#endif

// 对应《实验平台执行任务书》T1.3：WiFi + MQTT 按契约 v1.0 上云。
// 契约权威文本：科研项目《遥测契约/遥测数据格式说明.md》（扯皮仲裁以它为准）。

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

// alt 实测来源：BME280 气压高度（契约 §1 映射 alt←BME280；GPS 到货后作失锁兜底交叉校验）
Adafruit_BME280 bme;
bool bmeOk = false;
float altBase = 0.0f;   // 上电零点基线：契约 §2 规定 alt 为"相对本节点上电零点"的气压高度

// —— MPU6050（T1.2）：裸寄存器驱动，自 06_i2c_sensor_suite 整体搬运 ——
// 不用库的原因：对国产兼容芯免疫（不卡 WHO_AM_I 校验），且 40 行看透"I2C 器件 = 寄存器读写"
#define MPU_PWR_MGMT  0x6B   // 上电处于睡眠态，写 0 唤醒
#define MPU_ACCEL_CFG 0x1C   // 写 0 = 量程 ±2g
#define MPU_GYRO_CFG  0x1B   // 写 0 = 量程 ±250°/s
#define MPU_DATA      0x3B   // 测量数据首地址：ax ay az temp gx gy gz（大端 16 位 ×7）

Adafruit_SSD1306 oled(128, 64, &Wire, -1);   // 本机状态屏（不上云，仅现场排障用）
bool    mpuOk = false, oledOk = false;
uint8_t mpuAddr = 0;
float   lastRoll = NAN, lastPitch = NAN, lastAlt = NAN;   // 传感器缓存：loop 2Hz 刷新，上报取用

static bool mpuWrite(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(mpuAddr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

// 一次连续读 14 字节拿全部 7 个量（拆开读会前后不同步）
static bool mpuRead14(int16_t out[7]) {
    Wire.beginTransmission(mpuAddr);
    Wire.write(MPU_DATA);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(mpuAddr, (uint8_t)14) != 14) return false;
    uint8_t buf[14];
    for (uint8_t i = 0; i < 14; i++) buf[i] = Wire.read();
    for (uint8_t i = 0; i < 7; i++) out[i] = (int16_t)((buf[2 * i] << 8) | buf[2 * i + 1]);
    return true;
}

uint32_t seq = 0;   // 契约 §2：每采样 +1，断网续采也不重置，计满回绕归零

// 板载 WS2812 柔光指示（AGENTS.md §3.2 红线：RGB 各分量严禁超 10，常态 1~4）
#ifndef RGB_BUILTIN
#define RGB_BUILTIN 48
#endif
static void led(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(RGB_BUILTIN, r, g, b); }

// NTP 对上之后系统钟才是有效 Unix 时间（门槛取 2023-11 之后任意时刻）
static bool timeSynced() { return time(nullptr) > 1700000000; }

static void ensureWiFi() {
    static uint32_t lastTry = 0;
    // 间隔设为 15s，避免打断 ESP32 底层自动重连 (setAutoReconnect) 的握手进程
    if (millis() - lastTry < 15000) return;
    lastTry = millis();
    Serial.printf("[WiFi] 未连接（%s），正在发起重连 ...\n", WIFI_SSID);
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

static void ensureMQTT() {
    static uint32_t lastTry = 0;
    if (millis() - lastTry < 5000) return;
    lastTry = millis();
    if (mqtt.connect(NODE_ID)) {
        Serial.printf("[MQTT] 已连接 %s:%d\n", MQTT_HOST, MQTT_PORT);
    } else {
        Serial.printf("[MQTT] 连接失败 rc=%d，5s 后重试\n", mqtt.state());
    }
}

// 组包并发布一条契约 v1.0 报文：14 键齐全、紧凑 JSON、≤400 字节
static bool publishTelemetry() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t tsMs = (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000;   // 采样时刻（NTP 对时）

    JsonDocument doc;   // ArduinoJson v7
    doc["uas_id"] = NODE_ID;
    doc["seq"]    = ++seq;
    doc["ts"]     = tsMs;

    // —— 姿态角（T1.2 实测）：roll/pitch = 加速度计静止姿态角；yaw 需陀螺仪积分+滤波（会漂），
    //    滤波落地（毕设 #4 正戏）前按契约报 null，禁 0 冒充 ——
    if (!isnan(lastRoll)) { doc["roll"] = lastRoll; doc["pitch"] = lastPitch; }
    else                  { doc["roll"] = nullptr;  doc["pitch"] = nullptr; }
    doc["yaw"] = nullptr;
    doc["lat"]    = nullptr;   // 无 GPS（NEO-7M 未到）→ null；契约禁止用 0 冒充
    doc["lon"]    = nullptr;
    if (bmeOk) { lastAlt = bme.readAltitude(SEALEVEL_HPA) - altBase; doc["alt"] = lastAlt; }   // 契约 §2：相对上电零点
    else       { lastAlt = NAN; doc["alt"] = nullptr; }                                        // 未就绪 → null，不冒充
    doc["vbat"]   = nullptr;   // 无 INA219 → null
    doc["ibat"]   = nullptr;
    doc["rssi"]   = WiFi.RSSI();      // 有真实射频照实上报，弱网实验的环境变量记录
    doc["mode"]   = "NORMAL";         // 契约 §2：mode/fw 不可空（S2 断连补传才引入 BUFFERING/DEGRADED）
    doc["fw"]     = FW_VERSION;

    char buf[400];
    serializeJson(doc, buf, sizeof(buf));
    // PubSubClient 固定 QoS0 发布（契约 §7 已注记：丢包靠 seq 缺口度量，格式不受影响）
    return mqtt.publish(TOPIC_TELEM, buf);
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n================================================");
    Serial.printf("🛰️  %s 契约 v1.0 遥测节点  fw=%s\n", NODE_ID, FW_VERSION);
    Serial.println("================================================");

    // —— BME280 上电自举：0x76/0x77 两地址都试；失败不阻塞遥测，alt 回退 null ——
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    if (bme.begin(0x76) || bme.begin(0x77)) {
        bmeOk = (bme.sensorID() == 0x60);
        if (bmeOk) {
            // 契约 §2：alt 相对上电零点——开机连采 8 次取均值定基线
            float s = 0.0f;
            for (int i = 0; i < 8; i++) { s += bme.readAltitude(SEALEVEL_HPA); delay(10); }
            altBase = s / 8.0f;
        }
        Serial.printf("[BME] %s（芯片 ID 0x%02X），alt 按实测上报\n",
                      bmeOk ? "就绪" : "在线但不是 BME280", bme.sensorID());
    } else {
        Serial.println("[BME] 未找到（未接线或未上电）→ alt 按无传感器上报 null");
    }

    // —— MPU6050 自举（T1.2）：唤醒 + ±2g/±250dps 量程；失败不阻塞遥测，姿态角回退 null ——
    for (uint8_t a : {(uint8_t)0x68, (uint8_t)0x69}) {
        mpuAddr = a;
        if (mpuWrite(MPU_PWR_MGMT, 0x00) && mpuWrite(MPU_ACCEL_CFG, 0x00) && mpuWrite(MPU_GYRO_CFG, 0x00)) {
            int16_t d[7];
            if (mpuRead14(d)) { mpuOk = true; break; }
        }
    }
    Serial.println(mpuOk ? "[MPU] 就绪（T1.2 姿态角实测接入）" : "[MPU] 未找到 → roll/pitch 按 null 上报");

    // —— OLED 本机状态屏：仅现场排障显示，不上云；缺席不影响遥测 ——
    for (uint8_t a : {(uint8_t)0x3C, (uint8_t)0x3D}) {
        if (oled.begin(SSD1306_SWITCHCAPVCC, a)) { oledOk = true; break; }
    }
    if (oledOk) {
        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setTextColor(SSD1306_WHITE);
        oled.setCursor(0, 0);
        oled.print("UAV-LAB-01\nfw " FW_VERSION "\nT1.2 MPU+BME+OLED");
        oled.display();
        Serial.println("[OLED] 状态屏就绪");
    } else Serial.println("[OLED] 未找到 → 无状态屏，遥测不受影响");

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);                 // 关调制解调省电，降低遥测抖动
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setBufferSize(512);

    configTime(0, 0, "ntp1.aliyun.com", "pool.ntp.org");  // 系统钟存 UTC，ts 取真正 Unix 纪元
    setenv("TZ", "CST-8", 1); tzset();                    // 只影响本地显示，不改 time(nullptr)
}

// OLED 本机状态屏：ASCII 专用（GFX 默认字库不含中文）
static void renderStatus() {
    if (!oledOk) return;
    char l[24];
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    snprintf(l, sizeof(l), "%s #%06lu", NODE_ID, (unsigned long)seq);
    oled.print(l);
    oled.setCursor(0, 16);
    if (WiFi.status() != WL_CONNECTED) oled.print("WiFi connecting...");
    else {
        snprintf(l, sizeof(l), "WiFi %d dBm MQTT%s", WiFi.RSSI(), mqtt.connected() ? " OK" : " --");
        oled.print(l);
    }
    oled.setCursor(0, 32);
    if (!timeSynced()) oled.print("NTP syncing...");
    else if (!isnan(lastRoll)) { snprintf(l, sizeof(l), "R %+5.1f P %+5.1f", lastRoll, lastPitch); oled.print(l); }
    else oled.print("R --   P --");
    oled.setCursor(0, 48);
    if (isnan(lastAlt)) snprintf(l, sizeof(l), "alt ----  fw %s", FW_VERSION);
    else                snprintf(l, sizeof(l), "alt %+5.1f fw %s", lastAlt, FW_VERSION);
    oled.print(l);
    oled.display();
}

void loop() {
    static uint32_t lastPub = 0, lastSense = 0;
    mqtt.loop();

    // 2Hz 传感快读 + 状态屏刷新（与 1Hz 上报解耦）
    if (millis() - lastSense >= 500) {
        lastSense = millis();
        int16_t d[7];
        if (mpuOk && mpuRead14(d)) {
            float ax = d[0] / 16384.0f, ay = d[1] / 16384.0f, az = d[2] / 16384.0f;   // ±2g → g
            lastRoll  = atan2f(ay, az) * RAD_TO_DEG;
            lastPitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;
        }
        renderStatus();
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (!mqtt.connected()) ensureMQTT();

        if (millis() - lastPub >= (uint32_t)(1000 / PUB_HZ)) {
            lastPub = millis();
            if (!timeSynced()) {
                Serial.println("[NTP] 尚未对时，暂缓发布（契约要求 ts 为有效 Unix 毫秒）");
            } else if (publishTelemetry()) {
                Serial.printf("[遥测 #%06lu] seq=%lu 已发布 | rssi=%d dBm | 空闲堆 %lu KB\n",
                              (unsigned long)seq, (unsigned long)seq,
                              WiFi.RSSI(), (unsigned long)(ESP.getFreeHeap() / 1024));
                led(1, 3, 2);           // 薄荷青：本轮发布成功
            } else {
                Serial.println("[MQTT] publish 失败，下一轮重连重试");
            }
        }
    } else {
        ensureWiFi();
        led(3, 1, 1);                   // 淡粉：链路未就绪
    }
    delay(50);
}
