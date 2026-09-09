# Arduino Sensor Auto-Detection System

**Auto-rilevamento MCU e sensori** per sistemi IoT multi-dispositivo. Firmware che si auto-configura in base al microcontroller utilizzato, senza richiedere modifiche di codice da parte dell'utente.

## 🎯 Caratteristiche Principali

### 1. **Auto-Rilevamento MCU**
Il firmware rileva automaticamente il microcontroller e carica il profilo hardware corretto:
- **Pin ADC** (numero e risoluzione)
- **Bus disponibili** (I2C, 1-Wire, UART)
- **Memoria** (RAM, Flash)
- **Capacità di deep sleep**

**MCU Supportati:**
- ESP32 (classic, dual-core)
- ESP32-C3 (single-core, ADC limitati)
- ESP32-S3 (dual-core, più ADC)
- ESP32-C6
- ESP8266
- RP2040 (Raspberry Pi Pico)
- STM32F4xx

### 2. **Auto-Rilevamento Sensori**
Scansiona automaticamente tutti i bus supportati:

**I2C** → Identifica sensori per indirizzo + registro ID:
- SHT31/SHT35 (temperatura + umidità)
- HTU21D/SI7021
- BME280/BMP280 (temperatura + umidità + pressione)
- AHT10/AHT20
- Dispositivi sconosciuti (riportati come `I2C @0xNN`)

**1-Wire** → Identifica per family code:
- DS18B20 (temperatura, 0x28)
- DS1822 (0x22)
- DS18S20 (legacy, 0x10)
- Chip ID dedicati nei connettori (DS2431)

**Analogici** → Classificazione per:
- **ID chip dedicato** (1-Wire nel connettore del sensore) → 100% affidabile
- **Configurazione remota** (dal backend/dashboard) → stable
- **Euristica comportamentale** (fallback: stabilità segnale nel tempo)

**Digitali** → Pin fissi ma auto-verificati:
- DHT22 (temperatura + umidità)

### 3. **Configurazione MCU-Agnostica**
I pin vengono selezionati automaticamente dal profilo hardware, senza hardcoding:

```cpp
// Automatico - NO pin hardcoding necessario!
MCUProfile mcu = MCUDetector::detect();
PinProfile pins = HardwareConfig::getPinProfile();

SensorManagerAdaptive sensors;  // Tutto auto-configurato
sensors.begin();
```

## 📁 Struttura del Progetto

```
arrow-sensor-autodetect/
├── NodoTrasmettitore_Adattivo.ino    # Main sketch (entry point)
├── MCUDetection.h                    # Rilevamento MCU
├── HardwareConfig.h                  # Mapping pin per MCU
├── SensorManagerAdaptive.h           # Discovery sensori (I2C, 1-Wire, Analog)
├── AnalogSensors.h                   # Driver sensori analogici
├── I2CSensors.h                      # Driver sensori I2C
├── OneWireSensors.h                  # Driver sensori 1-Wire
├── DHTSensors.h                      # Driver DHT22
├── SensorTypes.h                     # Enums e interfacce
└── README.md                         # Questo file
```

## 🚀 Come Usare

### Setup Basico

1. **Clona o copia i file** nel tuo progetto Arduino

2. **Includi il file principale**:
   ```cpp
   // Automatico - MCU e sensori rilevati all'avvio
   #include "NodoTrasmettitore_Adattivo.ino"
   ```

3. **Personalizza (opzionale)**:
   - MAC receiver ESP-NOW (line ~20)
   - Sleep time (line ~23)
   - WiFi channel (line ~21)

4. **Compila e carica**:
   - Nessuna configurazione manuale di pin necessaria
   - Seleziona il tuo MCU dalla board menu di Arduino IDE
   - Upload!

### Flusso di Avvio

```
┌─────────────────────────────────┐
│  1. RILEVAMENTO MCU             │
│  - Legge chip ID                │
│  - Carica profilo pin            │
│  - Configura ADC                │
└──────────────┬──────────────────┘
               ↓
┌─────────────────────────────────┐
│  2. DISCOVERY SENSORI           │
│  - Scansiona I2C                │
│  - Scansiona 1-Wire             │
│  - Scansiona canali ADC         │
│  - Verifica DHT22               │
└──────────────┬──────────────────┘
               ↓
┌─────────────────────────────────┐
│  3. LETTURA SENSORI             │
│  - Legge tutti i sensori        │
│  - Classifica automaticamente   │
│  - Costruisce pacchetto         │
└──────────────┬──────────────────┘
               ↓
┌─────────────────────────────────┐
│  4. TRASMISSIONE (ESP-NOW)      │
│  - Invia pacchetto al receiver  │
└──────────────┬──────────────────┘
               ↓
┌─────────────────────────────────┐
│  5. DEEP SLEEP                  │
│  - Entra in sleep per N secondi │
└─────────────────────────────────┘
```

## 🔧 Personalizzazione

### Aggiungere un Nuovo Sensore I2C

1. Aggiungi l'indirizzo alla tabella `KNOWN_I2C_DEVICES` in `I2CSensors.h`:
   ```cpp
   static const I2CDeviceInfo KNOWN_I2C_DEVICES[] = {
       {0x44, I2CChip::SHT31, "SHT31/SHT35"},
       {0x29, I2CChip::VL6180X, "VL6180X"},  // ← NUOVO
   };
   ```

2. Aggiungi il chip type all'enum `I2CChip` in `I2CSensors.h`

3. Implementa il parser di lettura nel `switch` del metodo `read()` in `I2CSensors.h`

### Aggiungere un Nuovo MCU

1. Aggiungi il tipo all'enum `MCUType` in `MCUDetection.h`

2. Aggiungi il rilevamento nella funzione `detectMCU()` di `MCUDetection.h`

3. Aggiungi il profilo pin in `HardwareConfig.h` nel metodo `selectPinProfile()`:
   ```cpp
   case MCUType::YOUR_MCU: {
       profile.dhtPin = 13;
       profile.batteryPin = 36;
       profile.analogPins = YOUR_ANALOG_PINS;
       profile.analogPinCount = YOUR_COUNT;
       // ... altri pin
       break;
   }
   ```

## 📊 Output Seriale Tipico

```
==============================================
   AVVIO NODO TRASMETTITORE (auto-detect)
==============================================

--- 0. RILEVAMENTO MCU E PIN ---
╔════════════════════════════════════════╗
║  MCU: ESP32-C3
║  ADC: 12-bit, 5 canali
║  I2C: 1 bus | 1-Wire: ✓
║  RAM: 400 KB | Flash: 4096 KB
║  Ref ADC: 3.30 V | Deep Sleep: ✓
╚════════════════════════════════════════╝

--- 1. RILEVAMENTO SENSORI ---

=== INIZIALIZZAZIONE SENSORI ADATTIVA ===
[I2C] Inizializzato su SDA=6 SCL=7

--- DISCOVERY BUS SENSORI ---
[I2C-SCAN] trovati 2 dispositivi
  [I2C] 0x44 -> SHT31/SHT35
  [I2C] 0x76 -> BME280/BMP280
[1-WIRE] trovati 1 dispositivi
  [1-Wire] ROM:28 -> DS18B20
[ANALOG] Scanning 5 canali ADC
  [ADC] GPIO0 -> connesso
  [ADC] GPIO2 -> connesso
  [ADC] GPIO4 -> connesso

=== TreeSense: sensori rilevati ===
 - SHT31/SHT35 | bus=2 | uid=0x1C0044
 - BME280/BMP280 | bus=2 | uid=0x1C0076
 - DS18B20 | bus=3 | uid=0x1A2B3C4D
 - Canale analogico | bus=0 | uid=0xA00000
 - Canale analogico | bus=0 | uid=0xA00002
 - Canale analogico | bus=0 | uid=0xA00004
====================================
 - DHT22 rilevato e funzionante

--- 2. LETTURA SENSORI ---
  [MEAS_TEMPERATURE] canale=68 valore=22.50 confidenza=1.00
  [MEAS_HUMIDITY_AIR] canale=68 valore=45.30 confidenza=1.00
  [MEAS_TEMPERATURE] canale=118 valore=23.10 confidenza=1.00
  [MEAS_HUMIDITY_AIR] canale=118 valore=43.20 confidenza=1.00
  [MEAS_PRESSURE] canale=118 valore=1013.25 confidenza=1.00
  [MEAS_TEMPERATURE] canale=0 valore=22.80 confidenza=1.00
  [MEAS_VOLTAGE_RAW] canale=0 valore=2.35 confidenza=0.30
  [MEAS_VOLTAGE_RAW] canale=2 valore=1.65 confidenza=0.30
  [MEAS_HUMIDITY_AIR] canale=8 valore=44.50 confidenza=1.00

--- 3. CONFIGURAZIONE WI-FI E ESP-NOW ---
[CALLBACK ESP-NOW] Esito invio: SUCCESS

--- 4. INVIO PACCHETTO DATI ---
[PACKET] Dim: 256 B | ID: 0xA1B2C3D4 | Letture: 9 | VBat: 4.15V

--- 5. FASE DI SLEEP ---
[SLEEP] Chiusura Seriale ed entrata in Deep Sleep per 10 secondi...
```

## 🔐 Sicurezza & Affidabilità

- **Nessuna decisione locale definitiva per sensori analogici**: Il firmware trasmette sempre i dati con confidenza. La decisione finale su cosa sia collegato viene presa lato backend/dashboard
- **Probe intelligente**: Non trasmette dati inventati. Se un sensore non risponde, viene semplicemente skippato
- **CRC validation**: Per 1-Wire, valida il CRC del ROM code prima di usarlo
- **Headless operation**: Niente prompt seriale bloccante. Perfetto per dispositivi senza operatore

## 📝 Note Importanti

1. **Sensori analogici NON sono auto-identificabili** → usano ID chip dedicati o configurazione remota
2. **I2C e 1-Wire sono auto-identificabili** → nessuna ambiguità, riconoscimento affidabile al 100%
3. **DHT22 richiede pin fisso** → ma viene verificato in probe()
4. **Configurable Remotely** → il backend può inviare mappature di configurazione via LoRa downlink o MQTT

## 🐛 Troubleshooting

### "Nessun sensore trovato"
- Controlla i connettori I2C/1-Wire
- Verifica i valori di pull-up (I2C: 4.7kΩ tipici)
- Usa il serial monitor per vedere i dettagli di discovery

### "MCU sconosciuto"
- Il firmware utilizza il fallback generico
- I sensori dovrebbero comunque funzionare
- Aggiungi il profilo MCU nel file `MCUDetection.h`

### Sensore I2C non riconosciuto
- Verrà riportato come `I2C @0xNN (non riconosciuto)`
- Aggiungi il driver nella tabella `KNOWN_I2C_DEVICES`

## 📚 Dipendenze

- **Arduino Core** (ESP32, ESP8266, RP2040, STM32)
- **OneWire** (libreria standard Arduino)
- **DHT** (libreria Adafruit o equivalente)
- **Preferences** (built-in ESP32)
- **SensorProtocol.h** (libreria condivisa con Nodo Ricevitore)

## 🤝 Contribuisci

Puoi aggiungere:
- Nuovi MCU nella sezione `MCUDetection.h`
- Nuovi sensori I2C in `I2CSensors.h`
- Nuovi sensori 1-Wire in `OneWireSensors.h`
- Miglioramenti all'euristica analogica in `AnalogSensors.h`

## 📄 Licenza

MIT - Libero di usare e modificare

---

**Versione:** 1.0 | **Data:** 2026-09-09 | **Autore:** Copilot & Raffa-906
