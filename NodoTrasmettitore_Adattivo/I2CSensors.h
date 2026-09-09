#pragma once
#include <Wire.h>
#include "SensorTypes.h"

// ============================================================
//  I2CSensors
//  Scansiona il bus I2C e prova a identificare i chip più
//  diffusi in ambito ambientale/agricolo tramite:
//   1) indirizzo I2C tipico
//   2) registro/ID chip quando disponibile (per evitare falsi
//      positivi tra sensori diversi che condividono indirizzo)
//
//  Aggiungere un nuovo sensore = aggiungere una entry alla
//  tabella KNOWN_I2C_DEVICES + il relativo parser di lettura.
//  Questo è il punto di estensione pensato per la community.
// ============================================================

enum class I2CChip : uint8_t {
    SHT31, SHT3X,           // temp/umidità Sensirion
    HTU21D_SI7021,          // temp/umidità (stesso indirizzo 0x40, WHOAMI diverso)
    BME280, BMP280,         // temp/umidità/pressione Bosch
    AHT10_AHT20,            // temp/umidità economico
    DS3231_RTC,             // non un sensore ambientale, escluso dai risultati utili
    UNKNOWN_I2C
};

struct I2CDeviceInfo {
    uint8_t address;
    I2CChip chip;
    const char* label;
};

// Tabella indirizzi noti. In caso di collisione di indirizzo
// (es. 0x40 usato sia da HTU21D che da SHT2x-compatibili) si
// tenta un identify() dedicato prima di assumere il tipo.
static const I2CDeviceInfo KNOWN_I2C_DEVICES[] = {
    {0x44, I2CChip::SHT31,          "SHT31/SHT35"},
    {0x45, I2CChip::SHT31,          "SHT31/SHT35 (addr alt)"},
    {0x40, I2CChip::HTU21D_SI7021,  "HTU21D/SI7021"},
    {0x76, I2CChip::BME280,         "BME280/BMP280 (addr alt)"},
    {0x77, I2CChip::BME280,         "BME280/BMP280"},
    {0x38, I2CChip::AHT10_AHT20,    "AHT10/AHT20"},
};
static const size_t KNOWN_I2C_COUNT = sizeof(KNOWN_I2C_DEVICES) / sizeof(I2CDeviceInfo);

// Scansiona l'intero bus 0x08-0x77 e ritorna gli indirizzi che
// rispondono (ACK). Non ancora classificati.
inline int i2cScanBus(TwoWire &wire, uint8_t *foundAddrs, int maxResults) {
    int count = 0;
    for (uint8_t addr = 0x08; addr <= 0x77 && count < maxResults; addr++) {
        wire.beginTransmission(addr);
        if (wire.endTransmission() == 0) {
            foundAddrs[count++] = addr;
        }
        delay(2); // margine per bus condivisi/lunghi cavi verso il nodo albero
    }
    return count;
}

class I2CSensorDriver : public ISensorDriver {
public:
    I2CSensorDriver(TwoWire &wire, uint8_t address)
        : _wire(wire), _address(address), _chip(I2CChip::UNKNOWN_I2C) {}

    const char* name() const override { return _label; }
    BusType bus() const override { return BusType::BUS_I2C; }
    uint32_t uid() const override { return 0x1C0000 | _address; }

    bool probe() override {
        // 1) presenza sul bus
        _wire.beginTransmission(_address);
        if (_wire.endTransmission() != 0) return false;

        // 2) match indirizzo nella tabella nota
        for (size_t i = 0; i < KNOWN_I2C_COUNT; i++) {
            if (KNOWN_I2C_DEVICES[i].address == _address) {
                _chip = KNOWN_I2C_DEVICES[i].chip;
                _label = KNOWN_I2C_DEVICES[i].label;
                // per indirizzi condivisi, qui andrebbe una routine
                // identify() specifica (es. lettura registro CHIP_ID
                // su BME280 = 0x60 vs BMP280 = 0x58). Punto di
                // estensione per la community.
                return true;
            }
        }

        // sconosciuto ma presente: lo esponiamo comunque come
        // "I2C device @0xNN" così l'utente lo vede nel log e può
        // contribuire un driver per quel chip.
        _label = _unknownLabelBuf;
        snprintf(_unknownLabelBuf, sizeof(_unknownLabelBuf), "I2C @0x%02X (non riconosciuto)", _address);
        _chip = I2CChip::UNKNOWN_I2C;
        return true;
    }

    SensorReading read() override {
        SensorReading r;
        r.channel = _address;

        switch (_chip) {
            case I2CChip::SHT31: {
                // Comando misura single-shot high repeatability
                _wire.beginTransmission(_address);
                _wire.write(0x2C); _wire.write(0x06);
                _wire.endTransmission();
                delay(20);
                _wire.requestFrom((int)_address, 6);
                if (_wire.available() == 6) {
                    uint16_t rawT = (_wire.read() << 8) | _wire.read(); _wire.read(); // skip CRC
                    uint16_t rawH = (_wire.read() << 8) | _wire.read(); _wire.read();
                    float temp = -45 + 175.0f * (rawT / 65535.0f);
                    float hum  = 100.0f * (rawH / 65535.0f);
                    // Un chip, due grandezze: qui semplificato, in
                    // produzione va ritornata una coppia di reading
                    // (vedi nota in SensorManager: readAll()).
                    r.type = MeasureType::MEAS_TEMPERATURE;
                    r.value = temp;
                    r.unit = "°C";
                    r.confidence = 1.0f;
                    r.valid = true;
                    _lastHumidity = hum; // esposto separatamente da readAll()
                }
                break;
            }
            case I2CChip::AHT10_AHT20: {
                _wire.beginTransmission(_address);
                _wire.write(0xAC); _wire.write(0x33); _wire.write(0x00);
                _wire.endTransmission();
                delay(80);
                _wire.requestFrom((int)_address, 6);
                if (_wire.available() == 6) {
                    uint8_t b[6];
                    for (int i = 0; i < 6; i++) b[i] = _wire.read();
                    uint32_t rawH = ((uint32_t)b[1] << 12) | ((uint32_t)b[2] << 4) | (b[3] >> 4);
                    uint32_t rawT = (((uint32_t)b[3] & 0x0F) << 16) | ((uint32_t)b[4] << 8) | b[5];
                    float hum = (rawH / 1048576.0f) * 100.0f;
                    float temp = (rawT / 1048576.0f) * 200.0f - 50.0f;
                    r.type = MeasureType::MEAS_TEMPERATURE;
                    r.value = temp;
                    r.unit = "°C";
                    r.confidence = 1.0f;
                    r.valid = true;
                    _lastHumidity = hum;
                }
                break;
            }
            default:
                // chip non ancora implementato: nessuna lettura
                r.type = MeasureType::MEAS_UNKNOWN;
                r.valid = false;
                break;
        }
        return r;
    }

    // Alcuni chip (SHT31, AHT20...) danno 2 grandezze in un solo
    // ciclo di lettura: read() ritorna la temperatura, questa
    // espone l'umidità calcolata nella stessa transazione, per
    // evitare di interrogare due volte il chip.
    bool hasSecondaryHumidity() const {
        return _chip == I2CChip::SHT31 || _chip == I2CChip::AHT10_AHT20;
    }
    float secondaryHumidity() const { return _lastHumidity; }

private:
    TwoWire &_wire;
    uint8_t _address;
    I2CChip _chip;
    const char* _label = "I2C device";
    char _unknownLabelBuf[40];
    float _lastHumidity = NAN;
};
