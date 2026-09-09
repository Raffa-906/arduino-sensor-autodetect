#pragma once
#include <Arduino.h>
#include "MCUDetection.h"

// ============================================================
//  HardwareConfig.h
//  Livello di astrazione hardware: adatta pin e configurazioni
//  al MCU rilevato, senza richiedere cambio di codice da parte
//  dell'utente per diverse board.
//
//  Pattern: ogni MCU ha un profilo di pin "preferiti" per sensori
//  comuni, in ordine di priorità. Durante l'init, SensorManager
//  usa questi pin garantiti disponibili per il MCU corrente.
// ============================================================

struct PinProfile {
    uint8_t dhtPin;              // pin per DHT22 (fisso, non ADC)
    uint8_t batteryPin;          // pin per misurare batteria (ADC)
    const uint8_t* analogPins;   // array di pin ADC disponibili
    uint8_t analogPinCount;
    const uint8_t* analogIdPins; // chip ID 1-Wire dedicati (opzionale)
    uint8_t i2cSdaPin;           // I2C (se disponibile)
    uint8_t i2cSclPin;
    uint8_t oneWirePin;          // 1-Wire (se disponibile)
};

class HardwareConfig {
public:
    // Rileva il MCU e restituisce il profilo pin appropriato
    static PinProfile getPinProfile() {
        MCUProfile mcu = MCUDetector::detect();
        return selectPinProfile(mcu);
    }

    // Adatta la risoluzione ADC al MCU rilevato
    static void configureADC(const MCUProfile &mcu) {
#ifdef CONFIG_IDF_FIRMWARE_CHIP_ID
        // ESP32 family
        analogReadResolution(mcu.adcBits);
#elif defined(ARDUINO_ARCH_RP2040)
        // RP2040/Pico
        analogReadResolution(mcu.adcBits);
#elif defined(ESP8266)
        // ESP8266 ha ADC single-channel, non configurabile
        // ma legge comunque a 10-bit
#endif
        Serial.printf("[HW] ADC configurato a %u-bit\n", mcu.adcBits);
    }

    // Converte raw ADC a mV, considerando risoluzione e ref
    static uint32_t rawToMillivolts(const MCUProfile &mcu, uint16_t rawValue) {
        uint16_t maxVal = (1 << mcu.adcBits) - 1;  // 2^adcBits - 1
        return (uint32_t)rawValue * ((uint32_t)(mcu.refVoltage * 1000.0f)) / maxVal;
    }

private:
    // Tabella di profili pin per ogni MCU supportato
    static constexpr uint8_t ESP32_ANALOG_PINS[] = {36, 39, 34, 35, 32, 33};
    static constexpr uint8_t ESP32_C3_ANALOG_PINS[] = {0, 1, 2, 3, 4};
    static constexpr uint8_t ESP32_S3_ANALOG_PINS[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    static constexpr uint8_t PICO_ANALOG_PINS[] = {26, 27, 28, 29}; // GPIO26-29 sono ADC
    static constexpr uint8_t ESP8266_ANALOG_PINS[] = {A0};

    static PinProfile selectPinProfile(const MCUProfile &mcu) {
        PinProfile profile = {};

        switch (mcu.type) {
            case MCUType::ESP32: {
                // ESP32 classico: molti pin ADC, flessibile
                profile.dhtPin = 13;          // GPIO13 digitale libero
                profile.batteryPin = 36;      // ADC1_0 dedicato a batteria
                profile.analogPins = ESP32_ANALOG_PINS;
                profile.analogPinCount = sizeof(ESP32_ANALOG_PINS);
                profile.analogIdPins = nullptr;
                profile.i2cSdaPin = 21;       // I2C standard
                profile.i2cSclPin = 22;
                profile.oneWirePin = 14;      // GPIO14 libero per 1-Wire
                break;
            }
            case MCUType::ESP32_C3: {
                // ESP32-C3: vincoli ristretti (4 ADC su GPIO0-4)
                profile.dhtPin = 8;           // GPIO8 digitale (non ADC)
                profile.batteryPin = 3;       // GPIO3 ADC (vincolo tuo)
                profile.analogPins = ESP32_C3_ANALOG_PINS;
                profile.analogPinCount = sizeof(ESP32_C3_ANALOG_PINS);
                profile.analogIdPins = nullptr;
                profile.i2cSdaPin = 6;        // I2C su GPIO6/7
                profile.i2cSclPin = 7;
                profile.oneWirePin = 9;       // GPIO9 libero
                break;
            }
            case MCUType::ESP32_S3: {
                // ESP32-S3: molti pin ADC, simile a classico ma pin diversi
                profile.dhtPin = 10;          // GPIO10 digitale
                profile.batteryPin = 11;      // GPIO11 ADC
                profile.analogPins = ESP32_S3_ANALOG_PINS;
                profile.analogPinCount = sizeof(ESP32_S3_ANALOG_PINS);
                profile.analogIdPins = nullptr;
                profile.i2cSdaPin = 8;        // I2C su GPIO8/9
                profile.i2cSclPin = 9;
                profile.oneWirePin = 12;      // GPIO12 libero
                break;
            }
            case MCUType::RP2040: {
                // Raspberry Pi Pico: 4 ADC su GPIO26-29
                profile.dhtPin = 14;          // GPIO14 digitale
                profile.batteryPin = 26;      // GPIO26 ADC0
                profile.analogPins = PICO_ANALOG_PINS;
                profile.analogPinCount = sizeof(PICO_ANALOG_PINS);
                profile.analogIdPins = nullptr;
                profile.i2cSdaPin = 4;        // I2C0 su GPIO4/5
                profile.i2cSclPin = 5;
                profile.oneWirePin = 15;      // GPIO15 libero
                break;
            }
            case MCUType::ESP8266: {
                // ESP8266: ADC singolo su A0
                profile.dhtPin = D4;          // GPIO2 per DHT (GPIO4=D2)
                profile.batteryPin = A0;      // ADC unico
                profile.analogPins = ESP8266_ANALOG_PINS;
                profile.analogPinCount = 1;   // solo 1 canale ADC
                profile.analogIdPins = nullptr;
                profile.i2cSdaPin = D2;       // GPIO4 SDA
                profile.i2cSclPin = D1;       // GPIO5 SCL
                profile.oneWirePin = D3;      // GPIO0 libero
                break;
            }
            default:
                // Fallback generico (ESP32-like)
                profile.dhtPin = 13;
                profile.batteryPin = 36;
                profile.analogPins = ESP32_ANALOG_PINS;
                profile.analogPinCount = 2;   // almeno 2 ADC
                profile.analogIdPins = nullptr;
                profile.i2cSdaPin = 21;
                profile.i2cSclPin = 22;
                profile.oneWirePin = 14;
                break;
        }

        return profile;
    }
};
