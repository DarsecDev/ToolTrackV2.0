/*
 * ============================================================
 *  TOOLTRACK + CERRADURA RFID — SPIFFS + WEB
 *  Control de herramientas con histórico persistente
 * ============================================================
 */

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

// ============================================================
// WIFI
// ============================================================
const char* WIFI_SSID = "ELO-FOREST";
const char* WIFI_PASSWORD = "Elo.forest1935.2024";

// ============================================================
// RFID USERS
// ============================================================
struct RFIDUser { 
  const char* uid; 
  const char* name; 
};
const RFIDUser USERS[] = {
  { "1A212828", "Benjamin V." },
  { "A97EC6B2", "Arturo F." },
  { "C29A713D", "Mario M." }
};
const int NUM_USERS = sizeof(USERS) / sizeof(USERS[0]);

// ============================================================
// PINES
// ============================================================
#define RFID_SS_PIN   5
#define RFID_RST_PIN  27

#define PIN_LED_VERDE 26
#define PIN_LED_ROJO  25
#define PIN_BUZZER    33
#define PIN_SOLENOIDE 12

const uint8_t IR_PINS[12] = {
  13, 14, 16, 17,
  21, 22, 32, 34,
  35, 4, 36, 39
};

// ============================================================
// TIEMPOS
// ============================================================
#define TIEMPO_SOLENOIDE      3000
#define TIEMPO_BEEP_OK        1000
#define TIEMPO_ALERTA         3000
#define INTERVALO_PARPADEO    250
#define DEBOUNCE_MS           80

// ============================================================
// MÁXIMO EVENTOS EN HISTORIAL
// ============================================================
#define MAX_EVENTS 100
#define SPIFFS_LOG_FILE "/events.json"

// ============================================================
// ESTRUCTURAS
// ============================================================
struct ToolEvent {
  uint8_t toolId;
  char user[32];           // Quién TOMÓ la herramienta
  char returnedBy[32];     // Quién la DEVOLVIÓ (si es diferente)
  unsigned long timestamp;
  bool taken;              // true = tomada, false = devuelta
};

// ============================================================
// OBJETOS
// ============================================================
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);
AsyncWebServer server(80);

// ============================================================
// ESTADO GLOBAL
// ============================================================
bool sistemaDesbloqueado = false;

// Solenoide (no bloqueante)
bool solenoideActivo = false;
unsigned long solenoideInicio = 0;

// Alerta (no bloqueante)
bool alertaActiva = false;
unsigned long alertaInicio = 0;
unsigned long ultimoParpadeo = 0;
bool estadoAlerta = false;

// Beep (no bloqueante)
bool beepActivo = false;
unsigned long beepInicio = 0;

// IR (sensores)
bool lastIR[12];
unsigned long debounceMs[12];
bool toolTaken[12];
char toolOwner[12][32];  // Quién tomó cada herramienta

// RFID (usuario actual - activo hasta que otro acceda)
char currentUser[32] = "";

// Estado de herramientas activas (para mostrar en web)
struct ActiveTool {
  uint8_t toolId;
  char owner[32];
  unsigned long takenTime;
};
ActiveTool activeTakings[12];
int activeTakingsCount = 0;

// Eventos en RAM (últimos 12)
ToolEvent lastEvents[12];

// ============================================================
// SPIFFS - GESTIÓN DE EVENTOS
// ============================================================
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("[SPIFFS] Error al inicializar");
    return;
  }
  Serial.println("[SPIFFS] Inicializado correctamente");
  
  // Crear archivo si no existe
  if (!SPIFFS.exists(SPIFFS_LOG_FILE)) {
    File f = SPIFFS.open(SPIFFS_LOG_FILE, "w");
    f.print("[]");
    f.close();
    Serial.println("[SPIFFS] Archivo de eventos creado");
  }
}

void logEvent(uint8_t toolId, const char* user, const char* returnedBy, bool taken) {
  File file = SPIFFS.open(SPIFFS_LOG_FILE, "r");
  if (!file) {
    Serial.println("[SPIFFS] Error al leer archivo");
    return;
  }

  // Leer eventos existentes
  DynamicJsonDocument doc(8192);
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    doc.clear();
    doc = JsonArray();
  }

  JsonArray arr = doc.as<JsonArray>();

  // Crear nuevo evento
  JsonObject newEvent = arr.createNestedObject();
  newEvent["toolId"] = toolId;
  newEvent["user"] = user;  // Quién TOMÓ
  newEvent["returnedBy"] = returnedBy ? returnedBy : "";  // Quién DEVOLVIÓ
  newEvent["timestamp"] = millis() / 1000;
  newEvent["taken"] = taken;

  // Limitar a MAX_EVENTS más recientes
  if (arr.size() > MAX_EVENTS) {
    // Mantener solo los últimos MAX_EVENTS
    JsonArray newArr;
    for (int i = arr.size() - MAX_EVENTS; i < arr.size(); i++) {
      // Crear nueva estructura con últimos eventos
    }
  }

  // Guardar
  file = SPIFFS.open(SPIFFS_LOG_FILE, "w");
  serializeJson(arr, file);
  file.close();

  if (taken) {
    Serial.printf("[LOG] Tool %d - TOMADA por %s\n", toolId, user);
  } else {
    Serial.printf("[LOG] Tool %d - DEVUELTA por %s (tomada por %s)\n", toolId, returnedBy, user);
  }
}

void getAllEvents(JsonArray& arr) {
  File file = SPIFFS.open(SPIFFS_LOG_FILE, "r");
  if (!file) return;

  DynamicJsonDocument doc(8192);
  deserializeJson(doc, file);
  file.close();

  JsonArray events = doc.as<JsonArray>();
  for (JsonObject event : events) {
    arr.add(event);
  }
}

// ============================================================
// UTILIDADES
// ============================================================
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

// ============================================================
// ACCESO
// ============================================================
void accesoPermitido(const char* user) {
  digitalWrite(PIN_LED_VERDE, HIGH);
  digitalWrite(PIN_LED_ROJO, LOW);

  // Beep
  beepActivo = true;
  beepInicio = millis();
  digitalWrite(PIN_BUZZER, HIGH);

  // Solenoide
  solenoideActivo = true;
  solenoideInicio = millis();
  digitalWrite(PIN_SOLENOIDE, HIGH);

  // Guardar usuario actual (activo hasta que otro acceda)
  strncpy(currentUser, user, sizeof(currentUser) - 1);

  sistemaDesbloqueado = true;

  Serial.printf("[ACCESO] %s - Ahora es el usuario activo\n", user);
}

void accesoDenegado() {
  alertaActiva = true;
  alertaInicio = millis();
  ultimoParpadeo = millis();
  estadoAlerta = true;

  sistemaDesbloqueado = false;

  Serial.println("[DENEGADO] Usuario no autorizado");
}

// ============================================================
// RFID
// ============================================================
void handleRFID() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) 
    return;

  String uid = uidToString(rfid.uid.uidByte, rfid.uid.size);
  const char* user = findUser(uid);

  if (user) {
    accesoPermitido(user);
  } else {
    accesoDenegado();
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ============================================================
// IR (SENSORES DE HERRAMIENTAS)
// ============================================================
void handleIR() {
  // NO requiere usuario autenticado (la herramienta "recuerda" quién la tomó)
  
  for (int i = 0; i < 12; i++) {
    // Leer pin (LOW = objeto presente, HIGH = ausente)
    bool reading = (digitalRead(IR_PINS[i]) == LOW);

    // Debounce
    if (reading == lastIR[i]) {
      debounceMs[i] = millis();
      continue;
    }

    if ((millis() - debounceMs[i]) < DEBOUNCE_MS) 
      continue;

    // Estado cambió después de debounce
    lastIR[i] = reading;

    if (!reading && !toolTaken[i]) {
      // HERRAMIENTA TOMADA (estaba LOW = presente, ahora HIGH = ausente)
      
      toolTaken[i] = true;
      strncpy(toolOwner[i], currentUser, sizeof(toolOwner[i]) - 1);
      
      // Guardar en tabla de activos
      if (activeTakingsCount < 12) {
        activeTakings[activeTakingsCount].toolId = i;
        strncpy(activeTakings[activeTakingsCount].owner, currentUser, 31);
        activeTakings[activeTakingsCount].takenTime = millis() / 1000;
        activeTakingsCount++;
      }
      
      // Guardar evento en SPIFFS
      lastEvents[i].toolId = i;
      strncpy(lastEvents[i].user, currentUser, sizeof(lastEvents[i].user) - 1);
      lastEvents[i].returnedBy[0] = '\0';  // Inicializar vacío
      lastEvents[i].timestamp = millis() / 1000;
      lastEvents[i].taken = true;
      logEvent(i, currentUser, "", true);

      Serial.printf("[HERRAMIENTA %d] TOMADA por %s\n", i, currentUser);

    } else if (reading && toolTaken[i]) {
      // HERRAMIENTA DEVUELTA (estaba HIGH = ausente, ahora LOW = presente)
      
      char owner[32];
      strncpy(owner, toolOwner[i], sizeof(owner) - 1);
      
      char whoReturned[32];
      strncpy(whoReturned, currentUser, sizeof(whoReturned) - 1);
      
      toolTaken[i] = false;
      toolOwner[i][0] = '\0';
      
      // Remover de tabla de activos
      for (int j = 0; j < activeTakingsCount; j++) {
        if (activeTakings[j].toolId == i) {
          for (int k = j; k < activeTakingsCount - 1; k++) {
            activeTakings[k] = activeTakings[k + 1];
          }
          activeTakingsCount--;
          break;
        }
      }
      
      // Guardar evento en SPIFFS con quién la devolvió
      lastEvents[i].toolId = i;
      strncpy(lastEvents[i].user, owner, sizeof(lastEvents[i].user) - 1);
      strncpy(lastEvents[i].returnedBy, whoReturned, sizeof(lastEvents[i].returnedBy) - 1);
      lastEvents[i].timestamp = millis() / 1000;
      lastEvents[i].taken = false;
      logEvent(i, owner, whoReturned, false);

      Serial.printf("[HERRAMIENTA %d] DEVUELTA por %s (tomada por %s)\n", i, whoReturned, owner);
    }
  }
}

// ============================================================
// UPDATE NO BLOQUEANTE
// ============================================================
void updateSistema() {

  // SOLENOIDE
  if (solenoideActivo && millis() - solenoideInicio > TIEMPO_SOLENOIDE) {
    digitalWrite(PIN_SOLENOIDE, LOW);
    digitalWrite(PIN_LED_VERDE, LOW);
    solenoideActivo = false;
  }

  // BEEP
  if (beepActivo && millis() - beepInicio > TIEMPO_BEEP_OK) {
    digitalWrite(PIN_BUZZER, LOW);
    beepActivo = false;
  }

  // ALERTA (parpadeo)
  if (alertaActiva) {
    if (millis() - ultimoParpadeo > INTERVALO_PARPADEO) {
      estadoAlerta = !estadoAlerta;
      digitalWrite(PIN_LED_ROJO, estadoAlerta);
      digitalWrite(PIN_BUZZER, estadoAlerta);
      ultimoParpadeo = millis();
    }

    if (millis() - alertaInicio > TIEMPO_ALERTA) {
      digitalWrite(PIN_LED_ROJO, LOW);
      digitalWrite(PIN_BUZZER, LOW);
      alertaActiva = false;
    }
  }
}

// ============================================================
// WEB SERVER
// ============================================================
void setupWeb() {
  // JSON con estado de todas las herramientas
  server.on("/api/tools", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(2048);
    
    JsonArray tools = doc.createNestedArray("tools");
    for (int i = 0; i < 12; i++) {
      JsonObject tool = tools.createNestedObject();
      tool["id"] = i;
      tool["present"] = !toolTaken[i];
      tool["owner"] = toolOwner[i];  // Quién la tiene
      tool["lastTime"] = lastEvents[i].timestamp;
    }

    // Herramientas actualmente fuera del taller
    JsonArray active = doc.createNestedArray("activeTakings");
    for (int i = 0; i < activeTakingsCount; i++) {
      JsonObject taking = active.createNestedObject();
      taking["toolId"] = activeTakings[i].toolId;
      taking["owner"] = activeTakings[i].owner;
      taking["takenTime"] = activeTakings[i].takenTime;
      taking["minutesElapsed"] = (millis() / 1000) - activeTakings[i].takenTime;
    }

    doc["systemLocked"] = !sistemaDesbloqueado;
    doc["currentUser"] = currentUser;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // JSON con histórico de eventos
  server.on("/api/events", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(8192);
    JsonArray events = doc.createNestedArray("events");
    
    getAllEvents(events);

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // Limpiar histórico
  server.on("/api/clear-history", HTTP_POST, [](AsyncWebServerRequest *request) {
    File file = SPIFFS.open(SPIFFS_LOG_FILE, "w");
    file.print("[]");
    file.close();
    
    request->send(200, "application/json", "{\"status\":\"ok\"}");
    Serial.println("[SPIFFS] Histórico limpiado");
  });

  // HTML + CSS + JS
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = R"(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ToolTrack</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body { 
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      padding: 20px;
    }
    .container { max-width: 1200px; margin: 0 auto; }
    .header {
      background: white;
      padding: 20px;
      border-radius: 10px;
      margin-bottom: 20px;
      box-shadow: 0 4px 6px rgba(0,0,0,0.1);
    }
    .header h1 { color: #667eea; margin-bottom: 10px; }
    .status {
      display: flex;
      gap: 20px;
      flex-wrap: wrap;
    }
    .status-item {
      display: flex;
      align-items: center;
      gap: 10px;
    }
    .status-dot {
      width: 12px;
      height: 12px;
      border-radius: 50%;
    }
    .status-dot.active { background: #22c55e; }
    .status-dot.inactive { background: #ef4444; }
    .tabs {
      display: flex;
      gap: 10px;
      margin-bottom: 20px;
      border-bottom: 2px solid #e5e7eb;
    }
    .tab-btn {
      background: none;
      border: none;
      padding: 10px 20px;
      cursor: pointer;
      font-size: 14px;
      color: #999;
      border-bottom: 2px solid transparent;
      transition: all 0.3s;
    }
    .tab-btn.active {
      color: #667eea;
      border-bottom-color: #667eea;
    }
    .tab-content {
      display: none;
    }
    .tab-content.active {
      display: block;
    }
    .tools-grid {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(250px, 1fr));
      gap: 15px;
    }
    .tool-card {
      background: white;
      padding: 20px;
      border-radius: 10px;
      box-shadow: 0 4px 6px rgba(0,0,0,0.1);
      border-left: 5px solid #667eea;
    }
    .tool-card.missing {
      border-left-color: #ef4444;
    }
    .tool-id {
      font-size: 12px;
      color: #999;
      margin-bottom: 5px;
    }
    .tool-status {
      font-size: 18px;
      font-weight: bold;
      color: #667eea;
      margin-bottom: 10px;
    }
    .tool-card.missing .tool-status { color: #ef4444; }
    .tool-user {
      font-size: 12px;
      color: #666;
      margin-top: 10px;
    }
    .history-table {
      width: 100%;
      background: white;
      border-radius: 10px;
      overflow: hidden;
      box-shadow: 0 4px 6px rgba(0,0,0,0.1);
    }
    .history-table table {
      width: 100%;
      border-collapse: collapse;
    }
    .history-table th {
      background: #667eea;
      color: white;
      padding: 12px;
      text-align: left;
      font-weight: 600;
    }
    .history-table td {
      padding: 12px;
      border-bottom: 1px solid #e5e7eb;
    }
    .history-table tr:hover {
      background: #f9fafb;
    }
    .badge {
      display: inline-block;
      padding: 4px 8px;
      border-radius: 4px;
      font-size: 12px;
      font-weight: 600;
    }
    .badge.taken {
      background: #fecaca;
      color: #991b1b;
    }
    .badge.returned {
      background: #bbf7d0;
      color: #166534;
    }
    .active-tools-table {
      width: 100%;
      background: white;
      border-radius: 10px;
      overflow: hidden;
      box-shadow: 0 4px 6px rgba(0,0,0,0.1);
      margin-bottom: 20px;
    }
    .active-tools-table table {
      width: 100%;
      border-collapse: collapse;
    }
    .active-tools-table th {
      background: #ef4444;
      color: white;
      padding: 12px;
      text-align: left;
      font-weight: 600;
    }
    .active-tools-table td {
      padding: 12px;
      border-bottom: 1px solid #e5e7eb;
    }
    .active-tools-table tr:hover {
      background: #fef2f2;
    }
    .empty-state {
      text-align: center;
      padding: 30px;
      color: #999;
    }
    .btn {
      background: #667eea;
      color: white;
      border: none;
      padding: 10px 20px;
      border-radius: 5px;
      cursor: pointer;
      font-size: 14px;
      margin-top: 20px;
    }
    .btn:hover { background: #764ba2; }
    .btn.danger {
      background: #ef4444;
    }
    .btn.danger:hover {
      background: #dc2626;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>🔧 ToolTrack</h1>
      <div class="status">
        <div class="status-item">
          <div class="status-dot active"></div>
          <span>Sistema: <strong id="lockStatus">Bloqueado</strong></span>
        </div>
        <div class="status-item">
          <span>Usuario: <strong id="currentUser">-</strong></span>
        </div>
      </div>
    </div>

    <div class="tabs">
      <button class="tab-btn active" onclick="switchTab('tools')">Herramientas</button>
      <button class="tab-btn" onclick="switchTab('history')">Histórico</button>
    </div>

    <!-- TAB: HERRAMIENTAS -->
    <div id="tools" class="tab-content active">
      <h2 style="color: white; margin-bottom: 15px;">⚡ Herramientas en Uso (Fuera del Taller)</h2>
      
      <div class="active-tools-table">
        <table>
          <thead>
            <tr>
              <th>Herramienta</th>
              <th>Tomada por</th>
              <th>Tiempo Transcurrido</th>
            </tr>
          </thead>
          <tbody id="activeTakingsBody"></tbody>
        </table>
      </div>

      <h2 style="color: white; margin-bottom: 15px;">📊 Estado Completo de Herramientas</h2>
      <div class="tools-grid" id="toolsContainer"></div>
      
      <button class="btn" onclick="updateTools()">Actualizar</button>
    </div>

    <!-- TAB: HISTÓRICO -->
    <div id="history" class="tab-content">
      <div class="history-table">
        <table>
          <thead>
            <tr>
              <th>Herramienta</th>
              <th>Acción</th>
              <th>Usuario</th>
              <th>Devolvida por</th>
              <th>Hora</th>
            </tr>
          </thead>
          <tbody id="historyBody"></tbody>
        </table>
      </div>
      <button class="btn danger" onclick="clearHistory()">Limpiar Histórico</button>
      <button class="btn" onclick="updateHistory()">Actualizar</button>
    </div>
  </div>

  <script>
    function switchTab(tab) {
      document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
      document.querySelectorAll('.tab-btn').forEach(el => el.classList.remove('active'));
      document.getElementById(tab).classList.add('active');
      event.target.classList.add('active');
      
      if (tab === 'history') updateHistory();
    }

    async function updateTools() {
      try {
        const res = await fetch('/api/tools');
        const data = await res.json();

        document.getElementById('lockStatus').textContent = data.systemLocked ? 'Bloqueado' : 'Desbloqueado';
        document.getElementById('currentUser').textContent = data.currentUser || '-';

        // HERRAMIENTAS ACTIVAS
        const activeTBody = document.getElementById('activeTakingsBody');
        activeTBody.innerHTML = '';

        if (data.activeTakings && data.activeTakings.length > 0) {
          data.activeTakings.forEach(taking => {
            const row = activeTBody.insertRow();
            const minutesStr = taking.minutesElapsed < 60 
              ? `${taking.minutesElapsed} seg`
              : `${Math.floor(taking.minutesElapsed / 60)} min`;
            
            row.innerHTML = `
              <td><strong>Herramienta ${taking.toolId}</strong></td>
              <td>${taking.owner}</td>
              <td>${minutesStr}</td>
            `;
          });
        } else {
          activeTBody.innerHTML = '<tr><td colspan="3" class="empty-state">Todas las herramientas en el taller ✓</td></tr>';
        }

        // TODAS LAS HERRAMIENTAS
        const container = document.getElementById('toolsContainer');
        container.innerHTML = '';

        data.tools.forEach(tool => {
          const card = document.createElement('div');
          card.className = 'tool-card' + (!tool.present ? ' missing' : '');
          
          const timeStr = tool.lastTime ? new Date(tool.lastTime * 1000).toLocaleTimeString() : '-';
          
          card.innerHTML = `
            <div class="tool-id">Herramienta ${tool.id}</div>
            <div class="tool-status">${tool.present ? '✓ Presente' : '✗ Fuera'}</div>
            <div class="tool-user"><strong>Propietario:</strong> ${tool.owner || '-'}</div>
            <div class="tool-user"><strong>Hora:</strong> ${timeStr}</div>
          `;
          container.appendChild(card);
        });
      } catch (error) {
        console.error('Error:', error);
      }
    }

    async function updateHistory() {
      try {
        const res = await fetch('/api/events');
        const data = await res.json();

        const tbody = document.getElementById('historyBody');
        tbody.innerHTML = '';

        // Mostrar en orden inverso (más recientes primero)
        data.events.reverse().forEach(event => {
          const row = tbody.insertRow();
          const timeStr = new Date(event.timestamp * 1000).toLocaleString();
          
          let action, usuario, devolvida;
          
          if (event.taken) {
            action = '<span class="badge taken">TOMADA</span>';
            usuario = event.user || '-';
            devolvida = '-';
          } else {
            action = '<span class="badge returned">DEVUELTA</span>';
            usuario = event.user || '-';
            devolvida = event.returnedBy || '-';
          }
          
          row.innerHTML = `
            <td><strong>Herramienta ${event.toolId}</strong></td>
            <td>${action}</td>
            <td>${usuario}</td>
            <td>${devolvida}</td>
            <td>${timeStr}</td>
          `;
        });
      } catch (error) {
        console.error('Error:', error);
      }
    }

    async function clearHistory() {
      if (!confirm('¿Limpiar todo el histórico? No se puede deshacer.')) return;
      
      try {
        await fetch('/api/clear-history', { method: 'POST' });
        alert('Histórico limpiado');
        updateHistory();
      } catch (error) {
        console.error('Error:', error);
      }
    }

    // Actualizar cada 3 segundos
    updateTools();
    setInterval(updateTools, 3000);
  </script>
</body>
</html>
    )";
    request->send(200, "text/html", html);
  });

  server.begin();
  Serial.println("[WEB] Servidor iniciado en puerto 80");
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  // GPIO setup
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_ROJO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_SOLENOIDE, OUTPUT);

  // IR pins
  for (int i = 0; i < 12; i++) {
    if (IR_PINS[i] >= 34)
      pinMode(IR_PINS[i], INPUT);
    else
      pinMode(IR_PINS[i], INPUT_PULLUP);

    lastIR[i] = digitalRead(IR_PINS[i]);
    debounceMs[i] = millis();
    toolTaken[i] = false;
    toolOwner[i][0] = '\0';
  }

  activeTakingsCount = 0;

  // SPIFFS
  initSPIFFS();

  // SPI + RFID
  SPI.begin(18, 19, 23, RFID_SS_PIN);
  rfid.PCD_Init();

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(300);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] Conectado: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] Error de conexión");
  }

  setupWeb();

  Serial.println("\n[SISTEMA] ToolTrack iniciado");
  Serial.println("Acerca una tarjeta RFID...\n");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  handleRFID();
  handleIR();
  updateSistema();
  
  delay(10);  // Pequeña pausa para no saturar CPU
}
