/*
 *  TOOLTRACK — Sistema de Almacenamiento Inteligente
 *  ESP32 + RC522 RFID + 12 Sensores IR + WebSocket Dashboard
 *  
 *  PINOUT RC522 → ESP32:
 *    SDA  → GPIO 5     SCK  → GPIO 18
 *    MOSI → GPIO 23    MISO → GPIO 19
 *    RST  → GPIO 4     GND  → GND
 *    3.3V → 3.3V
 *
 *  PINES SENSORES IR (DO digital):
 *    Slot  1→GPIO13   2→GPIO14   3→GPIO25   4→GPIO33
 *    Slot  5→GPIO35   6→GPIO39   7→GPIO26   8→GPIO27
 *    Slot  9→GPIO32  10→GPIO34  11→GPIO36  12→GPIO15
 *
 *  LÓGICA IR:  LOW = herramienta PRESENTE
 *             HIGH = herramienta AUSENTE
 * 
*/

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ArduinoJson.h>
#include "web.h"
//  CONFIGURACIÓN INICIAL

const char* WIFI_SSID     = "ELO-FOREST";      
const char* WIFI_PASSWORD = "Elo.forest1935.2024";

// Usuarios RFID registrados

struct RFIDUser { const char* uid; const char* name; };
const RFIDUser USERS[] = {
  { "1A212828", "Benjamin V."   },
  { "A97EC6B2", "Arturo F."  },
  { "C29A713D", "Mario M." },
};
const int NUM_USERS = sizeof(USERS) / sizeof(USERS[0]);

// Nombres de las 12 herramientas
const char* TOOL_NAMES[12] = {
  "Martillo",        
  "Destornillador+", 
  "Llave inglesa",   
  "Alicate",         
  "Sierra manual",   
  "Cinta metrica",   
  "Taladro",         
  "Nivel",           
  "Llave torx",      
  "Tijeras acero",   
  "Desarmador plano",
  "Pinzas punta"       
};

const char* TOOL_ICONS[12] = {
  "🔨","🔧","🔩","🪛","🪚","📏",
  "🔌","📐","🔑","✂️","🪛","🔧"
};

//  2. CONFIGURACION DE PINES

#define RFID_SS_PIN   5
#define RFID_RST_PIN  4
#define LED_BUILTIN   2

const uint8_t IR_PINS[12] = {
  13, 14, 25, 33, 35, 21,
  26, 27, 32, 34, 22, 15
};

//  4. OBJETOS Y ESTADO GLOBAL

MFRC522        rfid(RFID_SS_PIN, RFID_RST_PIN);
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

struct ToolState {
  bool    present;
  char    lastUser[32];
  char    lastUID[20];
  char    lastTime[12];
};
ToolState tools[12];

char     pendingUID[20]  = "";
char     pendingUser[32] = "";
uint32_t pendingMs       = 0;
#define  PENDING_TIMEOUT_MS 10000

bool     lastIR[12];
uint32_t debounceMs[12];
#define  DEBOUNCE_MS 80

// ───────────────────────────────────────────────────
//  5. UTILIDADES
// ───────────────────────────────────────────────────

String uidToString(byte* buf, byte len) {
  String s = "";
  for (byte i = 0; i < len; i++) {
    if (buf[i] < 0x10) s += "0";
    s += String(buf[i], HEX);
  }
  s.toUpperCase();
  return s;
}

const char* findUser(const String& uid) {
  for (int i = 0; i < NUM_USERS; i++)
    if (uid == USERS[i].uid) return USERS[i].name;
  return nullptr;
}

String timeNow() {
  uint32_t t = millis() / 1000;
  char buf[12];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
    (t / 3600) % 24, (t / 60) % 60, t % 60);
  return String(buf);
}

//  6. WEBSOCKET — enviar datos

void sendFullState(AsyncWebSocketClient* client) {
  StaticJsonDocument<2048> doc;
  doc["type"] = "full_state";
  JsonArray arr = doc.createNestedArray("tools");
  for (int i = 0; i < 12; i++) {
    JsonObject o = arr.createNestedObject();
    o["slot"]     = i + 1;
    o["name"]     = TOOL_NAMES[i];
    o["icon"]     = TOOL_ICONS[i];
    o["present"]  = tools[i].present;
    o["lastUser"] = tools[i].lastUser;
    o["lastUID"]  = tools[i].lastUID;
    o["lastTime"] = tools[i].lastTime;
  }
  String json; serializeJson(doc, json);
  client ? client->text(json) : ws.textAll(json);
}

void sendEvent(int i, const char* evType) {
  StaticJsonDocument<256> doc;
  doc["type"]    = "slot_event";
  doc["slot"]    = i + 1;
  doc["name"]    = TOOL_NAMES[i];
  doc["icon"]    = TOOL_ICONS[i];
  doc["present"] = tools[i].present;
  doc["event"]   = evType;
  doc["user"]    = tools[i].lastUser;
  doc["uid"]     = tools[i].lastUID;
  doc["time"]    = tools[i].lastTime;
  String json; serializeJson(doc, json);
  ws.textAll(json);
  Serial.printf("[%s] Slot %02d | %-18s | %s | %s\n",
    tools[i].lastTime, i+1, TOOL_NAMES[i], evType, tools[i].lastUser);
}

void onWsEvent(AsyncWebSocket* srv, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("[WS] Cliente #%u conectado\n", client->id());
    sendFullState(client);
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[WS] Cliente #%u desconectado\n", client->id());
  }
}

// ───────────────────────────────────────────────────
//  7. RFID
// ───────────────────────────────────────────────────

void handleRFID() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  String uid = uidToString(rfid.uid.uidByte, rfid.uid.size);
  const char* user = findUser(uid);

  strncpy(pendingUID,  uid.c_str(),                 sizeof(pendingUID) - 1);
  strncpy(pendingUser, user ? user : "Desconocido", sizeof(pendingUser) - 1);
  pendingMs = millis();

  Serial.printf("[RFID] %s → %s\n", uid.c_str(), pendingUser);
  digitalWrite(LED_BUILTIN, HIGH); delay(80); digitalWrite(LED_BUILTIN, LOW);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

//  8. SENSORES IR

void handleIR() {
  for (int i = 0; i < 12; i++) {
    bool reading = (digitalRead(IR_PINS[i]) == LOW);   // LOW = presente
    if (reading == lastIR[i]) { debounceMs[i] = millis(); continue; }
    if ((millis() - debounceMs[i]) < DEBOUNCE_MS) continue;

    bool wasPres = tools[i].present;
    tools[i].present = lastIR[i] = reading;
    String t = timeNow();
    strncpy(tools[i].lastTime, t.c_str(), sizeof(tools[i].lastTime) - 1);

    if (wasPres && !reading) {
      // Herramienta retirada
      bool hasPending = (pendingUID[0] != '\0') &&
                        ((millis() - pendingMs) < PENDING_TIMEOUT_MS);
      strncpy(tools[i].lastUser,
              hasPending ? pendingUser : "Sin identificar",
              sizeof(tools[i].lastUser) - 1);
      strncpy(tools[i].lastUID,
              hasPending ? pendingUID  : "N/A",
              sizeof(tools[i].lastUID) - 1);
      if (hasPending) { pendingUID[0] = '\0'; pendingUser[0] = '\0'; }
      sendEvent(i, "salida");

    } else if (!wasPres && reading) {
      // Herramienta devuelta
      sendEvent(i, "entrada");
    }
  }
}

//  9. SETUP

void setup() {
  Serial.begin(115200);
  Serial.println("\n══════════════════════════");
  Serial.println("  TOOLTRACK — Iniciando");
  Serial.println("══════════════════════════");

  pinMode(LED_BUILTIN, OUTPUT);

  // IR — estado inicial
  for (int i = 0; i < 12; i++) {
    pinMode(IR_PINS[i], INPUT_PULLUP);
    lastIR[i]          = (digitalRead(IR_PINS[i]) == LOW);
    tools[i].present   = lastIR[i];
    debounceMs[i]      = 0;
    tools[i].lastUser[0] = '\0';
    tools[i].lastUID[0]  = '\0';
    strncpy(tools[i].lastTime, "00:00:00", sizeof(tools[i].lastTime));
    Serial.printf("  Slot %02d [%-18s] %s\n",
      i+1, TOOL_NAMES[i], tools[i].present ? "PRESENTE" : "AUSENTE");
  }

  // RFID
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("[RFID] RC522 listo.");

  // WiFi
  Serial.printf("[WiFi] Conectando a %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int att = 0;
  while (WiFi.status() != WL_CONNECTED && att < 40) {
    delay(500); Serial.print("."); att++;
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[WiFi] Falló. Reiniciando...");
    delay(3000); ESP.restart();
  }
  digitalWrite(LED_BUILTIN, LOW);
  Serial.printf("\n[WiFi] IP: http://%s\n", WiFi.localIP().toString().c_str());

  // WebSocket + rutas
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send_P(200, "text/html", DASHBOARD_HTML);
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* r) {
    StaticJsonDocument<2048> doc;
    doc["ip"]     = WiFi.localIP().toString();
    doc["uptime"] = millis() / 1000;
    JsonArray arr = doc.createNestedArray("tools");
    for (int i = 0; i < 12; i++) {
      JsonObject o = arr.createNestedObject();
      o["slot"] = i+1; o["name"] = TOOL_NAMES[i];
      o["present"] = tools[i].present;
      o["lastUser"] = tools[i].lastUser;
      o["lastUID"]  = tools[i].lastUID;
      o["lastTime"] = tools[i].lastTime;
    }
    String json; serializeJson(doc, json);
    r->send(200, "application/json", json);
  });

  server.onNotFound([](AsyncWebServerRequest* r) {
    r->send(404, "text/plain", "No encontrado");
  });

  server.begin();
  Serial.println("[HTTP] Servidor activo.");
  Serial.printf("\n  Abre: http://%s\n\n", WiFi.localIP().toString().c_str());
}

void loop() {
  handleRFID();
  handleIR();

  // Limpiar clientes WS desconectados cada 5 seg
  static uint32_t lastClean = 0;
  if (millis() - lastClean > 5000) {
    ws.cleanupClients();
    lastClean = millis();
  }

  // Expirar pendingUID sin movimiento
  if (pendingUID[0] != '\0' && (millis() - pendingMs) > PENDING_TIMEOUT_MS) {
    Serial.printf("[RFID] Pendiente expirado: %s\n", pendingUID);
    pendingUID[0] = '\0'; pendingUser[0] = '\0';
  }

  delay(5);
}