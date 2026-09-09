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
//  Versione MCU-agnostica del trasmettitore sensori.
//  Rileva automaticamente il microcontroller e auto-configura
//  i pin + bus senza intervento manuale.
//
//  Funziona su: ESP32, ESP32-C3, ESP32-S3, ESP8266, RP2040
// ============================================================

// Configuration (ESP-NOW receiver - personalizza con il tuo receiver MAC)
uint8_t receiverAddress[] = {0x34, 0xCD, 0xB0, 0xE9, 0x69, 0x8C};
#define MY_WIFI_CHANNEL 6
uint32_t sleepTimeSeconds = 10;
#define uS_TO_S_FACTOR 1000000ULL

volatile bool sendCompleted = false;
SensorManagerAdaptive* sensorManager = nullptr;
DHTSensorDriver* dhtSensor = nullptr;

// ============================================================
//  CALLBACK ESP-NOW
// ============================================================
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
    Serial.print("[ESP-NOW] Invio: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "✓ SUCCESS" : "✗ FAIL");
    sendCompleted = true;
}

// ============================================================
//  FUNZIONI AUSILIARI
// ============================================================

uint32_t getAutoNodeID() {
    uint64_t mac = ESP.getEfuseMac();
    uint32_t id = (uint32_t)(mac & 0xFFFFFFFF);
    Serial.printf("[INFO] Node ID dal MAC: 0x%08X\n", id);
    return id;
}

float readBatteryVoltage(const MCUProfile &mcu, uint8_t batteryPin) {
    uint32_t rawmV = analogReadMilliVolts(batteryPin);
    float vBat = (rawmV / 1000.0f) * 2.0f;  // divisore 1:2 standard
    Serial.printf("[BAT] Raw mV: %u | Calcolato: %.2f V\n", rawmV, vBat);
    return vBat;
}

void enterDeepSleep(uint32_t seconds) {
    Serial.printf("[SLEEP] Deep Sleep per %u secondi...\n", seconds);
    Serial.flush();
    Serial.end();
    
#ifdef CONFIG_IDF_FIRMWARE_CHIP_ID
    // ESP32 family
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * uS_TO_S_FACTOR);
    esp_deep_sleep_start();
#elif defined(ARDUINO_ARCH_RP2040)
    // RP2040 non ha deep sleep built-in, usa delay
    delay(seconds * 1000);
#else
    // Fallback
    delay(seconds * 1000);
#endif
}

void setupWiFiESPNow() {
#ifdef CONFIG_IDF_FIRMWARE_CHIP_ID
    // Solo per ESP32 family
    Serial.println("[WiFi] Configurazione ESP-NOW...");
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(MY_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERR] ESP-NOW init fallito!");
        return;
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverAddress, 6);
    peerInfo.channel = MY_WIFI_CHANNEL;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ERR] Aggiunta peer fallita!");
        return;
    }

    esp_now_register_send_cb(OnDataSent);
#else
    Serial.println("[NOTA] ESP-NOW non disponibile su questo MCU");
#endif
}

void sendSensorPacket(const std::vector<SensorReading> &readings, 
                      const MCUProfile &mcu, uint32_t nodeID, float batteryVolts) {
#ifdef CONFIG_IDF_FIRMWARE_CHIP_ID
    SensorPacket packet;
    packet.readingCount = 0;
    packet.nodeID = nodeID;
    packet.batteryVolts = batteryVolts;

    for (auto &r : readings) {
        if (packet.readingCount >= MAX_READINGS) {
            Serial.println("[WARN] Troppe letture, scartate");
            break;
        }
        WireReading &w = packet.readings[packet.readingCount];
        w.type = (uint8_t)r.type;
        w.channel = r.channel;
        w.value = r.value;
        w.confidence = r.confidence;
        packet.readingCount++;
    }

    Serial.printf("[PKT] Size: %u B | Letture: %u | VBat: %.2f V\n",
                  sizeof(SensorPacket), packet.readingCount, packet.batteryVolts);

    esp_err_t result = esp_now_send(receiverAddress, (uint8_t *)&packet, sizeof(SensorPacket));
    if (result != ESP_OK) {
        Serial.printf("[ERR] esp_now_send error: %d\n", result);
    }

    unsigned long startWait = millis();
    while (!sendCompleted && (millis() - startWait < 1500)) {
        delay(10);
    }
#else
    Serial.println("[INFO] Pacchetto dati (non inviato, MCU non supporta ESP-NOW):");
    Serial.printf("  Node ID: 0x%08X\n", nodeID);
    Serial.printf("  Batteria: %.2f V\n", batteryVolts);
    Serial.printf("  Letture: %u\n", readings.size());
    for (const auto &r : readings) {
        Serial.printf("    - [%s] ch=%u val=%.2f conf=%.2f\n",
                      measureTypeName(r.type), r.channel, r.value, r.confidence);
    }
#endif
}

// ============================================================
//  SETUP PRINCIPALE
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║  NODO TRASMETTITORE SENSORI - ADATTIVO  ║");
    Serial.println("║  Auto-detect MCU & Sensori              ║");
    Serial.println("╚════════════════════════════════════════╝");

    // ---- 1. RILEVAMENTO MCU ----
    Serial.println("\n--- 1. RILEVAMENTO MCU ---");
    SensorManagerAdaptive sensors;
    sensorManager = &sensors;
    
    MCUProfile mcu = sensors.getMCUProfile();
    PinProfile pins = sensors.getPinProfile();
    
    Serial.printf("[MCU] %s\n", mcu.name);
    Serial.printf("[PINS] DHT:%u | Battery:%u | I2C:%u/%u | 1-Wire:%u\n",
                  pins.dhtPin, pins.batteryPin, pins.i2cSdaPin, pins.i2cSclPin, pins.oneWirePin);

    // ---- 2. DISCOVERY SENSORI ----
    Serial.println("\n--- 2. DISCOVERY SENSORI ---");
    sensors.begin();

    // DHT22 (dinamico, ma pin da profilo hardware)
    bool dhtOk = false;
    SensorReading dhtTemp, dhtHum;
    
    if (pins.dhtPin != 0xFF) {
        dhtSensor = new DHTSensorDriver(pins.dhtPin);
        dhtOk = dhtSensor->probe();
        Serial.printf("[DHT] %s\n", dhtOk ? "✓ Rilevato" : "✗ Non trovato");
    }

    // ---- 3. LETTURA SENSORI ----
    Serial.println("\n--- 3. LETTURA SENSORI ---");
    std::vector<SensorReading> readings = sensors.readAll();

    // Aggiungi DHT se presente
    if (dhtOk && dhtSensor) {
        dhtTemp = dhtSensor->read();
        if (dhtTemp.valid) {
            readings.push_back(dhtTemp);
            Serial.printf("  [DHT-T] %.2f °C\n", dhtTemp.value);
        }
        
        float hum = dhtSensor->secondaryHumidity();
        if (!isnan(hum)) {
            dhtHum.type = MeasureType::MEAS_HUMIDITY_AIR;
            dhtHum.value = hum;
            dhtHum.channel = pins.dhtPin;
            dhtHum.unit = "%";
            dhtHum.confidence = 1.0f;
            dhtHum.valid = true;
            readings.push_back(dhtHum);
            Serial.printf("  [DHT-H] %.2f %%\n", hum);
        }
    }

    for (auto &r : readings) {
        Serial.printf("  [%s] ch=%u val=%.2f conf=%.2f\n",
                      measureTypeName(r.type), r.channel, r.value, r.confidence);
    }

    // ---- 4. INVIO DATI ----
    Serial.println("\n--- 4. TRASMISSIONE DATI ---");
    uint32_t nodeID = getAutoNodeID();
    float batteryVolts = readBatteryVoltage(mcu, pins.batteryPin);

    setupWiFiESPNow();
    sendSensorPacket(readings, mcu, nodeID, batteryVolts);

    // ---- 5. SLEEP ----
    Serial.println("\n--- 5. DEEP SLEEP ---");
    if (dhtSensor) delete dhtSensor;
    enterDeepSleep(sleepTimeSeconds);
}

void loop() {
    // Tutto in setup(), poi deep sleep
}
