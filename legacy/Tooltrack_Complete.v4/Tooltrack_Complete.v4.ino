/*
 * ============================================================
 *  TOOLTRACK + CERRADURA RFID — v4
 *  Correcciones aplicadas:
 *    1. logEvent: recorte real a MAX_EVENTS (SPIFFS no crece infinito)
 *    2. WebSocket reintegrado (actualizaciones en tiempo real)
 *  Diseño: usuario activo sin timeout (hasta que otro acceda)
 * ============================================================
 *
 *  PINOUT RC522 → ESP32:
 *    SDA  → GPIO 5     SCK  → GPIO 18
 *    MOSI → GPIO 23    MISO → GPIO 19
 *    RST  → GPIO 27    GND  → GND     3.3V → 3.3V
 *
 *  CERRADURA:
 *    LED Verde  → GPIO 26
 *    LED Rojo   → GPIO 25
 *    Buzzer     → GPIO 33
 *    Solenoide  → GPIO 12  (usar transistor/relé)
 *
 *  SENSORES IR (DO digital, LOW = presente):
 *    Slot  1→GPIO13   2→GPIO14   3→GPIO16   4→GPIO17
 *    Slot  5→GPIO21   6→GPIO22   7→GPIO32   8→GPIO34
 *    Slot  9→GPIO35  10→GPIO4   11→GPIO36  12→GPIO39
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
const char* WIFI_SSID     = "ELO-FOREST";
const char* WIFI_PASSWORD = "Elo.forest1935.2024";

// ============================================================
// USUARIOS RFID
// ============================================================
struct RFIDUser { const char* uid; const char* name; };
const RFIDUser USERS[] = {
  { "1A212828", "Benjamin V." },
  { "A97EC6B2", "Arturo F."  },
  { "C29A713D", "Mario M."   }
};
const int NUM_USERS = sizeof(USERS) / sizeof(USERS[0]);

// ============================================================
// NOMBRES E ÍCONOS DE HERRAMIENTAS
// ============================================================
const char* TOOL_NAMES[12] = {
  "Martillo",        "Destornillador+", "Llave inglesa", "Alicate",
  "Sierra manual",   "Cinta metrica",   "Taladro",       "Nivel",
  "Llave torx",      "Tijeras acero",   "Desarmador plano", "Pinzas punta"
};
const char* TOOL_ICONS[12] = {
  "🔨","🔧","🔩","🪛","🪚","📏","🔌","📐","🔑","✂️","🪛","🔧"
};

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
  35,  4, 36, 39
};

// ============================================================
// TIEMPOS
// ============================================================
#define TIEMPO_SOLENOIDE    3000
#define TIEMPO_BEEP_OK      1000
#define TIEMPO_ALERTA       3000
#define INTERVALO_PARPADEO   250
#define DEBOUNCE_MS           80

// ============================================================
// SPIFFS
// ============================================================
#define MAX_EVENTS       100
#define SPIFFS_LOG_FILE  "/events.json"

// ============================================================
// OBJETOS
// ============================================================
MFRC522        rfid(RFID_SS_PIN, RFID_RST_PIN);
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");          // ← WebSocket reintegrado

// ============================================================
// ESTADO GLOBAL
// ============================================================
char     currentUser[32] = "";     // Usuario activo (sin timeout)

// Solenoide (no bloqueante)
bool          solenoideActivo = false;
unsigned long solenoideInicio = 0;

// Alerta (no bloqueante)
bool          alertaActiva    = false;
unsigned long alertaInicio    = 0;
unsigned long ultimoParpadeo  = 0;
bool          estadoAlerta    = false;

// Beep (no bloqueante)
bool          beepActivo  = false;
unsigned long beepInicio  = 0;

// IR
bool          lastIR[12];
unsigned long debounceMs[12];
bool          toolTaken[12];
char          toolOwner[12][32];

// ============================================================
// SPIFFS — INICIALIZAR
// ============================================================
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("[SPIFFS] Error al inicializar");
    return;
  }
  if (!SPIFFS.exists(SPIFFS_LOG_FILE)) {
    File f = SPIFFS.open(SPIFFS_LOG_FILE, "w");
    f.print("[]");
    f.close();
  }
  Serial.println("[SPIFFS] Listo.");
}

// ============================================================
// SPIFFS — GUARDAR EVENTO (con recorte real a MAX_EVENTS)
// ============================================================
void logEvent(uint8_t toolId, const char* user,
              const char* returnedBy, bool taken) {

  // --- Leer array existente ---
  DynamicJsonDocument doc(16384);
  {
    File f = SPIFFS.open(SPIFFS_LOG_FILE, "r");
    if (f) {
      DeserializationError err = deserializeJson(doc, f);
      f.close();
      if (err || !doc.is<JsonArray>()) doc.set(JsonArray());
    } else {
      doc.set(JsonArray());
    }
  }

  JsonArray arr = doc.as<JsonArray>();

  // --- Agregar nuevo evento ---
  JsonObject ev = arr.createNestedObject();
  ev["toolId"]     = toolId;
  ev["user"]       = user;
  ev["returnedBy"] = returnedBy ? returnedBy : "";
  ev["timestamp"]  = millis() / 1000;
  ev["taken"]      = taken;

  // --- Recorte real: si supera MAX_EVENTS, eliminar los más antiguos ---
  if ((int)arr.size() > MAX_EVENTS) {
    int excess = (int)arr.size() - MAX_EVENTS;

    // Copiar solo los últimos MAX_EVENTS en un documento auxiliar
    DynamicJsonDocument trimmed(16384);
    JsonArray newArr = trimmed.to<JsonArray>();

    for (int i = excess; i < (int)arr.size(); i++) {
      newArr.add(arr[i]);
    }

    // Guardar versión recortada
    File f = SPIFFS.open(SPIFFS_LOG_FILE, "w");
    serializeJson(newArr, f);
    f.close();

    Serial.printf("[SPIFFS] Recortado a %d eventos.\n", MAX_EVENTS);
    return;   // Ya guardamos, salir
  }

  // --- Guardar sin recorte ---
  File f = SPIFFS.open(SPIFFS_LOG_FILE, "w");
  serializeJson(arr, f);
  f.close();

  if (taken)
    Serial.printf("[LOG] Tool %d TOMADA por %s\n", toolId, user);
  else
    Serial.printf("[LOG] Tool %d DEVUELTA por %s (tomada por %s)\n",
                  toolId, returnedBy, user);
}

// ============================================================
// SPIFFS — LEER TODOS LOS EVENTOS
// ============================================================
void getAllEvents(JsonArray& out) {
  File f = SPIFFS.open(SPIFFS_LOG_FILE, "r");
  if (!f) return;

  DynamicJsonDocument doc(16384);
  if (deserializeJson(doc, f) == DeserializationError::Ok && doc.is<JsonArray>()) {
    for (JsonObject ev : doc.as<JsonArray>()) out.add(ev);
  }
  f.close();
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
// WEBSOCKET — enviar estado completo a un cliente o a todos
// ============================================================
void sendFullState(AsyncWebSocketClient* client = nullptr) {
  DynamicJsonDocument doc(4096);
  doc["type"]        = "full_state";
  doc["currentUser"] = currentUser;

  JsonArray arr = doc.createNestedArray("tools");
  for (int i = 0; i < 12; i++) {
    JsonObject o = arr.createNestedObject();
    o["slot"]    = i + 1;
    o["name"]    = TOOL_NAMES[i];
    o["icon"]    = TOOL_ICONS[i];
    o["present"] = !toolTaken[i];
    o["owner"]   = toolOwner[i];
  }

  String json;
  serializeJson(doc, json);
  if (client) client->text(json);
  else        ws.textAll(json);
}

// Evento puntual (herramienta tomada o devuelta)
void sendToolEvent(int i, const char* evType,
                   const char* taker, const char* returner) {
  DynamicJsonDocument doc(512);
  doc["type"]       = "tool_event";
  doc["slot"]       = i + 1;
  doc["name"]       = TOOL_NAMES[i];
  doc["icon"]       = TOOL_ICONS[i];
  doc["event"]      = evType;       // "salida" | "entrada"
  doc["present"]    = !toolTaken[i];
  doc["owner"]      = taker;
  doc["returnedBy"] = returner ? returner : "";

  String json;
  serializeJson(doc, json);
  ws.textAll(json);
}

// Evento de acceso RFID (para que el dashboard actualice el usuario activo)
void sendAccessEvent(const char* user, bool granted) {
  DynamicJsonDocument doc(256);
  doc["type"]        = "access_event";
  doc["user"]        = user;
  doc["granted"]     = granted;
  doc["currentUser"] = currentUser;

  String json;
  serializeJson(doc, json);
  ws.textAll(json);
}

// ============================================================
// WEBSOCKET — manejador de eventos de conexión
// ============================================================
void onWsEvent(AsyncWebSocket* srv, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("[WS] Cliente #%u conectado\n", client->id());
    sendFullState(client);   // Enviar estado completo al nuevo cliente
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[WS] Cliente #%u desconectado\n", client->id());
  }
}

// ============================================================
// ACCESO
// ============================================================
void accesoPermitido(const char* user) {
  digitalWrite(PIN_LED_VERDE, HIGH);
  digitalWrite(PIN_LED_ROJO,  LOW);

  beepActivo    = true;
  beepInicio    = millis();
  digitalWrite(PIN_BUZZER, HIGH);

  solenoideActivo = true;
  solenoideInicio = millis();
  digitalWrite(PIN_SOLENOIDE, HIGH);

  strncpy(currentUser, user, sizeof(currentUser) - 1);

  Serial.printf("[ACCESO] Usuario activo: %s\n", currentUser);
  sendAccessEvent(user, true);   // ← notificar dashboard vía WS
}

void accesoDenegado(const char* uid) {
  alertaActiva   = true;
  alertaInicio   = millis();
  ultimoParpadeo = millis();
  estadoAlerta   = true;

  Serial.printf("[DENEGADO] UID: %s\n", uid);
  sendAccessEvent(uid, false);   // ← notificar dashboard vía WS
}

// ============================================================
// RFID
// ============================================================
void handleRFID() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  String uid      = uidToString(rfid.uid.uidByte, rfid.uid.size);
  const char* user = findUser(uid);

  if (user) accesoPermitido(user);
  else      accesoDenegado(uid.c_str());

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ============================================================
// IR — SENSORES DE HERRAMIENTAS
// ============================================================
void handleIR() {
  for (int i = 0; i < 12; i++) {
    bool reading = (digitalRead(IR_PINS[i]) == LOW);   // LOW = presente

    if (reading == lastIR[i]) { debounceMs[i] = millis(); continue; }
    if ((millis() - debounceMs[i]) < DEBOUNCE_MS) continue;

    lastIR[i] = reading;

    if (!reading && !toolTaken[i]) {
      // ── HERRAMIENTA TOMADA ──
      toolTaken[i] = true;
      strncpy(toolOwner[i], currentUser, sizeof(toolOwner[i]) - 1);

      logEvent(i, currentUser, "", true);
      sendToolEvent(i, "salida", currentUser, nullptr);   // ← WS

    } else if (reading && toolTaken[i]) {
      // ── HERRAMIENTA DEVUELTA ──
      char owner[32];
      strncpy(owner, toolOwner[i], sizeof(owner) - 1);

      toolTaken[i]    = false;
      toolOwner[i][0] = '\0';

      logEvent(i, owner, currentUser, false);
      sendToolEvent(i, "entrada", owner, currentUser);   // ← WS
    }
  }
}

// ============================================================
// UPDATE NO BLOQUEANTE (solenoide, beep, alerta)
// ============================================================
void updateSistema() {
  // Solenoide
  if (solenoideActivo && millis() - solenoideInicio > TIEMPO_SOLENOIDE) {
    digitalWrite(PIN_SOLENOIDE, LOW);
    digitalWrite(PIN_LED_VERDE, LOW);
    solenoideActivo = false;
  }

  // Beep
  if (beepActivo && millis() - beepInicio > TIEMPO_BEEP_OK) {
    digitalWrite(PIN_BUZZER, LOW);
    beepActivo = false;
  }

  // Alerta (parpadeo LED rojo + buzzer)
  if (alertaActiva) {
    if (millis() - ultimoParpadeo > INTERVALO_PARPADEO) {
      estadoAlerta = !estadoAlerta;
      digitalWrite(PIN_LED_ROJO, estadoAlerta);
      digitalWrite(PIN_BUZZER,   estadoAlerta);
      ultimoParpadeo = millis();
    }
    if (millis() - alertaInicio > TIEMPO_ALERTA) {
      digitalWrite(PIN_LED_ROJO, LOW);
      digitalWrite(PIN_BUZZER,   LOW);
      alertaActiva = false;
    }
  }
}

// ============================================================
// WEB SERVER + WEBSOCKET
// ============================================================
void setupWeb() {
  // ── WebSocket ──
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // ── API: estado de herramientas ──
  server.on("/api/tools", HTTP_GET, [](AsyncWebServerRequest* req) {
    DynamicJsonDocument doc(4096);
    doc["currentUser"] = currentUser;

    JsonArray tools = doc.createNestedArray("tools");
    for (int i = 0; i < 12; i++) {
      JsonObject t = tools.createNestedObject();
      t["id"]      = i;
      t["name"]    = TOOL_NAMES[i];
      t["icon"]    = TOOL_ICONS[i];
      t["present"] = !toolTaken[i];
      t["owner"]   = toolOwner[i];
    }

    String res; serializeJson(doc, res);
    req->send(200, "application/json", res);
  });

  // ── API: histórico ──
  server.on("/api/events", HTTP_GET, [](AsyncWebServerRequest* req) {
    DynamicJsonDocument doc(16384);
    JsonArray events = doc.createNestedArray("events");
    getAllEvents(events);

    String res; serializeJson(doc, res);
    req->send(200, "application/json", res);
  });

  // ── API: limpiar histórico ──
  server.on("/api/clear-history", HTTP_POST, [](AsyncWebServerRequest* req) {
    File f = SPIFFS.open(SPIFFS_LOG_FILE, "w");
    f.print("[]");
    f.close();
    req->send(200, "application/json", "{\"status\":\"ok\"}");
    Serial.println("[SPIFFS] Histórico limpiado.");
  });

  // ── Dashboard HTML ──
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    // Obtener IP para construir la URL del WebSocket
    String ip = WiFi.localIP().toString();

    String html = R"rawhtml(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ToolTrack</title>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Barlow:wght@400;600;700&display=swap');
    :root {
      --bg:       #0d1117;
      --surface:  #161b22;
      --border:   #30363d;
      --accent:   #58a6ff;
      --green:    #3fb950;
      --red:      #f85149;
      --yellow:   #d29922;
      --text:     #c9d1d9;
      --muted:    #8b949e;
      --mono:     'Share Tech Mono', monospace;
      --sans:     'Barlow', sans-serif;
    }
    * { margin:0; padding:0; box-sizing:border-box; }
    body { background:var(--bg); color:var(--text); font-family:var(--sans); min-height:100vh; }

    /* ── HEADER ── */
    header {
      display:flex; align-items:center; justify-content:space-between;
      padding:16px 24px;
      border-bottom:1px solid var(--border);
      background:var(--surface);
    }
    header h1 { font-family:var(--mono); font-size:1.25rem; letter-spacing:.05em; color:var(--accent); }
    .ws-badge {
      display:flex; align-items:center; gap:8px;
      font-size:.75rem; color:var(--muted);
    }
    .ws-dot {
      width:8px; height:8px; border-radius:50%;
      background:var(--red); transition:background .4s;
    }
    .ws-dot.connected { background:var(--green); }

    /* ── USER BAR ── */
    .user-bar {
      display:flex; align-items:center; gap:12px;
      padding:10px 24px;
      background:#1a2230;
      border-bottom:1px solid var(--border);
      font-size:.85rem;
    }
    .user-label { color:var(--muted); }
    .user-name  { color:var(--accent); font-weight:700; font-family:var(--mono); }

    /* ── TABS ── */
    .tabs { display:flex; border-bottom:1px solid var(--border); background:var(--surface); }
    .tab-btn {
      padding:12px 24px; border:none; background:none;
      color:var(--muted); cursor:pointer; font-family:var(--sans); font-size:.9rem;
      border-bottom:2px solid transparent; transition:all .2s;
    }
    .tab-btn:hover  { color:var(--text); }
    .tab-btn.active { color:var(--accent); border-bottom-color:var(--accent); }

    /* ── CONTENIDO ── */
    .content { padding:24px; max-width:1400px; margin:0 auto; }
    .tab-pane { display:none; }
    .tab-pane.active { display:block; }

    /* ── GRID DE HERRAMIENTAS ── */
    .tools-grid {
      display:grid;
      grid-template-columns:repeat(auto-fill, minmax(200px, 1fr));
      gap:12px;
    }
    .tool-card {
      background:var(--surface);
      border:1px solid var(--border);
      border-radius:8px;
      padding:16px;
      transition:border-color .3s, transform .15s;
    }
    .tool-card:hover { transform:translateY(-2px); }
    .tool-card.present { border-left:3px solid var(--green); }
    .tool-card.absent  { border-left:3px solid var(--red); }
    .tool-card .icon   { font-size:1.6rem; margin-bottom:8px; }
    .tool-card .slot   { font-size:.7rem; color:var(--muted); font-family:var(--mono); }
    .tool-card .name   { font-weight:600; font-size:.9rem; margin:4px 0 8px; }
    .tool-card .status {
      display:inline-block; padding:2px 8px; border-radius:4px;
      font-size:.75rem; font-weight:700; font-family:var(--mono);
    }
    .tool-card.present .status { background:#1a3a2a; color:var(--green); }
    .tool-card.absent  .status { background:#3a1a1a; color:var(--red);   }
    .tool-card .owner  { margin-top:8px; font-size:.75rem; color:var(--muted); }

    /* ── LOG EN TIEMPO REAL ── */
    .live-log {
      background:var(--surface); border:1px solid var(--border);
      border-radius:8px; overflow:hidden; margin-bottom:20px;
    }
    .live-log-header {
      padding:10px 16px; background:#1a2230;
      font-size:.8rem; color:var(--muted); font-family:var(--mono);
      border-bottom:1px solid var(--border);
      display:flex; justify-content:space-between;
    }
    .live-log-body { max-height:180px; overflow-y:auto; }
    .log-row {
      display:flex; align-items:center; gap:12px;
      padding:8px 16px; border-bottom:1px solid var(--border);
      font-size:.8rem; font-family:var(--mono);
      animation: fadeIn .3s ease;
    }
    @keyframes fadeIn { from { opacity:0; transform:translateX(-6px); } to { opacity:1; } }
    .log-row .ts    { color:var(--muted); min-width:70px; }
    .log-row .ev    { min-width:70px; font-weight:700; }
    .log-row .ev.salida  { color:var(--red);    }
    .log-row .ev.entrada { color:var(--green);  }
    .log-row .ev.acceso  { color:var(--accent); }
    .log-row .ev.denied  { color:var(--yellow); }
    .log-row .msg   { color:var(--text); }

    /* ── TABLA HISTÓRICO ── */
    .hist-table { width:100%; border-collapse:collapse; font-size:.85rem; }
    .hist-table th {
      text-align:left; padding:10px 12px;
      background:#1a2230; color:var(--muted);
      font-weight:600; font-family:var(--mono);
      border-bottom:1px solid var(--border);
    }
    .hist-table td { padding:10px 12px; border-bottom:1px solid var(--border); }
    .hist-table tr:hover td { background:#1a2230; }
    .badge {
      display:inline-block; padding:2px 7px; border-radius:4px;
      font-size:.72rem; font-weight:700; font-family:var(--mono);
    }
    .badge.tomada   { background:#3a1a1a; color:var(--red);   }
    .badge.devuelta { background:#1a3a2a; color:var(--green); }

    .section-title {
      font-size:.8rem; font-family:var(--mono);
      color:var(--muted); text-transform:uppercase;
      letter-spacing:.1em; margin-bottom:12px;
    }
    .btn {
      padding:8px 18px; border-radius:6px; border:none;
      cursor:pointer; font-family:var(--sans); font-size:.85rem;
      font-weight:600; margin-top:16px; margin-right:8px;
      transition:opacity .2s;
    }
    .btn:hover { opacity:.8; }
    .btn-primary { background:var(--accent); color:#000; }
    .btn-danger  { background:var(--red);    color:#fff; }

    .empty { text-align:center; padding:32px; color:var(--muted); font-size:.9rem; }
  </style>
</head>
<body>

<header>
  <h1>⚙ ToolTrack</h1>
  <div class="ws-badge">
    <div class="ws-dot" id="wsDot"></div>
    <span id="wsStatus">Conectando...</span>
  </div>
</header>

<div class="user-bar">
  <span class="user-label">Usuario activo:</span>
  <span class="user-name" id="currentUser">—</span>
</div>

<div class="tabs">
  <button class="tab-btn active" onclick="switchTab('tools', this)">Herramientas</button>
  <button class="tab-btn"        onclick="switchTab('history', this)">Histórico</button>
</div>

<div class="content">

  <!-- ── TAB: HERRAMIENTAS ── -->
  <div id="tools" class="tab-pane active">

    <p class="section-title" style="margin-bottom:16px;">Actividad reciente</p>
    <div class="live-log">
      <div class="live-log-header">
        <span>Log en tiempo real</span>
        <span id="logCount">0 eventos</span>
      </div>
      <div class="live-log-body" id="liveLog"></div>
    </div>

    <p class="section-title">Estado de herramientas</p>
    <div class="tools-grid" id="toolsGrid"></div>
  </div>

  <!-- ── TAB: HISTÓRICO ── -->
  <div id="history" class="tab-pane">
    <p class="section-title">Historial completo (SPIFFS)</p>
    <div style="background:var(--surface);border:1px solid var(--border);border-radius:8px;overflow:hidden;">
      <table class="hist-table">
        <thead>
          <tr>
            <th>Herramienta</th>
            <th>Acción</th>
            <th>Tomada por</th>
            <th>Devuelta por</th>
            <th>Hora</th>
          </tr>
        </thead>
        <tbody id="histBody"></tbody>
      </table>
    </div>
    <button class="btn btn-danger"   onclick="clearHistory()">Limpiar histórico</button>
    <button class="btn btn-primary"  onclick="loadHistory()">Actualizar</button>
  </div>

</div>

<script>
  // ── ESTADO LOCAL ──────────────────────────────────────────
  const toolState = {};    // slot → { name, icon, present, owner }
  let logCount = 0;

  // ── WEBSOCKET ─────────────────────────────────────────────
  const WS_URL = `ws://${location.hostname}/ws`;
  let   socket;

  function connectWS() {
    socket = new WebSocket(WS_URL);

    socket.onopen = () => {
      document.getElementById('wsDot').classList.add('connected');
      document.getElementById('wsStatus').textContent = 'En línea';
    };

    socket.onclose = () => {
      document.getElementById('wsDot').classList.remove('connected');
      document.getElementById('wsStatus').textContent = 'Reconectando...';
      setTimeout(connectWS, 3000);
    };

    socket.onmessage = (ev) => {
      const msg = JSON.parse(ev.data);

      if (msg.type === 'full_state') {
        document.getElementById('currentUser').textContent = msg.currentUser || '—';
        msg.tools.forEach(t => {
          toolState[t.slot] = t;
        });
        renderGrid();

      } else if (msg.type === 'tool_event') {
        toolState[msg.slot] = {
          name: msg.name, icon: msg.icon,
          present: msg.present, owner: msg.owner
        };
        renderGrid();

        const action = msg.event === 'salida' ? 'TOMADA' : 'DEVUELTA';
        const who    = msg.event === 'salida'
                     ? msg.owner
                     : (msg.returnedBy || msg.owner);
        addLog(msg.event, `${msg.icon} ${msg.name} — ${action} por ${who}`);

      } else if (msg.type === 'access_event') {
        if (msg.granted) {
          document.getElementById('currentUser').textContent = msg.currentUser;
          addLog('acceso', `✔ ${msg.user} accedió al sistema`);
        } else {
          addLog('denied', `✘ Tarjeta desconocida: ${msg.user}`);
        }
      }
    };
  }

  // ── RENDER GRID ───────────────────────────────────────────
  function renderGrid() {
    const grid = document.getElementById('toolsGrid');
    grid.innerHTML = '';
    for (let slot = 1; slot <= 12; slot++) {
      const t = toolState[slot];
      if (!t) continue;
      const cls = t.present ? 'present' : 'absent';
      const card = document.createElement('div');
      card.className = `tool-card ${cls}`;
      card.innerHTML = `
        <div class="icon">${t.icon}</div>
        <div class="slot">Slot ${slot}</div>
        <div class="name">${t.name}</div>
        <span class="status">${t.present ? '● PRESENTE' : '○ AUSENTE'}</span>
        <div class="owner">${t.present ? '' : (t.owner ? '→ ' + t.owner : '')}</div>
      `;
      grid.appendChild(card);
    }
  }

  // ── LOG EN TIEMPO REAL ─────────────────────────────────────
  function addLog(type, msg) {
    logCount++;
    document.getElementById('logCount').textContent = `${logCount} eventos`;

    const now  = new Date();
    const ts   = now.toTimeString().slice(0,8);
    const body = document.getElementById('liveLog');

    const row  = document.createElement('div');
    row.className = 'log-row';
    row.innerHTML = `
      <span class="ts">${ts}</span>
      <span class="ev ${type}">${type.toUpperCase()}</span>
      <span class="msg">${msg}</span>
    `;
    body.prepend(row);

    // Limitar a 50 filas en pantalla
    while (body.children.length > 50) body.removeChild(body.lastChild);
  }

  // ── HISTÓRICO (fetch REST) ────────────────────────────────
  async function loadHistory() {
    const res  = await fetch('/api/events');
    const data = await res.json();
    const tbody = document.getElementById('histBody');
    tbody.innerHTML = '';

    if (!data.events || data.events.length === 0) {
      tbody.innerHTML = '<tr><td colspan="5" class="empty">Sin eventos registrados</td></tr>';
      return;
    }

    [...data.events].reverse().forEach(ev => {
      const tr  = document.createElement('tr');
      const ts  = new Date(ev.timestamp * 1000).toLocaleString('es');
      const badge = ev.taken
        ? '<span class="badge tomada">TOMADA</span>'
        : '<span class="badge devuelta">DEVUELTA</span>';
      tr.innerHTML = `
        <td>${ev.toolId + 1}. ${TOOL_NAMES[ev.toolId] || ''}</td>
        <td>${badge}</td>
        <td>${ev.user || '—'}</td>
        <td>${ev.returnedBy || '—'}</td>
        <td>${ts}</td>
      `;
      tbody.appendChild(tr);
    });
  }

  async function clearHistory() {
    if (!confirm('¿Limpiar todo el histórico? Esta acción no se puede deshacer.')) return;
    await fetch('/api/clear-history', { method:'POST' });
    loadHistory();
  }

  // ── TABS ──────────────────────────────────────────────────
  function switchTab(id, btn) {
    document.querySelectorAll('.tab-pane').forEach(p => p.classList.remove('active'));
    document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
    document.getElementById(id).classList.add('active');
    btn.classList.add('active');
    if (id === 'history') loadHistory();
  }

  // Nombres locales para la tabla de histórico
  const TOOL_NAMES = )rawhtml";

    // Inyectar array de nombres desde C++
    html += "[";
    for (int i = 0; i < 12; i++) {
      html += "\"";
      html += TOOL_NAMES[i];
      html += "\"";
      if (i < 11) html += ",";
    }
    html += R"rawhtml(];

  // ── INICIO ────────────────────────────────────────────────
  connectWS();
</script>
</body>
</html>
)rawhtml";

    req->send(200, "text/html", html);
  });

  server.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "text/plain", "No encontrado");
  });

  server.begin();
  Serial.println("[WEB] Servidor HTTP + WebSocket activo.");
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n══════════════════════════════");
  Serial.println("  TOOLTRACK v4 — Iniciando");
  Serial.println("══════════════════════════════");

  // GPIO
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_ROJO,  OUTPUT);
  pinMode(PIN_BUZZER,    OUTPUT);
  pinMode(PIN_SOLENOIDE, OUTPUT);

  // Sensores IR
  for (int i = 0; i < 12; i++) {
    // GPIO 34,35,36,39 son input-only en ESP32 (sin pullup interno)
    if (IR_PINS[i] >= 34) pinMode(IR_PINS[i], INPUT);
    else                   pinMode(IR_PINS[i], INPUT_PULLUP);

    lastIR[i]       = (digitalRead(IR_PINS[i]) == LOW);
    debounceMs[i]   = millis();
    toolTaken[i]    = false;
    toolOwner[i][0] = '\0';

    Serial.printf("  Slot %02d [%-18s] %s\n",
      i + 1, TOOL_NAMES[i], lastIR[i] ? "PRESENTE" : "AUSENTE");
  }

  // SPIFFS
  initSPIFFS();

  // SPI + RFID
  SPI.begin(18, 19, 23, RFID_SS_PIN);
  rfid.PCD_Init();
  Serial.println("[RFID] RC522 listo.");

  // WiFi
  Serial.printf("[WiFi] Conectando a %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int att = 0;
  while (WiFi.status() != WL_CONNECTED && att < 40) {
    delay(500); Serial.print("."); att++;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[WiFi] Falló. Reiniciando...");
    delay(3000); ESP.restart();
  }
  Serial.printf("\n[WiFi] IP: http://%s\n", WiFi.localIP().toString().c_str());

  setupWeb();

  Serial.println("\n[SISTEMA] Listo. Acerca una tarjeta RFID...\n");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  handleRFID();
  handleIR();
  updateSistema();

  // Limpiar clientes WS desconectados cada 5 s
  static uint32_t lastClean = 0;
  if (millis() - lastClean > 5000) {
    ws.cleanupClients();
    lastClean = millis();
  }

  delay(5);
}
