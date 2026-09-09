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
//  rilevato. Usa HardwareConfig per selezionare i pin e i bus
//  corretti senza intervento dell'utente.
// ============================================================

class SensorManagerAdaptive {
public:
    SensorManagerAdaptive() 
        : _mcu(MCUDetector::detect()),
          _pins(HardwareConfig::getPinProfile()),
          _oneWire(_pins.oneWirePin == 255 ? 0 : _pins.oneWirePin),
          _oneWireEnabled(_pins.oneWirePin != 255) {}

    void begin() {
        Serial.println("\n╔═══════════════════════════════════════════╗");
        Serial.println("║  INIZIALIZZAZIONE SENSORI ADATTIVA       ║");
        Serial.println("╚═══════════════════════════════════════════╝\n");

        // Configura ADC secondo il MCU
        HardwareConfig::configureADC(_mcu);

        // Inizializza I2C solo se pin validi
        bool i2cValid = (_pins.i2cSdaPin != 0xFF && _pins.i2cSclPin != 0xFF);
        if (i2cValid) {
            _i2c = std::make_unique<TwoWire>(0);
            _i2c->begin(_pins.i2cSdaPin, _pins.i2cSclPin);
            _i2cEnabled = true;
        }

        _prefs.begin("treesense", false);
        discoverAll();

        Serial.printf("[INIT] Sensori rilevati: %u\n", _drivers.size());
        printDiscoveryReport(Serial);
    }

    void discoverAll() {
        _drivers.clear();
        discoverI2C();
        discoverOneWire();
        discoverAnalog();
    }

    std::vector<SensorReading> readAll() {
        std::vector<SensorReading> out;
        for (auto &drv : _drivers) {
            SensorReading r = drv->read();
            if (r.valid) out.push_back(r);

            // Caso speciale: chip I2C con doppia grandezza
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
        out.println(F("\n=== TreeSense: Sensori Rilevati ==="));
        if (_drivers.empty()) {
            out.println(F(" ⚠ Nessun sensore trovato!"));
        }
        for (auto &drv : _drivers) {
            out.print(F" - ");
            out.print(drv->name());
            out.print(F(" | bus="));
            out.print((int)drv->bus());
            out.print(F(" | uid=0x"));
            out.println(drv->uid(), HEX);
        }
        out.println(F("================================\n"));
    }

    const MCUProfile& getMCUProfile() const { return _mcu; }
    const PinProfile& getPinProfile() const { return _pins; }

    // Accesso ai driver analogici per calibrazione
    std::vector<AnalogSensorDriver*> analogDrivers() {
        std::vector<AnalogSensorDriver*> result;
        for (auto &drv : _drivers) {
            if (drv->bus() == BusType::BUS_ANALOG) {
                result.push_back(static_cast<AnalogSensorDriver*>(drv.get()));
            }
        }
        return result;
    }

private:
    MCUProfile _mcu;
    PinProfile _pins;
    std::unique_ptr<TwoWire> _i2c;
    OneWire _oneWire;
    bool _oneWireEnabled;
    bool _i2cEnabled = false;
    Preferences _prefs;
    std::vector<std::unique_ptr<ISensorDriver>> _drivers;

    void discoverI2C() {
        if (!_i2cEnabled || !_i2c) return;
        uint8_t addrs[16];
        int found = i2cScanBus(*_i2c, addrs, 16);
        for (int i = 0; i < found; i++) {
            auto drv = std::make_unique<I2CSensorDriver>(*_i2c, addrs[i]);
            if (drv->probe()) _drivers.push_back(std::move(drv));
        }
    }

    void discoverOneWire() {
        if (!_oneWireEnabled) return;
        uint8_t romTable[8][8];
        int found = oneWireScanBus(_oneWire, romTable, 8);
        for (int i = 0; i < found; i++) {
            auto drv = std::make_unique<OneWireSensorDriver>(_oneWire, romTable[i]);
            if (drv->probe()) _drivers.push_back(std::move(drv));
        }
    }

    void discoverAnalog() {
        for (uint8_t i = 0; i < _pins.analogPinCount; i++) {
            uint8_t idPin = _pins.analogIdPins ? _pins.analogIdPins[i] : 0xFF;
            auto drv = std::make_unique<AnalogSensorDriver>(_pins.analogPins[i], _prefs, idPin);
            if (drv->probe()) _drivers.push_back(std::move(drv));
        }
    }
};
