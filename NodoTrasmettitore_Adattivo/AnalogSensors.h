#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <OneWire.h>
#include "SensorTypes.h"

// Family code riservato, a scelta del progetto, per i chip ID
// 1-Wire dedicati messi nei connettori dei sensori analogici
// (es. DS2431 = 0x2D reale, ma qui puoi anche solo controllare
// "c'è un ROM su questo pin dedicato" senza distinguere modello,
// e leggerne la memoria per sapere il tipo di sensore scritto
// in fabbrica dal produttore del modulo).
inline bool readAnalogIdChip(uint8_t idPin, MeasureType &outType) {
    if (idPin == 0xFF) return false; // nessun ID pin cablato per questo canale
    OneWire idBus(idPin);
    uint8_t rom[8];
    idBus.reset_search();
    if (!idBus.search(rom)) return false;       // nessun chip presente
    if (OneWire::crc8(rom, 7) != rom[7]) return false;

    // Lettura di 1 byte di memoria utente dal chip (offset e comandi
    // dipendono dal modello, es. DS2431: read memory 0xF0 + addr).
    // Qui semplificato: il byte letto codifica direttamente un
    // MeasureType secondo una convenzione decisa dal progetto e
    // scritta in fabbrica sui moduli sensore ufficiali.
    idBus.reset();
    idBus.select(rom);
    idBus.write(0xF0); // Read Memory command (DS2431-style)
    idBus.write(0x00); idBus.write(0x00); // address LSB/MSB
    uint8_t typeByte = idBus.read();

    if (typeByte >= (uint8_t)MeasureType::MEAS_TEMPERATURE && typeByte <= (uint8_t)MeasureType::MEAS_PRESSURE) {
        outType = (MeasureType)typeByte;
        return true;
    }
    return false;
}

// ============================================================
//  AnalogSensors
//  I sensori puramente analogici (potenziometri, dendrometri
//  potenziometrici, molti soil moisture resistivi/capacitivi)
//  NON sono elettricamente auto-identificabili: dal punto di
//  vista dell'ADC sono tutti "una tensione su un pin".
//
//  IMPORTANTE: questo modulo NON chiede mai nulla all'utente in
//  locale (niente prompt seriale bloccante) perché i nodi sono
//  dispositivi headless, senza operatore davanti. La
//  classificazione avviene solo per due vie, entrambe senza
//  interazione fisica sul dispositivo:
//
//  1) ID CHIP DEDICATO PER CONNETTORE (consigliato, affidabile al
//     100%). Ogni slot connettore analogico ha un proprio GPIO
//     riservato a un piccolo chip 1-Wire (es. DS2431) che vive
//     dentro al connettore del sensore. Essendo un GPIO dedicato
//     a un solo chip (non un bus condiviso), la corrispondenza
//     pin-analogico -> tipo sensore è certa. Vedi
//     AnalogIdChipReader più sotto.
//
//  2) EURISTICA + CONFIDENZA, nessuna decisione locale definitiva.
//     Se non c'è un ID chip, il canale viene comunque letto e
//     trasmesso sempre (mai bloccato), insieme a un "sospetto" di
//     classificazione e alla sua confidenza. La decisione finale
//     su cosa sia realmente collegato viene presa lato backend/
//     dashboard al momento della registrazione del nodo, e può
//     essere poi scaricata sul nodo come mappatura remota tramite
//     applyRemoteChannelConfig() — mai chiesta sul dispositivo.
// ============================================================

struct AnalogHeuristicResult {
    MeasureType guess = MeasureType::MEAS_VOLTAGE_RAW;
    float confidence = 0.0f;
    const char* reason = "";
};

class AnalogSensorDriver : public ISensorDriver {
public:
    // idPin: GPIO dedicato al chip ID 1-Wire di questo connettore,
    // 0xFF se il tuo hardware non lo prevede (allora si passa
    // all'euristica + configurazione remota).
    AnalogSensorDriver(uint8_t pin, Preferences &prefs, uint8_t idPin = 0xFF)
        : _pin(pin), _prefs(prefs), _idPin(idPin) {}

    const char* name() const override { return _label; }
    BusType bus() const override { return BusType::BUS_ANALOG; }
    uint32_t uid() const override { return 0xA00000 | _pin; }

    // Un canale ADC "esiste" sempre elettricamente: probe() qui
    // verifica solo che il pin non sia floating/scollegato,
    // leggendo varianza minima nel tempo. Se è completamente
    // piatto a 0 o a Vcc per più letture, probabilmente non c'è
    // nulla collegato.
    bool probe() override {
        // Pull-down interno: un pin davvero scollegato viene forzato
        // verso massa e letto stabilmente basso (rumore floating
        // altrimenti simula un segnale "connesso" a valori casuali,
        // es. 0.5-2.5V, anche a vuoto). Un sensore/potenziometro
        // reale collegato sovrasta il pull-down con la sua tensione.
        pinMode(_pin, INPUT_PULLDOWN);
        int samples[5];
        for (int i = 0; i < 5; i++) {
            samples[i] = analogRead(_pin);
            delay(5);
        }
        int minV = samples[0], maxV = samples[0];
        for (int i = 1; i < 5; i++) {
            minV = min(minV, samples[i]);
            maxV = max(maxV, samples[i]);
        }
        bool stuckLow  = maxV < 15;      // sempre a massa
        bool stuckHigh = minV > 4080;    // sempre a Vcc (ADC 12-bit ESP32)
        _connected = !(stuckLow || stuckHigh);

        // Priorità 1: ID chip dedicato letto direttamente dal
        // connettore (nessuna interazione, nessuna ambiguità).
        MeasureType idType;
        if (readAnalogIdChip(_idPin, idType)) {
            declareType(idType); // salva anche in flash, sopravvive ai riavvii
            return _connected;
        }

        // Priorità 2: mappatura ricevuta in precedenza da remoto
        // (vedi applyRemoteChannelConfig in main.cpp), già salvata
        // in flash da un boot precedente.
        char key[16];
        snprintf(key, sizeof(key), "adc%u_type", _pin);
        _savedType = (MeasureType)_prefs.getUChar(key, (uint8_t)MeasureType::MEAS_UNKNOWN);

        return _connected;
    }

    SensorReading read() override {
        SensorReading r;
        r.channel = _pin;
        int raw = analogRead(_pin);
        float voltage = raw * (3.3f / 4095.0f); // ADC 12-bit, riferimento 3.3V tipico ESP32

        if (_savedType != MeasureType::MEAS_UNKNOWN) {
            // canale già configurato esplicitamente dall'utente
            r.type = _savedType;
            r.confidence = 1.0f;
        } else {
            AnalogHeuristicResult h = classifyByBehaviour();
            r.type = h.guess;
            r.confidence = h.confidence;
        }

        r.value = applyCalibration(voltage, r.type);
        r.unit = unitFor(r.type);
        r.valid = _connected;
        return r;
    }

    // Applica una mappatura canale->tipo ricevuta da remoto (non
    // da input locale). Va chiamata quando arriva un downlink di
    // configurazione dal gateway/backend (LoRa downlink, MQTT
    // retained message, ecc.), MAI da un prompt su questo nodo.
    void applyRemoteChannelConfig(MeasureType t) {
        declareType(t);
    }

    // Salva permanentemente il tipo per questo pin (usata sia da
    // applyRemoteChannelConfig() sia, se disponibile, dall'esito
    // di un ID chip dedicato letto in probe()).
    void declareType(MeasureType t) {
        char key[16];
        snprintf(key, sizeof(key), "adc%u_type", _pin);
        _prefs.putUChar(key, (uint8_t)t);
        _savedType = t;
    }

    // Salva due punti di calibrazione (es. secco/bagnato per
    // umidità suolo, oppure 0mm/Xmm per dendrometro).
    void declareCalibration(float rawLow, float valueLow, float rawHigh, float valueHigh) {
        char kA[16], kB[16], kC[16], kD[16];
        snprintf(kA, sizeof(kA), "adc%u_rl", _pin);
        snprintf(kB, sizeof(kB), "adc%u_vl", _pin);
        snprintf(kC, sizeof(kC), "adc%u_rh", _pin);
        snprintf(kD, sizeof(kD), "adc%u_vh", _pin);
        _prefs.putFloat(kA, rawLow);
        _prefs.putFloat(kB, valueLow);
        _prefs.putFloat(kC, rawHigh);
        _prefs.putFloat(kD, valueHigh);
        _hasCalibration = true;
    }

private:
    uint8_t _pin;
    uint8_t _idPin;
    Preferences &_prefs;
    const char* _label = "Canale analogico";
    bool _connected = false;
    MeasureType _savedType = MeasureType::MEAS_UNKNOWN;
    bool _hasCalibration = false;

    // Buffer circolare per l'euristica comportamentale (finestra
    // temporale). In produzione andrebbe popolato dal loop
    // principale ad ogni ciclo di lettura, qui semplificato con
    // lettura singola + due letture distanziate.
    AnalogHeuristicResult classifyByBehaviour() {
        AnalogHeuristicResult h;
        int v1 = analogRead(_pin);
        delay(300);
        int v2 = analogRead(_pin);
        int delta = abs(v1 - v2);

        if (delta < 2) {
            // segnale estremamente stabile su finestra breve:
            // compatibile con dendrometro o soil moisture in
            // condizioni stabili. Non distinguibile senza storico
            // più lungo (ore) -> confidenza bassa, richiede
            // conferma utente.
            h.guess = MeasureType::MEAS_VOLTAGE_RAW;
            h.confidence = 0.3f;
            h.reason = "Segnale stabile: probabile dendrometro o soil moisture, ma serve conferma";
        } else {
            h.guess = MeasureType::MEAS_POTENTIOMETER;
            h.confidence = 0.4f;
            h.reason = "Segnale variabile in tempi brevi: probabile input manuale/potenziometro";
        }
        return h;
    }

    float applyCalibration(float voltage, MeasureType type) {
        if (!_hasCalibration) return voltage; // fallback: tensione grezza
        char kA[16], kB[16], kC[16], kD[16];
        snprintf(kA, sizeof(kA), "adc%u_rl", _pin);
        snprintf(kB, sizeof(kB), "adc%u_vl", _pin);
        snprintf(kC, sizeof(kC), "adc%u_rh", _pin);
        snprintf(kD, sizeof(kD), "adc%u_vh", _pin);
        float rl = _prefs.getFloat(kA, 0);
        float vl = _prefs.getFloat(kB, 0);
        float rh = _prefs.getFloat(kC, 3.3);
        float vh = _prefs.getFloat(kD, 1);
        if (rh == rl) return voltage;
        return vl + (voltage - rl) * (vh - vl) / (rh - rl);
    }

    const char* unitFor(MeasureType t) {
        switch (t) {
            case MeasureType::MEAS_HUMIDITY_SOIL: return "%";
            case MeasureType::MEAS_DENDROMETER:   return "mm";
            case MeasureType::MEAS_POTENTIOMETER: return "°";
            default: return "V";
        }
    }
};
