#include "HardwareManager.h"

#include <RTClib.h>
#include <Wire.h>
#include <SPI.h>

namespace {
constexpr uint8_t kI2cSdaPin = 8;
constexpr uint8_t kI2cSclPin = 9;
constexpr uint8_t kSpiSckPin = 12;
constexpr uint8_t kSpiMosiPin = 11;
constexpr uint8_t kCardKbAddress = 0x5F;
constexpr uint8_t kBuzzerPin = 4;

RTC_DS3231 gRtc;

bool detectI2cDevice(uint8_t address) {
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();
    return error == 0;
}

void initBuzzerPin() {
    pinMode(kBuzzerPin, OUTPUT);
    digitalWrite(kBuzzerPin, LOW);
}

void beepPattern(uint8_t beeps) {
    const uint8_t count = beeps == 0 ? 1 : beeps;
    for (uint8_t i = 0; i < count; ++i) {
        digitalWrite(kBuzzerPin, HIGH);
        delay(70);
        digitalWrite(kBuzzerPin, LOW);
        delay(90);
    }
}
}  // namespace

bool initHardware(SystemState &state, Stream &out) {
    if (!Wire.setPins(kI2cSdaPin, kI2cSclPin)) {
        state.i2cReady = false;
        out.println("I2C: setPins failed");
    } else {
        state.i2cReady = true;
    }

    // Initialize SPI bus for displays (Shared SCK=12, MOSI=11)
    SPI.begin(kSpiSckPin, -1, kSpiMosiPin);
    out.println("SPI: ready (SCK=12, MOSI=11)");

    // RTClib internally starts Wire bus. We avoid explicit Wire.begin() here,
    // so startup log is clean and the bus is initialized exactly once.
    state.rtcReady = gRtc.begin();
    state.cardKbReady = detectI2cDevice(kCardKbAddress);

    initBuzzerPin();
    state.buzzerReady = true;

    out.println("--- Hardware init ---");
    printHardwareInfo(state, out);
    if (!state.cardKbReady) {
        out.println("CardKB: not detected on 0x5F");
    }
    if (!state.rtcReady) {
        out.println("RTC: DS3231 not detected");
    }

    return state.i2cReady;
}

void printHardwareInfo(const SystemState &state, Stream &out) {
    out.print("i2c: ");
    out.println(state.i2cReady ? "ready" : "not ready");

    out.print("cardkb: ");
    out.println(state.cardKbReady ? "ready" : "not ready");

    out.print("rtc: ");
    out.println(state.rtcReady ? "ready" : "not ready");

    out.print("buzzer: ");
    out.println(state.buzzerReady ? "ready" : "not ready");
}

void scanI2cBus(Stream &out) {
    out.println("--- I2C scan ---");

    uint8_t found = 0;
    for (uint8_t address = 1; address < 127; ++address) {
        Wire.beginTransmission(address);
        const uint8_t error = Wire.endTransmission();
        if (error == 0) {
            out.print("found: 0x");
            if (address < 16) {
                out.print('0');
            }
            out.println(address, HEX);
            ++found;
        }
    }

    out.print("devices: ");
    out.println(found);
}

void testBuzzer(const SystemState &state, Stream &out, uint8_t beeps) {
    if (!state.buzzerReady) {
        out.println("Buzzer not ready");
        return;
    }

    out.print("Buzzer test: ");
    out.print(beeps == 0 ? 1 : beeps);
    out.println(" beeps");
    beepPattern(beeps);
}

bool tryReadCardKb(SystemState &state, char &ch) {
    if (!state.cardKbReady || !state.i2cMutex) {
        return false;
    }

    bool success = false;
    if (xSemaphoreTake(state.i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        Wire.requestFrom(static_cast<int>(kCardKbAddress), 1);
        if (Wire.available() > 0) {
            const uint8_t value = Wire.read();
            if (value != 0) {
                ch = static_cast<char>(value);
                success = true;
            }
        }
        xSemaphoreGive(state.i2cMutex);
    }
    return success;
}

bool printRtcNow(const SystemState &state, Stream &out) {
    if (!state.rtcReady || !state.i2cMutex) {
        out.println("RTC not ready");
        return false;
    }

    if (xSemaphoreTake(state.i2cMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        const DateTime now = gRtc.now();
        char timestamp[24];
        snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d",
                 now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());

        out.print("rtc now: ");
        out.println(timestamp);

        out.print("rtc temp C: ");
        out.println(gRtc.getTemperature(), 2);
        xSemaphoreGive(state.i2cMutex);
        return true;
    }
    return false;
}
