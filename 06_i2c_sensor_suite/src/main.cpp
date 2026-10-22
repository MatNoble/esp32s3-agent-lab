#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// 06_i2c_sensor_suite —— 三传感器共线首发：BME280 + MPU6050 + OLED 同挂一条 I2C
// 目的：自费玩一套的核心预演——"一条总线挂全部传感器"就是 04 遥测节点的最终形态
//       （INA219 到货后挂上第 4 个地址 0x40，即为完整四件套）
// 驱动策略：BME280/OLED 用 Adafruit 库（温度补偿/显存刷新太复杂不自研）；
//           MPU6050 手写裸寄存器驱动——40 行看透"I2C 器件 = 寄存器读写"的本质，
//           且对国产兼容芯免疫（不卡 WHO_AM_I 校验）
// 接线：全部按模块丝印对号入座——VCC→3V3、GND→GND、SCL→GPIO9、SDA→GPIO8（面包板共线）

#define PIN_SDA 8
#define PIN_SCL 9

Adafruit_BME280  bme;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);   // 第 4 参 reset=-1：I2C 小屏无复位脚

bool    bmeOk = false, mpuOk = false, oledOk = false;
uint8_t bmeAddr = 0, mpuAddr = 0, oledAddr = 0;
uint8_t found[16], nFound = 0;

// ———— MPU6050 裸寄存器层（寄存器地址出自其数据手册） ————
#define MPU_PWR_MGMT 0x6B   // 上电处于睡眠态，写 0 唤醒
#define MPU_ACCEL_CFG 0x1C  // 写 0 = 量程 ±2g
#define MPU_GYRO_CFG  0x1B  // 写 0 = 量程 ±250°/s
#define MPU_DATA      0x3B  // 测量数据首地址：ax ay az temp gx gy gz（大端 16 位 ×7）
#define MPU_WHO_AM_I  0x75

static bool mpuWrite(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(mpuAddr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static uint8_t mpuReadReg(uint8_t reg) {
    Wire.beginTransmission(mpuAddr);
    Wire.write(reg);
    Wire.endTransmission(false);                 // repeated-start：读完不断开
    Wire.requestFrom(mpuAddr, (uint8_t)1);
    return Wire.read();
}

// 一次连续读 14 字节拿全部 7 个量（数据手册规定必须连续读，拆开读会前后不同步）
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

// ———— 总线扫描 ————
static void scanBus() {
    Serial.printf("[I2C] 扫描总线 (SDA=%d, SCL=%d) ...\n", PIN_SDA, PIN_SCL);
    for (uint8_t a = 1; a < 0x7F; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() != 0) continue;
        found[nFound++] = a;
        const char *tag = "";
        if (a == 0x68) tag = "  ← MPU6050";
        if (a == 0x69) tag = "  ← MPU6050（AD0 拉高版）";
        if (a == 0x76 || a == 0x77) tag = "  ← BME280";
        if (a == 0x3C) tag = "  ← OLED";
        if (a == 0x3D) tag = "  ← OLED（备选地址）";
        Serial.printf("[I2C]   发现器件 0x%02X%s\n", a, tag);
    }
    if (!nFound) Serial.println("[I2C]   一个都没扫到 → 查 3V3/GND/SDA/SCL 四线丝印是否都插对插实");
    else {
        Serial.printf("[I2C] 共 %d 个器件在线\n", nFound);
        for (uint8_t i = 0; i < nFound; i++) {
            if (!mpuAddr && (found[i] == 0x68 || found[i] == 0x69)) mpuAddr = found[i];
            if (!bmeAddr && (found[i] == 0x76 || found[i] == 0x77)) bmeAddr = found[i];
            if (!oledAddr && (found[i] == 0x3C || found[i] == 0x3D)) oledAddr = found[i];
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n================================================");
    Serial.println("🧩 06_i2c_sensor_suite — BME280 + MPU6050 + OLED 共线首发");
    Serial.println("================================================");

    Wire.begin(PIN_SDA, PIN_SCL);
    scanBus();

    // MPU6050：唤醒 + 量程配置 + 试读一次
    if (mpuAddr) {
        mpuOk = mpuWrite(MPU_PWR_MGMT, 0x00) && mpuWrite(MPU_ACCEL_CFG, 0x00) &&
                mpuWrite(MPU_GYRO_CFG, 0x00) && [&] { int16_t d[7]; return mpuRead14(d); }();
        Serial.printf("[MPU] 0x%02X %s（WHO_AM_I=0x%02X；非 0x68 属国产兼容芯，照常使用）\n",
                      mpuAddr, mpuOk ? "就绪" : "应答但读数失败", mpuReadReg(MPU_WHO_AM_I));
    } else Serial.println("[MPU] 未上总线（GY-521 只接 VCC/GND/SCL/SDA 四脚，XDA/XCL/AD0/INT 悬空）");

    // BME280：0x60=正品判定，同 05 工程
    if (bmeAddr) {
        bmeOk = bme.begin(bmeAddr);
        if (bme.sensorID() == 0x58) { bmeOk = false; Serial.println("[BME] 芯片 ID 0x58 → BMP280 假芯，找店家退换 ✘"); }
        else Serial.printf("[BME] 0x%02X %s（ID 0x%02X）\n", bmeAddr, bmeOk ? "就绪" : "握手失败", bme.sensorID());
    } else Serial.println("[BME] 未上总线");

    // OLED：初始化并打欢迎屏
    if (oledAddr) {
        oledOk = oled.begin(SSD1306_SWITCHCAPVCC, oledAddr);
        if (oledOk) {
            oled.clearDisplay();
            oled.setTextSize(1);
            oled.setTextColor(SSD1306_WHITE);
            oled.setCursor(0, 0);
            oled.print("UAV-LAB-01  06-suite\nI2C first light!");
            oled.display();
            Serial.printf("[OLED] 0x%02X 就绪，屏上应已出现欢迎行\n", oledAddr);
        } else Serial.println("[OLED] 初始化失败（地址在扫但不响应）→ 拍屏背面丝印核对地址");
    } else Serial.println("[OLED] 未上总线");

    Serial.printf("\n%s\n", (mpuOk + bmeOk + oledOk) == 3
        ? "✅ 三器件共线全通——“一条总线挂全部传感器”架构预演达成"
        : "⚠️ 有器件缺席，按上方提示排查后按板载 RST 重扫");
}

void loop() {
    static uint32_t n = 0, lastBme = 0;
    static float tc = 0, hpa = 0, rh = 0;

    // BME280 每 2s 刷一次（气压温度补偿读取耗时，不必每轮读）
    if (bmeOk && millis() - lastBme > 2000) {
        lastBme = millis();
        tc = bme.readTemperature();
        hpa = bme.readPressure() / 100.0f;
        rh = bme.readHumidity();
    }

    int16_t d[7];
    bool haveMpu = mpuOk && mpuRead14(d);

    float roll = 0, pitch = 0, gzdps = 0;
    if (haveMpu) {
        float ax = d[0] / 16384.0f, ay = d[1] / 16384.0f, az = d[2] / 16384.0f;  // ±2g 量程 → g
        gzdps = d[12] / 131.0f;                                                          // ±250 → °/s
        // 加速度计姿态角（静止时准；运动/振动需滤波——那是 T1.2 与毕设 #4 的正戏）
        roll  = atan2f(ay, az) * RAD_TO_DEG;
        pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;
    }

    Serial.printf("#%05lu %s| T %5.1fC P %6.1fhPa H %4.1f%%\n", (unsigned long)++n,
                  haveMpu ? "" : "MPU-- ",
                  tc, hpa, rh);
    if (haveMpu)
        Serial.printf("       Roll %+6.1f  Pitch %+6.1f  gz %+6.1f d/s\n", roll, pitch, gzdps);

    if (oledOk) {
        char l[24];
        oled.clearDisplay();
        oled.setCursor(0, 0);
        snprintf(l, sizeof(l), "I2C x%d: %02X %02X %02X", nFound,
                 nFound > 0 ? found[0] : 0, nFound > 1 ? found[1] : 0, nFound > 2 ? found[2] : 0);
        oled.print(l);
        oled.setCursor(0, 16);
        snprintf(l, sizeof(l), "%4.1fC %5.1fh %2.0f%%", tc, hpa, rh);
        oled.print(l);
        oled.setCursor(0, 32);
        snprintf(l, sizeof(l), "R %+5.1f P %+5.1f", roll, pitch);
        oled.print(l);
        oled.setCursor(0, 48);
        snprintf(l, sizeof(l), "gz %+5.1f d/s %s", gzdps, haveMpu ? "" : "MPU--");
        oled.print(l);
        oled.display();
    }

    delay(500);
}
