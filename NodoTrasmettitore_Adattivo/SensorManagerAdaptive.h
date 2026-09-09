#pragma once
#include <vector>
#include <memory>
#include <Wire.h>
#include <OneWire.h>
#include <Preferences.h>
#include "SensorTypes.h"
#include "I2CSensors.h"
#include "OneWireSensors.h"
#include "AnalogSensors.h"
#include "MCUDetection.h"
#include "HardwareConfig.h"

// ============================================================
//  SensorManagerAdaptive.h
//  Estende SensorManager per auto-configurarsi in base al MCU
//  rilevato. Mantiene la stessa interfaccia e flusso logico
//  del SensorManager originale, ma auto-seleziona pin e bus.
//
//  Il MCU viene rilevato automaticamente, e i pin vengono
//  configurati dal profilo hardware. I2C e 1-Wire vengono
//  inizializzati solo se disponibili per il MCU corrente.
// ============================================================

class SensorManagerAdaptive {
public:
    // Costruttore: rileva MCU e carica il profilo pin automaticamente
    SensorManagerAdaptive()
        : _mcu(MCUDetector::detect()),
          _pins(HardwareConfig::getPinProfile()),
          _oneWire(_pins.oneWirePin == 255 ? 0 : _pins.oneWirePin),
          _oneWireEnabled(_pins.oneWirePin != 255),
          _i2cBus(0),
          _i2cEnabled(false) {}

    void begin() {
        Serial.println("\n=== INIZIALIZZAZIONE SENSORI ADATTIVA ===");
        
        // Configura ADC secondo il MCU rilevato
        HardwareConfig::configureADC(_mcu);

        // Inizializza I2C solo se il MCU ha pin I2C disponibili
        bool i2cValid = (_pins.i2cSdaPin != 0xFF && _pins.i2cSclPin != 0xFF);
        if (i2cValid) {
            _i2cBus.begin(_pins.i2cSdaPin, _pins.i2cSclPin);
            _i2cEnabled = true;
            Serial.printf("[I2C] Inizializzato su SDA=%u SCL=%u\n", 
                         _pins.i2cSdaPin, _pins.i2cSclPin);
        } else {
            Serial.println("[I2C] Non disponibile su questo MCU");
            _i2cEnabled = false;
        }

        _prefs.begin("treesense", false);
        discoverAll();
    }

    void discoverAll() {
        _drivers.clear();
        Serial.println("\n--- DISCOVERY BUS SENSORI ---");
        discoverI2C();
        discoverOneWire();
        discoverAnalog();
    }

    // Ritorna tutte le letture valide di questo ciclo, già classificate
    std::vector<SensorReading> readAll() {
        std::vector<SensorReading> out;
        for (auto &drv : _drivers) {
            SensorReading r = drv->read();
            if (r.valid) out.push_back(r);

            // Caso speciale: chip I2C con doppia grandezza (es. SHT31
            // ritorna temperatura da read(), umidità va presa a parte)
            if (drv->bus() == BusType::BUS_I2C) {
                I2CSensorDriver* i2cDrv = static_cast<I2CSensorDriver*>(drv.get());
                if (i2cDrv->hasSecondaryHumidity()) {
                    SensorReading rh;
                    rh.type = MeasureType::MEAS_HUMIDITY_AIR;
                    rh.value = i2cDrv->secondaryHumidity();
                    rh.unit = "%";
                    rh.confidence = 1.0f;
                    rh.channel = r.channel;
                    rh.valid = !isnan(rh.value);
                    if (rh.valid) out.push_back(rh);
                }
            }
        }
        return out;
    }

    size_t sensorCount() const { return _drivers.size(); }

    void printDiscoveryReport(Stream &out) {
        out.println(F("\n=== TreeSense: sensori rilevati ==="));
        for (auto &drv : _drivers) {
            out.print(F(" - "));
            out.print(drv->name());
            out.print(F(" | bus="));
            out.print((int)drv->bus());
            out.print(F(" | uid=0x"));
            out.println(drv->uid(), HEX);
        }
        out.println(F("===================================="));
    }

    // Accesso ai driver analogici per la fase di calibrazione guidata
    std::vector<AnalogSensorDriver*> analogDrivers() {
        std::vector<AnalogSensorDriver*> result;
        for (auto &drv : _drivers) {
            if (drv->bus() == BusType::BUS_ANALOG) {
                result.push_back(static_cast<AnalogSensorDriver*>(drv.get()));
            }
        }
        return result;
    }

    // Accessori al profilo MCU e hardware
    const MCUProfile& getMCUProfile() const { return _mcu; }
    const PinProfile& getPinProfile() const { return _pins; }

private:
    MCUProfile _mcu;
    PinProfile _pins;
    TwoWire _i2cBus;
    OneWire _oneWire;
    bool _oneWireEnabled;
    bool _i2cEnabled;
    Preferences _prefs;
    std::vector<std::unique_ptr<ISensorDriver>> _drivers;

    void discoverI2C() {
        if (!_i2cEnabled) return;
        uint8_t addrs[16];
        int found = i2cScanBus(_i2cBus, addrs, 16);
        Serial.printf("[I2C-SCAN] trovati %d dispositivi\n", found);
        for (int i = 0; i < found; i++) {
            auto drv = std::make_unique<I2CSensorDriver>(_i2cBus, addrs[i]);
            if (drv->probe()) {
                _drivers.push_back(std::move(drv));
                Serial.printf("  [I2C] 0x%02X -> %s\n", addrs[i], drv->name());
            }
        }
    }

    void discoverOneWire() {
        if (!_oneWireEnabled) return;
        uint8_t romTable[8][8];
        int found = oneWireScanBus(_oneWire, romTable, 8);
        Serial.printf("[1-WIRE] trovati %d dispositivi\n", found);
        for (int i = 0; i < found; i++) {
            auto drv = std::make_unique<OneWireSensorDriver>(_oneWire, romTable[i]);
            if (drv->probe()) {
                _drivers.push_back(std::move(drv));
                Serial.printf("  [1-Wire] ROM:%02X -> %s\n", romTable[i][0], drv->name());
            }
        }
    }

    void discoverAnalog() {
        Serial.printf("[ANALOG] Scanning %u canali ADC\n", _pins.analogPinCount);
        for (uint8_t i = 0; i < _pins.analogPinCount; i++) {
            uint8_t idPin = _pins.analogIdPins ? _pins.analogIdPins[i] : 0xFF;
            auto drv = std::make_unique<AnalogSensorDriver>(_pins.analogPins[i], _prefs, idPin);
            if (drv->probe()) {
                _drivers.push_back(std::move(drv));
                Serial.printf("  [ADC] GPIO%u -> connesso\n", _pins.analogPins[i]);
            }
        }
    }
};
