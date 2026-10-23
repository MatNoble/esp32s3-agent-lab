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
#include <Preferences.h>   // NVS：连接类配置存 flash 分区，改网不再重烧（重烧固件也不丢）
#include <WebServer.h>     // SoftAP 配网门户
#include <DNSServer.h>     // 配网热点的 captive portal DNS
#include <ESPmDNS.h>       // 解析 pi-matnoble.local 这类 .local 主机名

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
#define MPU_DLPF      0x1A   // 数字低通：写 3 = 加速度 44Hz / 陀螺 42Hz（默认 0=关闭，桌面微振动全透）
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

// —— 运行期配置 + SoftAP 配网（fw 0.7.0）：换 WiFi / 换 broker 不再需要重烧 ——
// 连接类配置存 NVS（flash 分区，普通重烧固件不丢）；NVS 为空时用 wifi_secrets.h 出厂值播种。
// WiFi 连不上且开机 45s 后自动开配网热点 "UAV-LAB-01-Setup"（无密码）；手机连上自动弹配网页
// （或浏览器访问 http://192.168.4.1/），保存后设备自动重启。开机时按住 BOOT 键可无条件强制进入。
// broker 地址支持 IP 或 mDNS 主机名（本实验室 Pi = pi-matnoble.local）。
Preferences prefs;
struct NodeCfg { String ssid, pass, mqttHost; uint16_t mqttPort; } cfg;
static bool portalOn = false, forcePortal = false;
static WebServer *portal = nullptr;
static DNSServer  *portalDns = nullptr;

static void cfgSave() {
    prefs.putString("ssid", cfg.ssid);
    prefs.putString("pass", cfg.pass);
    prefs.putString("mhost", cfg.mqttHost);
    prefs.putUShort("mport", cfg.mqttPort);
}

static void cfgLoad() {
    cfg.mqttHost = prefs.getString("mhost", MQTT_HOST);
    cfg.mqttPort = prefs.getUShort("mport", MQTT_PORT);
    cfg.ssid     = prefs.getString("ssid", "");
    cfg.pass     = prefs.getString("pass", "");
    if (cfg.ssid.isEmpty()) {   // 首次上电（或整片擦除后）：出厂默认播种并落 NVS
        cfg.ssid = WIFI_SSID;
        cfg.pass = WIFI_PASS;
        cfgSave();
        Serial.println("[CFG] NVS 为空 → 已按出厂默认播种（wifi_secrets.h）");
    }
}

static void handleSave() {
    String s = portal->arg("s"), p = portal->arg("p"), h = portal->arg("h"), o = portal->arg("o");
    if (!s.length()) { portal->send(400, "text/plain; charset=utf-8", "WiFi 名称不能为空"); return; }
    cfg.ssid = s;
    if (p.length()) cfg.pass = p;                  // 密码留空 = 沿用已存值
    if (h.length()) cfg.mqttHost = h;
    cfg.mqttPort = (uint16_t)o.toInt();
    if (cfg.mqttPort == 0) cfg.mqttPort = 1883;
    cfgSave();
    portal->send(200, "text/html; charset=utf-8",
                 "<!doctype html><meta charset=utf-8>"
                 "<meta name=viewport content='width=device-width,initial-scale=1'>"
                 "<body style='font-family:system-ui;text-align:center;padding-top:3em'>"
                 "<h3>已保存 ✓</h3><p>设备正在重启并连接新网络……<br>此热点即将消失。</p></body>");
    delay(1500);
    ESP.restart();
}

static String scanOptionsHtml() {
    String out;
    int n = WiFi.scanComplete();
    for (int i = 0; i < n && i < 15; i++) {   // 按信号强度给前 15 个候选
        String s = WiFi.SSID(i);
        if (s.length()) out += "<option value=\"" + s + "\">" + s + "（" + String(WiFi.RSSI(i)) + " dBm）";
    }
    return out;
}

static void handleRoot() {
    if (WiFi.scanComplete() == -2) WiFi.scanNetworks(true);   // 尚未扫过 → 异步扫，刷新页面即见候选
    String page = String("<!doctype html><meta charset=utf-8>") +
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>" NODE_ID " 配网</title>"
        "<body style='font-family:system-ui;max-width:26em;margin:2em auto;padding:0 1em'>"
        "<h3>" NODE_ID " 网络配置</h3>"
        "<p style='color:#666;margin-top:-8px'>fw " FW_VERSION " · 保存后自动重启</p>"
        "<form method=POST action=/save style='display:grid;gap:14px'>"
        "WiFi 名称（可点选扫描结果）<input name=s list=ss required style='padding:8px;font-size:15px'>"
        "<datalist id=ss>" + scanOptionsHtml() + "</datalist>"
        "WiFi 密码（留空 = 沿用已存）<input name=p type=password style='padding:8px;font-size:15px'>"
        "MQTT broker（主机名或 IP）<input name=h value='" + cfg.mqttHost + "' style='padding:8px'>"
        "MQTT 端口<input name=o value='" + String(cfg.mqttPort) + "' style='padding:8px'>"
        "<button style='padding:10px;font-size:15px'>保存并重启</button></form></body>";
    portal->send(200, "text/html; charset=utf-8", page);
}

static void portalStart() {
    if (portalOn) return;
    portalOn = true;
    WiFi.mode(WIFI_AP_STA);                       // AP 与 STA 并存：配网同时继续尝试原配置
    WiFi.softAP("UAV-LAB-01-Setup");
    portalDns = new DNSServer();
    portalDns->start(53, "*", WiFi.softAPIP());   // 泛域名解析 → 手机连上自动弹配网页
    portal = new WebServer(80);
    portal->on("/", handleRoot);
    portal->on("/save", handleSave);
    portal->onNotFound([] {
        portal->sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
        portal->send(302, "text/plain", "");
    });
    portal->begin();
    WiFi.scanNetworks(true);
    Serial.printf("[PORTAL] 配网热点已开：连 \"UAV-LAB-01-Setup\" → 自动弹页，或浏览器访问 http://%s/\n",
                  WiFi.softAPIP().toString().c_str());
}

static void portalLoop() {
    if (!portalOn) return;
    portalDns->processNextRequest();
    portal->handleClient();
}

static void portalStop() {
    if (!portalOn) return;
    portalOn = false;
    portal->stop();
    portalDns->stop();
    delete portal;
    delete portalDns;
    portal = nullptr;
    portalDns = nullptr;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);            // 关 AP 口、保留 STA 连接
    Serial.println("[PORTAL] 网络已通，配网热点关闭");
}

// broker 地址解析：纯 IP 直接用；.local 主机名走 mDNS 查询（Pi 的 avahi 应答），结果缓存 60s
static IPAddress brokerIP;
static uint32_t brokerResolvedAt = 0;
static bool brokerResolve() {
    if (brokerIP && millis() - brokerResolvedAt < 60000) return true;
    if (brokerIP.fromString(cfg.mqttHost)) { brokerResolvedAt = millis(); return true; }
    String name = cfg.mqttHost;
    if (name.endsWith(".local")) name.remove(name.length() - 6);
    char nm[64];
    name.toCharArray(nm, sizeof(nm));
    IPAddress ip = MDNS.queryHost(nm);
    if (!ip) {
        Serial.printf("[MQTT] mDNS 解析 %s 失败，5s 后重试\n", cfg.mqttHost.c_str());
        return false;
    }
    brokerIP = ip;
    brokerResolvedAt = millis();
    Serial.printf("[MQTT] %s = %s（mDNS 解析成功）\n", cfg.mqttHost.c_str(), ip.toString().c_str());
    return true;
}

static void ensureWiFi() {
    static uint32_t lastTry = 0;
    // 底层 setAutoReconnect 的关联握手进行中时绝不打断：disconnect() 会把进度清零，
    // 旧版 15s 一轮的 disconnect+begin 恰好是首连被拖到 45s 的元凶
    if (WiFi.status() == WL_IDLE_STATUS) return;
    if (millis() - lastTry < 30000) return;
    lastTry = millis();
    Serial.printf("[WiFi] 未连接（%s），重新发起连接 ...\n", cfg.ssid.c_str());
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());   // 不再 disconnect：对进行中的尝试是幂等的
}

static void ensureMQTT() {
    static uint32_t lastTry = 0;
    if (millis() - lastTry < 5000) return;
    lastTry = millis();
    if (!brokerResolve()) return;   // 解析失败不打断 5s 节流，下一轮再来
    mqtt.setServer(brokerIP, cfg.mqttPort);
    if (mqtt.connect(NODE_ID)) {
        Serial.printf("[MQTT] 已连接 %s:%u（%s）\n",
                      brokerIP.toString().c_str(), (unsigned)cfg.mqttPort, cfg.mqttHost.c_str());
    } else {
        Serial.printf("[MQTT] 连接失败 rc=%d，5s 后重试\n", mqtt.state());
    }
}

// —— WiFi 粗定位：查当前关联 AP 的 BSSID 是否在 config.h 的表里 ——
static bool wifiPosLookup(float *lat, float *lon) {
    String b = WiFi.BSSIDstr();
    for (auto &e : WIFI_POS_TABLE) {
        if (e.bssid[0] && b.equalsIgnoreCase(e.bssid)) { *lat = e.lat; *lon = e.lon; return true; }
    }
    return false;
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
    float wlat, wlon;
    if (wifiPosLookup(&wlat, &wlon)) { doc["lat"] = wlat; doc["lon"] = wlon; }   // WiFi 粗定位兜底（粒度=AP）
    else { doc["lat"] = nullptr; doc["lon"] = nullptr; }                          // 未知环境/无 GPS → null，不冒充
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
            // 芯片内置 IIR 滤波（默认关闭）：压掉 ±1Pa≈8.5cm 级采样噪声，室内气流/开门不再直接进曲线
            bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                            Adafruit_BME280::SAMPLING_X2,    // 温度
                            Adafruit_BME280::SAMPLING_X16,   // 气压（alt 来源，最高过采样）
                            Adafruit_BME280::SAMPLING_X1,    // 湿度（暂不上云）
                            Adafruit_BME280::FILTER_X16,     // 芯片内置 IIR
                            Adafruit_BME280::STANDBY_MS_0_5);
            // 契约 §2：alt 相对上电零点——开机连采 8 次取均值定基线（100ms 间隔让 IIR 先收敛）
            float s = 0.0f;
            for (int i = 0; i < 8; i++) { s += bme.readAltitude(SEALEVEL_HPA); delay(100); }
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
        if (mpuWrite(MPU_PWR_MGMT, 0x00) && mpuWrite(MPU_ACCEL_CFG, 0x00)
            && mpuWrite(MPU_GYRO_CFG, 0x00) && mpuWrite(MPU_DLPF, 0x03)) {
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
        oled.print("UAV-LAB-01\nfw " FW_VERSION "\nNVS cfg + portal");
        oled.display();
        Serial.println("[OLED] 状态屏就绪");
    } else Serial.println("[OLED] 未找到 → 无状态屏，遥测不受影响");

    // —— 运行期配置：NVS 优先，出厂值播种；BOOT 键按住上电 = 无条件强制配网 ——
    prefs.begin("uavcfg", false);
    cfgLoad();
    pinMode(0, INPUT_PULLUP);
    forcePortal = (digitalRead(0) == LOW);
    if (forcePortal) Serial.println("[BOOT] 检测到按住 BOOT 键 → 直接进入配网模式");

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);                 // 关调制解调省电，降低遥测抖动
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());

    mqtt.setBufferSize(512);              // broker 地址由 ensureMQTT 里解析后再 setServer

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
    if (portalOn) oled.print("AP Setup 192.168.4.1");
    else if (WiFi.status() != WL_CONNECTED) oled.print("WiFi connecting...");
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
    portalLoop();

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
        static bool mdnsUp = false;
        if (!mdnsUp) mdnsUp = MDNS.begin(NODE_ID);   // 接入网络后初始化 mDNS：查 .local + 通告本机名
        if (portalOn && !forcePortal) portalStop();  // 网络已通且非 BOOT 强制配网 → 收掉配网热点
        if (!mqtt.connected()) ensureMQTT();

        // WiFi 粗定位引导：每 30s 提示当前 AP 是否在表（采集 BSSID 就看这行日志；带 SSID 便于辨认归属）
        static uint32_t lastPosLog = 0;
        if (millis() - lastPosLog > 30000) {
            lastPosLog = millis();
            float la, lo;
            Serial.printf("[WiFiPOS] SSID \"%s\" · AP %s %s\n", WiFi.SSID().c_str(), WiFi.BSSIDstr().c_str(),
                          wifiPosLookup(&la, &lo) ? "命中粗定位表 → lat/lon 按表上报"
                                                  : "不在表 → lat/lon 报 null；把该 BSSID+坐标填入 config.h 即可上报粗定位");
        }

        if (millis() - lastPub >= (uint32_t)(1000 / PUB_HZ)) {
            lastPub = millis();
            if (!timeSynced()) {
                static uint32_t lastNtpLog = 0;   // 同一句每秒刷屏太吵，10s 一条
                if (millis() - lastNtpLog > 10000) {
                    lastNtpLog = millis();
                    Serial.println("[NTP] 尚未对时，暂缓发布（契约要求 ts 为有效 Unix 毫秒）");
                }
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
        // WiFi 一直连不上：开机 45s 后自动进配网（换网零烧录的入口；BOOT 键按住上电则立即进）
        if (!portalOn && (forcePortal || millis() > 45000)) portalStart();
        ensureWiFi();
        led(3, 1, 1);                   // 淡粉：链路未就绪
    }
    delay(50);
}
