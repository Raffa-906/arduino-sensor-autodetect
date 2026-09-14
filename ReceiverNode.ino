#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SensorProtocol.h>

const char* WIFI_SSID   = "Resonator";
const char* WIFI_PASS   = "resonator-2020";
const char* SERVER_URL  = "http://192.168.20.224:8000/ingest/";

#define MAX_ENTRIES 50

// NOTA: non ridefiniamo più una nostra struct locale. Usiamo
// direttamente SensorPacket/WireReading da SensorProtocol.h, la
// STESSA libreria condivisa inclusa dal trasmettitore. È essenziale
// che sia la stessa: essendo un memcpy binario via ESP-NOW, se le
// due parti avessero struct diverse (anche solo per un campo in più
// o in ordine diverso) i dati arriverebbero corrotti o scartati
// silenziosamente (mismatch di sizeof()).

struct JournalEntry {
  uint32_t id;
  SensorPacket data;
};

Preferences journalMem;
uint32_t head = 0;
uint32_t tail = 0;

volatile bool newDataAvailable = false;
SensorPacket freshData;

// Costruisce il JSON includendo, per ogni lettura, il tipo di
// sensore (es. "MEAS_TEMPERATURE", "MEAS_HUMIDITY_AIR", ...) così
// il server sa su quale grandezza sta lavorando ogni canale, non
// solo il valore grezzo.
bool sendToFastAPI(const SensorPacket& data) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  String jsonPayload = "{";
  jsonPayload += "\"node_id\":" + String(data.nodeID) + ",";
  jsonPayload += "\"battery\":" + String(data.batteryVolts, 2) + ",";
  jsonPayload += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  jsonPayload += "\"readings\":[";
  for (uint8_t i = 0; i < data.readingCount; i++) {
    const WireReading &r = data.readings[i];
    jsonPayload += "{";
    jsonPayload += "\"sensor_type\":\"" + String(measureTypeName((MeasureType)r.type)) + "\",";
    jsonPayload += "\"channel\":" + String(r.channel) + ",";
    jsonPayload += "\"value\":" + String(r.value, 2) + ",";
    jsonPayload += "\"confidence\":" + String(r.confidence, 2);
    jsonPayload += "}";
    if (i + 1 < data.readingCount) jsonPayload += ",";
  }
  jsonPayload += "]}";

  int httpCode = http.POST(jsonPayload);
  http.end();

  return (httpCode == 200 || httpCode == 201);
}

void saveToFlash(const SensorPacket& data) {
  head++;
  JournalEntry entry = { head, data };

  String key = "entry_" + String(head % MAX_ENTRIES);
  journalMem.putBytes(key.c_str(), &entry, sizeof(JournalEntry));
  journalMem.putULong("head", head);

  Serial.printf("[FLASH] Dati salvati in memoria nello slot #%lu\n", head % MAX_ENTRIES);
}

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  if (len == sizeof(SensorPacket) && !newDataAvailable) {
    memcpy(&freshData, incomingData, sizeof(SensorPacket));
    newDataAvailable = true;
  } else if (len != sizeof(SensorPacket)) {
    Serial.printf("[ESP-NOW RECV] Pacchetto scartato: dimensione %d != attesa %u (versione firmware diversa tra nodo e ricevitore?)\n",
                  len, (unsigned)sizeof(SensorPacket));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  journalMem.begin("journal", false);
  head = journalMem.getULong("head", 0);
  tail = journalMem.getULong("tail", 0);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("[WIFI] Connessione in corso ");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WIFI] Connesso. IP: %s | Canale Wi-Fi AP: %d\n", 
                  WiFi.localIP().toString().c_str(), WiFi.channel());
    Serial.println("[AVVISO] Assicurati che il trasmettitore invii sullo stesso canale!");
  } else {
    Serial.println("\n[WIFI] Rete assente. I dati verranno salvati in Flash.");
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Errore di inizializzazione");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);
}

void loop() {
  if (newDataAvailable) {
    SensorPacket currentData = freshData;
    newDataAvailable = false; 

    Serial.printf("[ESP-NOW RECV] Nodo: 0x%08X | Bat: %.2fV | Letture: %u\n",
                  currentData.nodeID, currentData.batteryVolts, currentData.readingCount);
    for (uint8_t i = 0; i < currentData.readingCount; i++) {
      const WireReading &r = currentData.readings[i];
      Serial.printf("    - [%s] canale=%u valore=%.2f confidenza=%.2f\n",
                    measureTypeName((MeasureType)r.type), r.channel, r.value, r.confidence);
    }

    if (!sendToFastAPI(currentData)) {
      Serial.println("[HTTP FALLITO] Salvataggio in Flash...");
      saveToFlash(currentData);
    } else {
      Serial.println("[HTTP OK] Inviato con successo a FastAPI.");
    }
  }

  if (tail < head && WiFi.status() == WL_CONNECTED) {
    uint32_t target_id = tail + 1;
    String key = "entry_" + String(target_id % MAX_ENTRIES);

    JournalEntry entryToSend;
    if (journalMem.getBytes(key.c_str(), &entryToSend, sizeof(JournalEntry)) == sizeof(JournalEntry)) {
      if (sendToFastAPI(entryToSend.data)) {
        tail++;
        journalMem.putULong("tail", tail);
        Serial.printf("[RECOVERY] Recuperato record #%lu\n", target_id);
      }
    }
  }

  delay(10);
}
