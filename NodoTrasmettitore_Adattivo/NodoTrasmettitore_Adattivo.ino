#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <SensorProtocol.h>
#include "SensorTypes.h"
#include "MCUDetection.h"
#include "HardwareConfig.h"
#include "SensorManagerAdaptive.h"
#include "DHTSensors.h"

// ============================================================
//  NodoTrasmettitore_Adattivo.ino
//  Versione MCU-agnostica mantenendo ESATTAMENTE la struttura
//  e il flusso logico dell'originale NodoTrasmettitore_Tot.ino
//
//  Funziona su: ESP32, ESP32-C3, ESP32-S3, ESP8266, RP2040
// ============================================================

// Configurazione ESP-NOW (personalizza con il tuo receiver MAC)
uint8_t receiverAddress[] = {0x34, 0xCD, 0xB0, 0xE9, 0x69, 0x8C};
#define MY_WIFI_CHANNEL 6

uint32_t sleepTimeSeconds = 10;
#define uS_TO_S_FACTOR 1000000ULL

volatile bool sendCompleted = false;

// Preferences dedicate alla persistenza del pin DHT trovato dallo scan
// (stesso namespace "treesense" usato da SensorManagerAdaptive per i
// canali analogici, ma chiave separata).
Preferences dhtPrefs;

// ============================================================
//  RICERCA AUTOMATICA DEL PIN DHT22
//  Il DHT22 non è scansionabile in modo passivo (vedi nota in
//  DHTSensors.h): bisogna interrogare attivamente un pin alla
//  volta con il protocollo a timing per sapere se risponde.
//
//  Strategia:
//   1) Se in flash c'è già un pin salvato da uno scan precedente,
//      lo si riprova per primo: se risponde ancora, si usa subito
//      senza rifare tutta la scansione (fast path, quasi sempre
//      vero ad ogni risveglio).
//   2) Se non risponde più (o non c'era nulla salvato), si scandisce
//      la lista di pin candidati del profilo hardware corrente
//      (dhtPin "atteso" per primo, poi le alternative libere).
//   3) Il primo pin che risponde con un DHT22 valido viene salvato
//      in flash per i risvegli successivi.
//
//  Va chiamata PRIMA di qualunque altra inizializzazione di bus
//  (I2C, 1-Wire, ADC, WiFi/ESP-NOW): il protocollo del DHT è a
//  timing sensibile e non deve condividere il processore con
//  altre attività di comunicazione nello stesso momento.
// ============================================================
uint8_t resolveDHTPin(const PinProfile &pins) {
    dhtPrefs.begin("treesense", false);
    uint8_t saved = dhtPrefs.getUChar("dht_pin", 0xFF);

    if (saved != 0xFF) {
        DHTSensorDriver test(saved);
        if (test.probe()) {
            Serial.printf("[DHT] Pin salvato GPIO%u confermato\n", saved);
            dhtPrefs.end();
            return saved;
        }
        Serial.printf("[DHT] Pin salvato GPIO%u non risponde più, ripeto la scansione\n", saved);
    }

    for (uint8_t i = 0; i < pins.dhtCandidateCount; i++) {
        uint8_t candidate = pins.dhtCandidatePins[i];
        Serial.printf("[DHT] Provo GPIO%u...\n", candidate);
        DHTSensorDriver test(candidate);
        if (test.probe()) {
            dhtPrefs.putUChar("dht_pin", candidate);
            Serial.printf("[DHT] Trovato su GPIO%u, salvato in flash\n", candidate);
            dhtPrefs.end();
            return candidate;
        }
    }

    Serial.println("[DHT] Nessun DHT22 trovato tra i pin candidati");
    dhtPrefs.end();
    return 0xFF;
}

// ============================================================
//  FUNZIONI AUSILIARI (esattamente come l'originale)
// ============================================================

uint32_t getAutoNodeID() {
    uint64_t mac = ESP.getEfuseMac();
    uint32_t id = (uint32_t)(mac & 0xFFFFFFFF);
    Serial.printf("[DEBUG] Node ID generato dal MAC: 0x%08X\n", id);
    return id;
}

float readBatteryVoltage(uint8_t batteryPin) {
    uint32_t rawmV = analogReadMilliVolts(batteryPin);
    float vBat = (rawmV / 1000.0f) * 2.0f;
    Serial.printf("[DEBUG] Batteria - Grezzo mV: %u | Calcolato: %.2f V\n", rawmV, vBat);
    return vBat;
}

void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
    Serial.print("[CALLBACK ESP-NOW] Esito invio: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAIL");
    sendCompleted = true;
}

void enterDeepSleep(uint32_t seconds) {
    Serial.printf("[SLEEP] Chiusura Seriale ed entrata in Deep Sleep per %u secondi...\n", seconds);
    Serial.flush();
    Serial.end();
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * uS_TO_S_FACTOR);
    esp_deep_sleep_start();
}

// ============================================================
//  SETUP PRINCIPALE (ESATTAMENTE la struttura originale)
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println("\n==============================================");
    Serial.println("   AVVIO NODO TRASMETTITORE (auto-detect, MCU-agnostico) ");
    Serial.println("==============================================");

    // ---- 0. RILEVAMENTO MCU (fatto una sola volta, riusato sotto) ----
    Serial.println("\n--- 0. RILEVAMENTO MCU E PIN ---");
    MCUProfile mcu = MCUDetector::detect();
    PinProfile pins = HardwareConfig::getPinProfile(mcu);
    HardwareConfig::configureADC(mcu);

    // ---- -1. RICERCA DHT22 (PRIMA di qualunque altro bus/comunicazione) ----
    Serial.println("\n--- RICERCA PIN DHT22 ---");
    uint8_t dhtPin = resolveDHTPin(pins);

    // Configura ADC come nell'originale (ma ora sa la risoluzione corretta)
    analogSetPinAttenuation(pins.batteryPin, ADC_11db);
    pinMode(pins.batteryPin, INPUT);

    // ---- 1. DISCOVERY SENSORI (ESATTAMENTE come originale) ----
    Serial.println("\n--- 1. RILEVAMENTO SENSORI ---");

    SensorPacket packet;
    packet.readingCount = 0;
    packet.nodeID = getAutoNodeID();
    packet.batteryVolts = readBatteryVoltage(pins.batteryPin);

    // I2C e OneWire: ora automaticamente configurati da SensorManagerAdaptive
    // in base al MCU rilevato (niente hardcoding di pin)
    SensorManagerAdaptive sensors;
    sensors.begin();
    sensors.printDiscoveryReport(Serial);

    // DHT22: pin trovato dinamicamente dallo scan (resolveDHTPin).
    // L'esito (trovato/non trovato, su quale GPIO) è già stampato
    // dentro resolveDHTPin(): qui si ripete solo probe() per
    // popolare i dati (temperatura/umidità) sull'oggetto definitivo.
    DHTSensorDriver dht(dhtPin);
    bool dhtOk = (dhtPin != 0xFF) && dht.probe();

    // ---- 2. LETTURA E COSTRUZIONE PACCHETTO (IDENTICO all'originale) ----
    Serial.println("\n--- 2. LETTURA SENSORI ---");

    // Leggi tutti i sensori rilevati automaticamente
    std::vector<SensorReading> readings = sensors.readAll();

    // Aggiungi DHT se presente (ESATTAMENTE come originale)
    if (dhtOk) {
        SensorReading rt = dht.read();
        if (rt.valid) readings.push_back(rt);
        SensorReading rh;
        rh.type = MeasureType::MEAS_HUMIDITY_AIR;
        rh.value = dht.secondaryHumidity();
        rh.channel = dhtPin;
        rh.confidence = 1.0f;
        rh.valid = !isnan(rh.value);
        if (rh.valid) readings.push_back(rh);
    }

    // Costruisci pacchetto: ESATTAMENTE come originale, byte per byte
    for (auto &r : readings) {
        if (packet.readingCount >= MAX_READINGS) {
            Serial.println("[WARNING] Troppe letture, alcune verranno scartate (aumenta MAX_READINGS)");
            break;
        }
        WireReading &w = packet.readings[packet.readingCount];
        w.type = (uint8_t)r.type;
        w.channel = r.channel;
        w.value = r.value;
        w.confidence = r.confidence;
        packet.readingCount++;
        Serial.printf("  [%s] canale=%u valore=%.2f confidenza=%.2f\n",
                      measureTypeName(r.type), r.channel, r.value, r.confidence);
    }

    // ---- 3. CONFIGURAZIONE WI-FI / ESP-NOW (invariata rispetto all'originale) ----
    Serial.println("\n--- 3. CONFIGURAZIONE WI-FI E ESP-NOW ---");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(MY_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERRORE] Inizializzazione ESP-NOW fallita!");
        enterDeepSleep(sleepTimeSeconds);
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverAddress, 6);
    peerInfo.channel = MY_WIFI_CHANNEL;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ERRORE] Aggiunta Peer fallita!");
        enterDeepSleep(sleepTimeSeconds);
    }

    esp_now_register_send_cb(OnDataSent);

    // ---- 4. INVIO PACCHETTO (invariato) ----
    Serial.println("\n--- 4. INVIO PACCHETTO DATI ---");
    Serial.printf("[PACKET] Dim: %u B | ID: 0x%08X | Letture: %u | VBat: %.2fV\n",
                  sizeof(SensorPacket), packet.nodeID, packet.readingCount, packet.batteryVolts);

    esp_err_t sendResult = esp_now_send(receiverAddress, (uint8_t *)&packet, sizeof(SensorPacket));
    if (sendResult != ESP_OK) {
        Serial.printf("[DEBUG] Errore esp_now_send(): %d\n", sendResult);
    }

    unsigned long startWait = millis();
    while (!sendCompleted && (millis() - startWait < 1500)) {
        delay(10);
    }

    // ---- 5. SLEEP (invariato) ----
    Serial.println("\n--- 5. FASE DI SLEEP ---");
    enterDeepSleep(sleepTimeSeconds);
}

void loop() {
    // Vuoto: tutto avviene in setup() prima del deep sleep
}
