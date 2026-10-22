#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

// 05_i2c_bme280 —— 自费玩一套·首个外设传感器点亮
// ① I2C 总线扫描：以后 MPU6050(0x68)/INA219(0x40)/OLED(0x3C) 到货，都先跑本工程认地址、排查接线
// ② BME280 读数 + 真芯判别：BME280 芯片 ID=0x60；市面常见拿无湿度通道的 BMP280(ID=0x58) 冒充
// 接线（母对母杜邦线 4 根）：VCC→3V3，GND→GND，SCL→GPIO9，SDA→GPIO8（与 04 遥测节点共线）

#define PIN_SDA 8
#define PIN_SCL 9

Adafruit_BME280 bme;
bool bmeOk = false;
float alt0 = NAN;   // 开机零点：契约 alt 字段口径=相对上电零点的气压高度

// 开机扫描 0x01~0x7E：列出总线上所有器件地址（排查接线的第一手段）
static uint8_t scanBus() {
    Serial.println("[I2C] 扫描总线 (SDA=8, SCL=9) ...");
    uint8_t found = 0, bmeAddr = 0;
    for (uint8_t a = 1; a < 0x7F; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[I2C]   发现器件 0x%02X%s\n", a,
                          (a == 0x76 || a == 0x77) ? "  ← BME280 在这儿" : "");
            found++;
            if (!bmeAddr && (a == 0x76 || a == 0x77)) bmeAddr = a;
        }
    }
    if (!found) Serial.println("[I2C]   一个都没扫到 → 查三件事：VCC 是否插的 3V3、GND 是否共地、SDA/SCL 是否插反或未插实（改好后按板载 RST 重扫）");
    return bmeAddr;
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n================================================");
    Serial.println("🌡️ 05_i2c_bme280 — I2C 扫描 + BME280 读数");
    Serial.println("================================================");

    Wire.begin(PIN_SDA, PIN_SCL);

    uint8_t addr = scanBus();
    if (!addr) return;

    bmeOk = bme.begin(addr) && bme.sensorID() == 0x60;
    switch (bme.sensorID()) {
    case 0x60:
        Serial.printf("[BME] 地址 0x%02X 芯片 ID 0x60 → 正品 BME280（温/压/湿三合一），每 2s 报一次读数 ✔\n", addr);
        for (int i = 0; i < 8; i++) { alt0 += bme.readAltitude(1013.25f); delay(10); }   // 定开机零点
        alt0 /= 8.0f;
        Serial.println("");
        break;
    case 0x58:
        Serial.println("[BME] 芯片 ID 0x58 → 这是 BMP280（无湿度通道），拿它冒充 BME280 是常见坑 → 截图找店家退换 ✘");
        break;
    default:
        Serial.printf("[BME] 驱动握手失败，芯片 ID 0x%02X（拍照留证再排查）\n", bme.sensorID());
    }
}

void loop() {
    static uint32_t n = 0;
    if (!bmeOk) {
        static uint32_t lastHint = 0;
        if (millis() - lastHint > 30000) {
            lastHint = millis();
            Serial.println("[BME] 未就绪 → 按上方提示排查接线后，按板载 RST 重扫");
        }
        delay(100);
        return;
    }

    Serial.printf("#%06lu  T=%5.2f °C | P=%6.2f hPa | H=%5.1f %%RH | 气压高度=%6.1f m | 相对开机=%+5.1f m（契约 alt 口径）\n",
                  (unsigned long)++n,
                  bme.readTemperature(),
                  bme.readPressure() / 100.0f,   // Pa → hPa
                  bme.readHumidity(),
                  bme.readAltitude(1013.25f),    // 绝对值：标准大气压折算（厦门实际海拔±30m 漂）
                  bme.readAltitude(1013.25f) - alt0);   // 相对值：上电归零，爬楼/电梯变化直接可读
    delay(2000);
}
