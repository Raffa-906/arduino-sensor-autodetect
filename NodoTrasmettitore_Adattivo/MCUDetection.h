#pragma once
#include <Arduino.h>

// ============================================================
//  MCUDetection.h
//  Rilevamento automatico del microcontroller e delle sue
//  specifiche hardware (pin ADC, bus supportati, memoria, ecc.)
//
//  Questo consente al firmware di auto-configurarsi in base al
//  dispositivo su cui è caricato, senza richiedere intervento
//  dell'utente per ogni nuova board.
// ============================================================

enum class MCUType : uint8_t {
    ESP32,          // ESP32 classico (dual-core, 30+ GPIO ADC)
    ESP32_C3,       // ESP32-C3 (single-core, 4 ADC pins)
    ESP32_S3,       // ESP32-S3 (dual-core, 20 ADC pins)
    ESP32_C6,       // ESP32-C6 (single-core)
    ESP8266,        // ESP8266 (single-core, ADC singolo)
    RP2040,         // Raspberry Pi Pico (dual-core, 4 ADC)
    STM32F4xx,      // STM32 F4 series
    UNKNOWN
};

struct MCUProfile {
    MCUType type;
    const char* name;
    uint8_t adcBits;            // risoluzione ADC (8, 10, 12, 16)
    uint8_t maxAdcChannels;     // numero massimo di pin ADC
    uint8_t maxI2cBuses;        // numero di bus I2C disponibili
    bool supportsOneWire;       // ha pin GPIO liberi per 1-Wire
    bool supportsUART;          // supporta UART (Modbus, SDI-12)
    bool supportsDeepSleep;     // ha low-power mode
    uint32_t ramKB;             // RAM disponibile
    uint32_t flashKB;           // Flash disponibile
    float refVoltage;           // tensione di riferimento ADC (V)
};

class MCUDetector {
public:
    static MCUProfile detect() {
        MCUProfile profile = detectMCU();
        reportProfile(profile);
        return profile;
    }

private:
    static MCUProfile detectMCU() {
        MCUProfile p;
        p.refVoltage = 3.3f;  // default per maggior parte MCU

#ifdef CONFIG_IDF_FIRMWARE_CHIP_ID
        // ESP-IDF (ESP32 family)
        uint32_t chipId = ESP.getChipModel();
        
        if (chipId == CHIP_ESP32) {
            p.type = MCUType::ESP32;
            p.name = "ESP32 (Classic)";
            p.adcBits = 12;
            p.maxAdcChannels = 16;
            p.maxI2cBuses = 2;
            p.supportsOneWire = true;
            p.supportsUART = true;
            p.supportsDeepSleep = true;
            p.ramKB = 520;
            p.flashKB = 4096;
        }
        else if (chipId == CHIP_ESP32C3) {
            p.type = MCUType::ESP32_C3;
            p.name = "ESP32-C3";
            p.adcBits = 12;
            p.maxAdcChannels = 5;  // GPIO0-4
            p.maxI2cBuses = 1;
            p.supportsOneWire = true;
            p.supportsUART = true;
            p.supportsDeepSleep = true;
            p.ramKB = 400;
            p.flashKB = 4096;
        }
        else if (chipId == CHIP_ESP32S3) {
            p.type = MCUType::ESP32_S3;
            p.name = "ESP32-S3";
            p.adcBits = 12;
            p.maxAdcChannels = 20;
            p.maxI2cBuses = 2;
            p.supportsOneWire = true;
            p.supportsUART = true;
            p.supportsDeepSleep = true;
            p.ramKB = 512;
            p.flashKB = 8192;
        }
        else if (chipId == CHIP_ESP32C6) {
            p.type = MCUType::ESP32_C6;
            p.name = "ESP32-C6";
            p.adcBits = 12;
            p.maxAdcChannels = 7;
            p.maxI2cBuses = 1;
            p.supportsOneWire = true;
            p.supportsUART = true;
            p.supportsDeepSleep = true;
            p.ramKB = 512;
            p.flashKB = 4096;
        }
        else {
            p.type = MCUType::UNKNOWN;
            p.name = "ESP32 sconosciuto";
            p.adcBits = 12;
            p.maxAdcChannels = 8;
            p.maxI2cBuses = 1;
            p.supportsOneWire = true;
            p.supportsUART = true;
            p.supportsDeepSleep = true;
            p.ramKB = 320;
            p.flashKB = 4096;
        }
#elif defined(ESP8266)
        p.type = MCUType::ESP8266;
        p.name = "ESP8266";
        p.adcBits = 10;
        p.maxAdcChannels = 1;  // solo A0
        p.maxI2cBuses = 1;
        p.supportsOneWire = true;
        p.supportsUART = true;
        p.supportsDeepSleep = true;
        p.ramKB = 160;
        p.flashKB = 4096;
        p.refVoltage = 3.3f;
#elif defined(ARDUINO_ARCH_RP2040)
        p.type = MCUType::RP2040;
        p.name = "RP2040 (Pico)";
        p.adcBits = 12;
        p.maxAdcChannels = 4;  // ADC0-3 su GPIO26-29
        p.maxI2cBuses = 2;
        p.supportsOneWire = true;
        p.supportsUART = true;
        p.supportsDeepSleep = false;  // no built-in deep sleep
        p.ramKB = 264;
        p.flashKB = 2048;
        p.refVoltage = 3.3f;
#elif defined(STM32F4xx)
        p.type = MCUType::STM32F4xx;
        p.name = "STM32F4xx";
        p.adcBits = 12;
        p.maxAdcChannels = 19;
        p.maxI2cBuses = 3;
        p.supportsOneWire = true;
        p.supportsUART = true;
        p.supportsDeepSleep = true;
        p.ramKB = 192;
        p.flashKB = 512;
        p.refVoltage = 3.3f;
#else
        // Fallback: generico
        p.type = MCUType::UNKNOWN;
        p.name = "MCU sconosciuto";
        p.adcBits = 12;
        p.maxAdcChannels = 4;
        p.maxI2cBuses = 1;
        p.supportsOneWire = true;
        p.supportsUART = false;
        p.supportsDeepSleep = false;
        p.ramKB = 128;
        p.flashKB = 256;
        p.refVoltage = 3.3f;
#endif

        return p;
    }

    static void reportProfile(const MCUProfile &p) {
        Serial.println("\n╔════════════════════════════════════════╗");
        Serial.print("║  MCU: ");
        Serial.print(p.name);
        Serial.println();
        Serial.printf("║  ADC: %u-bit, %u canali\n", p.adcBits, p.maxAdcChannels);
        Serial.printf("║  I2C: %u bus | 1-Wire: %s\n", p.maxI2cBuses, p.supportsOneWire ? "✓" : "✗");
        Serial.printf("║  RAM: %u KB | Flash: %u KB\n", p.ramKB, p.flashKB);
        Serial.printf("║  Ref ADC: %.2f V | Deep Sleep: %s\n", p.refVoltage, p.supportsDeepSleep ? "✓" : "✗");
        Serial.println("╚════════════════════════════════════════╝\n");
    }
};
