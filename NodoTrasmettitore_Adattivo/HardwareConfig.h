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
    uint8_t dhtPin;              // pin DHT "atteso"/di default, primo candidato nello scan
    uint8_t batteryPin;          // pin per misurare batteria (ADC)
    const uint8_t* analogPins;   // array di pin ADC disponibili
    uint8_t analogPinCount;
    const uint8_t* analogIdPins; // chip ID 1-Wire dedicati (opzionale)
    uint8_t i2cSdaPin;           // I2C (se disponibile)
    uint8_t i2cSclPin;
    uint8_t oneWirePin;          // 1-Wire (se disponibile)
    const uint8_t* dhtCandidatePins; // pin digitali liberi da provare per lo scan DHT
    uint8_t dhtCandidateCount;       // (dhtPin è già incluso come primo elemento)
};

class HardwareConfig {
public:
    // Rileva il MCU e restituisce il profilo pin appropriato
    static PinProfile getPinProfile() {
        MCUProfile mcu = MCUDetector::detect();
        return selectPinProfile(mcu);
    }

    // Variante che riusa un MCUProfile già rilevato altrove, per
    // evitare di richiamare MCUDetector::detect() (e ristampare il
    // banner MCU) più volte nello stesso boot.
    static PinProfile getPinProfile(const MCUProfile &mcu) {
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

    // Pin digitali candidati per lo scan automatico del DHT22 (vedi
    // resolveDHTPin() nel .ino). Il primo elemento è il pin "atteso"
    // di default: se il cablaggio è quello previsto, lo scan lo trova
    // al primo tentativo. Gli altri sono alternative libere, già
    // depurate dai pin riservati a I2C/1-Wire/ADC e dai pin di
    // strapping (bootstrap) del MCU, che non vanno usati per
    // periferiche esterne generiche.
    static constexpr uint8_t ESP32_DHT_CANDIDATES[]    = {13, 4, 16, 17};
    static constexpr uint8_t ESP32_C3_DHT_CANDIDATES[] = {8, 5, 10};
    static constexpr uint8_t ESP32_S3_DHT_CANDIDATES[] = {10, 13, 14, 21};
    static constexpr uint8_t PICO_DHT_CANDIDATES[]     = {14, 16, 17, 18};
    static constexpr uint8_t ESP8266_DHT_CANDIDATES[]  = {2}; // D4: pochissimi GPIO liberi

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
                profile.dhtCandidatePins = ESP32_DHT_CANDIDATES;
                profile.dhtCandidateCount = sizeof(ESP32_DHT_CANDIDATES);
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
                profile.dhtCandidatePins = ESP32_C3_DHT_CANDIDATES;
                profile.dhtCandidateCount = sizeof(ESP32_C3_DHT_CANDIDATES);
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
                profile.dhtCandidatePins = ESP32_S3_DHT_CANDIDATES;
                profile.dhtCandidateCount = sizeof(ESP32_S3_DHT_CANDIDATES);
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
                profile.dhtCandidatePins = PICO_DHT_CANDIDATES;
                profile.dhtCandidateCount = sizeof(PICO_DHT_CANDIDATES);
                break;
            }
            case MCUType::ESP8266: {
                profile.analogPins = ESP8266_ANALOG_PINS;
                profile.analogPinCount = 1;   // solo 1 canale ADC
                profile.analogIdPins = nullptr;
#if defined(ESP8266)
                // Le etichette Dx/A0 esistono solo sul core ESP8266
                profile.dhtPin = D4;          // GPIO2 per DHT
                profile.batteryPin = A0;      // ADC unico
                profile.i2cSdaPin = D2;       // GPIO4 SDA
                profile.i2cSclPin = D1;       // GPIO5 SCL
                profile.oneWirePin = D3;      // GPIO0 libero
                profile.dhtCandidatePins = ESP8266_DHT_CANDIDATES;
                profile.dhtCandidateCount = sizeof(ESP8266_DHT_CANDIDATES);
#else
                // NOTA: questo ramo non è mai raggiunto a runtime, perché
                // MCUType::ESP8266 viene selezionato solo quando la macro
                // ESP8266 è definita (vedi MCUDetection.h). È qui solo
                // perché il compilatore analizza comunque tutti i case
                // dello switch anche quando compila per un altro target;
                // senza questo ramo, un build ESP32 fallirebbe per uso
                // delle label Dx/A0 (definite solo dal core ESP8266).
                // Se questo codice diventa raggiungibile, i pin sotto
                // NON sono validati sull'hardware reale: vanno verificati.
                profile.dhtPin = 2;
                profile.batteryPin = 17;      // A0
                profile.i2cSdaPin = 4;
                profile.i2cSclPin = 5;
                profile.oneWirePin = 0;
                profile.dhtCandidatePins = ESP8266_DHT_CANDIDATES;
                profile.dhtCandidateCount = sizeof(ESP8266_DHT_CANDIDATES);
#endif
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
                profile.dhtCandidatePins = ESP32_DHT_CANDIDATES;
                profile.dhtCandidateCount = sizeof(ESP32_DHT_CANDIDATES);
                break;
        }

        return profile;
    }
};
