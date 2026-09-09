#pragma once
#include <DHT.h>
#include "SensorTypes.h"

// ============================================================
//  DHTSensors
//  Il DHT22 (e simili AM230x) NON è scansionabile: usa un
//  protocollo proprietario a singolo pin senza indirizzo o ROM
//  code, quindi il pin va comunque saputo in anticipo (è un
//  vincolo fisico del sensore, non una scelta del firmware).
//
//  Quello che il firmware PUÒ fare automaticamente è verificare
//  se sul pin dichiarato risponde davvero un DHT22 funzionante:
//  se il pin è scollegato o il sensore è assente, probe()
//  fallisce e il canale semplicemente non compare nel pacchetto
//  finale, invece di mandare dati inventati.
// ============================================================

class DHTSensorDriver : public ISensorDriver {
public:
    explicit DHTSensorDriver(uint8_t pin) : _pin(pin), _dht(pin, DHT22) {}

    const char* name() const override { return "DHT22"; }
    BusType bus() const override { return BusType::BUS_DIGITAL; }
    uint32_t uid() const override { return 0xD00000 | _pin; }

    bool probe() override {
        _dht.begin();
        delay(2000); // il DHT22 richiede stabilizzazione dopo il power-up

        float h = _dht.readHumidity();
        float t = _dht.readTemperature();
        int retry = 0;
        while ((isnan(h) || isnan(t)) && retry < 3) {
            delay(500);
            h = _dht.readHumidity();
            t = _dht.readTemperature();
            retry++;
        }
        if (isnan(h) || isnan(t)) return false; // nessun sensore valido su questo pin

        _lastTemp = t;
        _lastHum = h;
        return true;
    }

    SensorReading read() override {
        SensorReading r;
        r.channel = _pin;
        r.type = MeasureType::MEAS_TEMPERATURE;
        r.value = _lastTemp;
        r.unit = "°C";
        r.confidence = 1.0f;
        r.valid = !isnan(_lastTemp);
        return r;
    }

    // Come per i chip I2C a doppia grandezza: la temperatura esce
    // da read(), l'umidità è esposta qui per lo stesso ciclo di
    // lettura (vedi SensorManager::readAll()).
    bool hasSecondaryHumidity() const { return true; }
    float secondaryHumidity() const { return _lastHum; }

private:
    uint8_t _pin;
    DHT _dht;
    float _lastTemp = NAN;
    float _lastHum = NAN;
};
