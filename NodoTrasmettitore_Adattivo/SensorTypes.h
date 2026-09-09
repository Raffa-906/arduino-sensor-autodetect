#pragma once
#include <Arduino.h>
#include <SensorProtocol.h>

// ============================================================
//  SensorTypes.h
//  Definizioni locali al Nodo Trasmettitore: bus supportati,
//  esito letture, interfaccia astratta ISensorDriver.
//  MeasureType invece vive in SensorProtocol.h (libreria
//  condivisa) perché serve identico anche al Nodo Ricevitore.
// ============================================================

// NOTA: i valori sono prefissati con "BUS_" perché il core ESP32
// definisce macro del preprocessore con nomi come ANALOG, INPUT,
// OUTPUT, I2C ecc. (usate da pinMode e simili). Se un valore di
// questo enum avesse lo stesso nome, il preprocessore lo
// sostituirebbe silenziosamente PRIMA che il compilatore veda
// l'enum, causando errori di sintassi difficili da capire.
enum class BusType : uint8_t {
    BUS_ANALOG,     // ADC su GPIO
    BUS_I2C,
    BUS_ONEWIRE,
    BUS_SDI12,
    BUS_UART_MODBUS,
    BUS_DIGITAL,    // pin digitale fisso a protocollo proprietario (es. DHT22)
    BUS_UNKNOWN
};

struct SensorReading {
    MeasureType type = MeasureType::MEAS_UNKNOWN;
    float value = NAN;
    const char* unit = "";
    float confidence = 0.0f;   // 0..1 quanto siamo sicuri della classificazione
    uint8_t channel = 0;       // pin/indirizzo/porta sorgente
    bool valid = false;
};

// Interfaccia che ogni driver di sensore deve implementare.
// Un "driver" può rappresentare un chip singolo (es. SHT31)
// o un intero canale analogico con la sua euristica.
class ISensorDriver {
public:
    virtual ~ISensorDriver() {}

    // Nome leggibile, per log/config (es. "SHT31 @0x44", "ADC34-soil?")
    virtual const char* name() const = 0;

    virtual BusType bus() const = 0;

    // Tenta di rilevare/inizializzare il sensore su questo canale.
    // Ritorna true se il sensore è presente e riconosciuto.
    virtual bool probe() = 0;

    // Esegue una lettura. Va chiamato solo dopo probe() == true.
    virtual SensorReading read() = 0;

    // Identificatore univoco stabile (address I2C, ROM code onewire,
    // numero pin ADC...) usato per non duplicare rilevazioni.
    virtual uint32_t uid() const = 0;
};
