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

    // ---- 0. RILEVAMENTO MCU (NUOVO - trasparente all'utente) ----
    Serial.println("\n--- 0. RILEVAMENTO MCU E PIN ---");
    MCUProfile mcu = MCUDetector::detect();
    PinProfile pins = HardwareConfig::getPinProfile();
    HardwareConfig::configureADC(mcu);

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

    // DHT22: pin dal profilo hardware adattivo, auto-verificato
    DHTSensorDriver dht(pins.dhtPin);
    bool dhtOk = dht.probe();
    if (dhtOk) Serial.println(" - DHT22 rilevato e funzionante");
    else       Serial.println(" - DHT22 non rilevato (pin scollegato o sensore assente)");

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
        rh.channel = pins.dhtPin;
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
