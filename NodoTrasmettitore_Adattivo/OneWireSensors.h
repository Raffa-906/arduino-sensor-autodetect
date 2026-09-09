#pragma once
#include <OneWire.h>
#include "SensorTypes.h"

// ============================================================
//  OneWireSensors
//  Il bus 1-Wire si auto-identifica per costruzione: ogni
//  dispositivo ha un ROM code a 64 bit il cui primo byte è il
//  "family code" che indica il modello di chip. Nessuna euristica
//  necessaria, è identificazione reale.
//
//  Family code comuni:
//   0x28 -> DS18B20 (temperatura)
//   0x22 -> DS1822  (temperatura)
//   0x10 -> DS18S20 (temperatura, legacy)
// ============================================================

class OneWireSensorDriver : public ISensorDriver {
public:
    OneWireSensorDriver(OneWire &bus, const uint8_t rom[8])
        : _bus(bus) {
        memcpy(_rom, rom, 8);
    }

    const char* name() const override { return _label; }
    BusType bus() const override { return BusType::BUS_ONEWIRE; }

    uint32_t uid() const override {
        // usa i byte centrali del ROM come id compatto
        return (_rom[1] << 24) | (_rom[2] << 16) | (_rom[3] << 8) | _rom[4];
    }

    bool probe() override {
        switch (_rom[0]) {
            case 0x28: _label = "DS18B20"; _known = true; break;
            case 0x22: _label = "DS1822";  _known = true; break;
            case 0x10: _label = "DS18S20"; _known = true; break;
            default:   _label = "OneWire sconosciuto"; _known = false; break;
        }
        return _known; // se non è un chip di temperatura noto, lo ignoriamo
    }

    SensorReading read() override {
        SensorReading r;
        r.channel = _rom[7];

        _bus.reset();
        _bus.select(_rom);
        _bus.write(0x44, 1); // start conversion, parasite power on
        delay(750);          // 750ms worst case per 12-bit DS18B20

        _bus.reset();
        _bus.select(_rom);
        _bus.write(0xBE);    // read scratchpad

        uint8_t data[9];
        for (int i = 0; i < 9; i++) data[i] = _bus.read();

        int16_t raw = (data[1] << 8) | data[0];
        float celsius = raw / 16.0f;

        r.type = MeasureType::MEAS_TEMPERATURE;
        r.value = celsius;
        r.unit = "°C";
        r.confidence = 1.0f;
        r.valid = (celsius > -55 && celsius < 125); // range fisico DS18B20
        return r;
    }

private:
    OneWire &_bus;
    uint8_t _rom[8];
    const char* _label = "OneWire";
    bool _known = false;
};

// Scansiona il bus e ritorna quanti dispositivi ha trovato,
// riempiendo romTable con i ROM code (max maxDevices).
inline int oneWireScanBus(OneWire &bus, uint8_t romTable[][8], int maxDevices) {
    uint8_t addr[8];
    int count = 0;
    bus.reset_search();
    while (count < maxDevices && bus.search(addr)) {
        if (OneWire::crc8(addr, 7) == addr[7]) {
            memcpy(romTable[count], addr, 8);
            count++;
        }
    }
    return count;
}
