#include "webserver.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_random.h>

#include <cstring>

#include "config.h"
#include "state.h"
#include "storage.h"
#include "time_sync.h"

namespace tt {
namespace webserver {

namespace {

AsyncWebServer s_server(80);
AsyncWebSocket s_ws("/ws");

char s_token[33] = {};
uint32_t s_tokenExpires = 0;
uint8_t s_failedAttempts = 0;
uint32_t s_lockoutUntil = 0;
uint32_t s_lastCleanup = 0;
bool s_restartRequested = false;
bool s_forceAnalysis = false;
bool s_wifiResetRequested = false;

void copyText(char* destination, size_t size, const char* source) {
  if (size == 0) return;
  std::strncpy(destination, source ? source : "", size - 1);
  destination[size - 1] = '\0';
}

void sendJson(AsyncWebServerRequest* request, int code, const String& body) {
  request->send(code, "application/json", body);
}

void sendError(AsyncWebServerRequest* request, int code, const char* message) {
  JsonDocument document;
  document["error"] = message;
  String output;
  serializeJson(document, output);
  sendJson(request, code, output);
}

void generateToken() {
  uint8_t random[16];
  esp_fill_random(random, sizeof(random));
  for (size_t i = 0; i < sizeof(random); i++) {
    snprintf(s_token + (i * 2), 3, "%02x", random[i]);
  }
  s_token[32] = '\0';
  s_tokenExpires = millis() + cfg::ADMIN_SESSION_MS;
}

bool isAuthenticated(AsyncWebServerRequest* request) {
  if (s_token[0] == '\0') return false;
  if (static_cast<int32_t>(millis() - s_tokenExpires) >= 0) {
    s_token[0] = '\0';
    return false;
  }

  const AsyncWebHeader* header = request->getHeader("Cookie");
  if (header == nullptr) return false;

  const String cookie = header->value();
  const int position = cookie.indexOf("TT_SESSION=");
  if (position < 0) return false;

  int end = cookie.indexOf(';', position);
  if (end < 0) end = cookie.length();
  const String token = cookie.substring(position + 11, end);
  return token == s_token;
}

bool requireAuth(AsyncWebServerRequest* request) {
  if (!isAuthenticated(request)) {
    sendError(request, 401, "Sesión requerida");
    return false;
  }
  if (storage.mustChangePassword()) {
    sendError(request, 403, "must_change_password");
    return false;
  }
  return true;
}

String statusJson() {
  JsonDocument document;
  document["system"] = "Activo";
  document["firmware"] = cfg::FIRMWARE_VERSION;
  document["uptime"] = millis() / 1000;
  document["bootId"] = String(g_state.bootId, HEX);

  JsonObject time = document["time"].to<JsonObject>();
  const uint32_t epoch = timesync::nowEpoch();
  time["epoch"] = epoch;
  time["valid"] = timesync::valid();
  time["iso"] = epoch > 0 ? timesync::isoTime(epoch) : "";
  time["bootEpoch"] = timesync::bootEpoch();

  JsonObject wifi = document["wifi"].to<JsonObject>();
  wifi["connected"] = WiFi.status() == WL_CONNECTED;
  wifi["ssid"] = WiFi.SSID();
  wifi["rssi"] = WiFi.RSSI();
  wifi["ip"] = WiFi.localIP().toString();

  JsonObject camera = document["camera"].to<JsonObject>();
  camera["online"] = g_state.cameraOnline;
  camera["lastSeen"] = g_state.cameraLastSeen;

  JsonObject power = document["power"].to<JsonObject>();
  power["acPresent"] = g_state.acPresent;
  power["upsOnBattery"] = !g_state.acPresent;

  uint8_t taken = 0;
  uint8_t known = 0;
  for (uint8_t i = 0; i < TOOL_COUNT; i++) {
    if (g_state.tools[i].known) {
      known++;
      if (!g_state.tools[i].present) taken++;
    }
  }

  document["toolsTaken"] = taken;
  document["toolsKnown"] = known;
  document["toolCount"] = TOOL_COUNT;
  document["sessionUser"] = g_state.sessionUser;
  document["heap"] = ESP.getFreeHeap();
  document["minHeap"] = ESP.getMinFreeHeap();
  document["psram"] = psramFound() ? ESP.getFreePsram() : 0;
  document["eventsCount"] = storage.eventCount();

  String output;
  serializeJson(document, output);
  return output;
}

String toolsJson() {
  JsonDocument document;
  JsonArray tools = document["tools"].to<JsonArray>();

  for (uint8_t i = 0; i < TOOL_COUNT; i++) {
    const ToolConfig& config = storage.tool(i);
    const ToolRuntime& runtime = g_state.tools[i];
    char slot[3];
    slotForTool(i, slot);

    JsonObject item = tools.add<JsonObject>();
    item["id"] = i;
    item["slot"] = slot;
    item["name"] = config.name;
    item["expectedClass"] = config.expectedClass;
    item["active"] = config.active;
    if (runtime.known) {
      item["present"] = runtime.present;
    } else {
      item["present"] = nullptr;
    }
    item["owner"] = runtime.owner;
    item["takenAt"] = runtime.takenAtEpoch;
    item["cameraPresent"] = runtime.cameraPresent;
    item["cameraConfidence"] = runtime.cameraConfidence;
  }

  JsonArray takings = document["activeTakings"].to<JsonArray>();
  for (uint8_t i = 0; i < TOOL_COUNT; i++) {
    const ToolRuntime& runtime = g_state.tools[i];
    if (!runtime.known || runtime.present) continue;
    char slot[3];
    slotForTool(i, slot);
    JsonObject item = takings.add<JsonObject>();
    item["toolId"] = i;
    item["slot"] = slot;
    item["name"] = storage.tool(i).name;
    item["owner"] = runtime.owner;
    item["takenAt"] = runtime.takenAtEpoch;
  }

  String output;
  serializeJson(document, output);
  return output;
}

String snapshotJson() {
  JsonDocument document;
  document["type"] = "snapshot";

  JsonObject data = document["data"].to<JsonObject>();
  data["system"] = "Activo";
  data["firmware"] = cfg::FIRMWARE_VERSION;
  data["uptime"] = millis() / 1000;
  data["toolsTaken"] = 0;
  data["toolCount"] = TOOL_COUNT;
  data["cameraOnline"] = g_state.cameraOnline;
  data["timeValid"] = timesync::valid();
  data["epoch"] = timesync::nowEpoch();
  data["sessionUser"] = g_state.sessionUser;

  uint8_t taken = 0;
  JsonArray tools = data["tools"].to<JsonArray>();
  for (uint8_t i = 0; i < TOOL_COUNT; i++) {
    const ToolRuntime& runtime = g_state.tools[i];
    if (runtime.known && !runtime.present) taken++;

    char slot[3];
    slotForTool(i, slot);
    JsonObject item = tools.add<JsonObject>();
    item["id"] = i;
    item["slot"] = slot;
    item["name"] = storage.tool(i).name;
    if (runtime.known) {
      item["present"] = runtime.present;
    } else {
      item["present"] = nullptr;
    }
    item["owner"] = runtime.owner;
    item["takenAt"] = runtime.takenAtEpoch;
    item["cameraPresent"] = runtime.cameraPresent;
    item["cameraConfidence"] = runtime.cameraConfidence;
  }
  data["toolsTaken"] = taken;

  String output;
  serializeJson(document, output);
  return output;
}

void broadcast(const String& payload) {
  if (s_ws.count() > 0) s_ws.textAll(payload);
}

void handleWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                   AwsEventType type, void* argument, uint8_t* data,
                   size_t length) {
  (void)server;

  if (type == WS_EVT_CONNECT) {
    client->text(snapshotJson());
    Serial.printf("[WS] Cliente %u conectado\n", client->id());
    return;
  }

  if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[WS] Cliente %u desconectado\n", client->id());
    return;
  }

  if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = static_cast<AwsFrameInfo*>(argument);
    if (info == nullptr) return;
    if (!info->final || info->index != 0 || info->len != length ||
        info->opcode != WS_TEXT) {
      return;
    }
    const String message(reinterpret_cast<const char*>(data), length);
    if (message.indexOf("ping") >= 0) {
      client->text("{\"type\":\"pong\"}");
    }
  }
}

constexpr size_t MAX_JSON_BODY = 4096;

void collectBody(AsyncWebServerRequest* request, uint8_t* data, size_t length,
                 size_t index, size_t total) {
  if (index == 0) {
    String* body = new String();
    // Nunca reservar el tamaño declarado por el cliente: un Content-Length
    // enorme podría agotar la memoria antes de rechazarlo.
    body->reserve(total < MAX_JSON_BODY + 1 ? total : MAX_JSON_BODY + 1);
    request->_tempObject = body;
  }

  String* body = static_cast<String*>(request->_tempObject);
  if (body == nullptr) return;

  // Guardar como máximo un byte de más para que el handler responda 413.
  if (body->length() <= MAX_JSON_BODY) {
    const size_t room = MAX_JSON_BODY + 1 - body->length();
    const size_t copy = length < room ? length : room;
    body->concat(reinterpret_cast<const char*>(data), copy);
  }
}

String takeBody(AsyncWebServerRequest* request) {
  String* body = static_cast<String*>(request->_tempObject);
  if (body == nullptr) return String();

  String result = *body;
  delete body;
  request->_tempObject = nullptr;
  return result;
}

using JsonHandler =
    std::function<void(AsyncWebServerRequest*, JsonVariant&)>;

void registerJsonHandler(const char* uri, WebRequestMethod method,
                         JsonHandler handler) {
  s_server.on(
      uri, method,
      [handler](AsyncWebServerRequest* request) {
        const String body = takeBody(request);
        if (body.length() > 4096) {
          sendError(request, 413, "Cuerpo demasiado grande");
          return;
        }

        JsonDocument document;
        const DeserializationError error = deserializeJson(document, body);
        if (error) {
          sendError(request, 400, "JSON inválido");
          return;
        }

        JsonVariant json = document.as<JsonVariant>();
        handler(request, json);
      },
      nullptr, collectBody);
}

}  // namespace

void begin() {
  // ----------------------------------------------------------
  //  Autenticación
  // ----------------------------------------------------------
  s_server.on("/api/auth/session", HTTP_GET,
              [](AsyncWebServerRequest* request) {
                JsonDocument document;
                const bool authenticated = isAuthenticated(request);
                document["authenticated"] = authenticated;
                document["mustChangePassword"] = storage.mustChangePassword();
                document["expiresIn"] =
                    authenticated
                        ? static_cast<int32_t>(s_tokenExpires - millis()) / 1000
                        : 0;
                String output;
                serializeJson(document, output);
                sendJson(request, 200, output);
              });

  registerJsonHandler(
      "/api/auth/login", HTTP_POST,
      [](AsyncWebServerRequest* request, JsonVariant& json) {
        if (static_cast<int32_t>(millis() - s_lockoutUntil) < 0) {
          sendError(request, 429, "Demasiados intentos; espera un minuto");
          return;
        }

        const String password = json["password"] | "";
        if (!storage.verifyPassword(password)) {
          s_failedAttempts++;
          if (s_failedAttempts >= cfg::ADMIN_MAX_ATTEMPTS) {
            s_failedAttempts = 0;
            s_lockoutUntil = millis() + cfg::ADMIN_LOCKOUT_MS;
          }
          sendError(request, 401, "Contraseña incorrecta");
          return;
        }

        s_failedAttempts = 0;
        generateToken();

        JsonDocument document;
        document["ok"] = true;
        document["expiresIn"] = cfg::ADMIN_SESSION_MS / 1000;
        document["mustChangePassword"] = storage.mustChangePassword();

        String output;
        serializeJson(document, output);

        AsyncWebServerResponse* response =
            request->beginResponse(200, "application/json", output);
        String cookie = "TT_SESSION=";
        cookie += s_token;
        cookie += "; Path=/; HttpOnly; SameSite=Lax; Max-Age=";
        cookie += String(cfg::ADMIN_SESSION_MS / 1000);
        response->addHeader("Set-Cookie", cookie);
        request->send(response);
      });

  s_server.on("/api/auth/logout", HTTP_POST,
              [](AsyncWebServerRequest* request) {
                s_token[0] = '\0';
                s_tokenExpires = 0;
                AsyncWebServerResponse* response = request->beginResponse(
                    200, "application/json", "{\"ok\":true}");
                response->addHeader(
                    "Set-Cookie",
                    "TT_SESSION=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0");
                request->send(response);
              });

  registerJsonHandler(
      "/api/auth/change-password", HTTP_POST,
      [](AsyncWebServerRequest* request, JsonVariant& json) {
        if (!isAuthenticated(request)) {
          sendError(request, 401, "Sesión requerida");
          return;
        }

        const String current = json["current"] | "";
        const String next = json["new"] | "";
        if (next.length() < 4) {
          sendError(request, 400, "La nueva contraseña es demasiado corta");
          return;
        }
        if (!storage.changePassword(current, next)) {
          sendError(request, 400, "La contraseña actual no es correcta");
          return;
        }
        sendJson(request, 200, "{\"ok\":true}");
      });

  // ----------------------------------------------------------
  //  API pública
  // ----------------------------------------------------------
  s_server.on("/api/status", HTTP_GET,
              [](AsyncWebServerRequest* request) {
                sendJson(request, 200, statusJson());
              });

  s_server.on("/api/tools", HTTP_GET,
              [](AsyncWebServerRequest* request) {
                sendJson(request, 200, toolsJson());
              });

  s_server.on("/api/events", HTTP_GET,
              [](AsyncWebServerRequest* request) {
                size_t limit = 50;
                size_t offset = 0;
                if (request->hasParam("limit")) {
                  const long value =
                      request->getParam("limit")->value().toInt();
                  if (value > 0) limit = static_cast<size_t>(value);
                }
                if (request->hasParam("offset")) {
                  const long value =
                      request->getParam("offset")->value().toInt();
                  if (value > 0) offset = static_cast<size_t>(value);
                }
                sendJson(request, 200, storage.eventsJson(limit, offset));
              });

  s_server.on("/api/events/export.csv", HTTP_GET,
              [](AsyncWebServerRequest* request) {
                if (!requireAuth(request)) return;

                AsyncResponseStream* response =
                    request->beginResponseStream("text/csv");
                response->addHeader(
                    "Content-Disposition",
                    "attachment; filename=\"tooltrack_eventos.csv\"");
                storage.eventsCsv(*response);
                request->send(response);
              });

  s_server.on("/api/photo", HTTP_GET,
              [](AsyncWebServerRequest* request) {
                if (!LittleFS.exists(cfg::LAST_PHOTO_FILE)) {
                  sendError(request, 404, "Sin foto disponible");
                  return;
                }
                request->send(LittleFS, cfg::LAST_PHOTO_FILE, "image/jpeg");
              });

  s_server.on("/api/photo/meta", HTTP_GET,
              [](AsyncWebServerRequest* request) {
                if (!LittleFS.exists(cfg::LAST_RESULT_FILE)) {
                  sendError(request, 404, "Sin análisis disponible");
                  return;
                }
                request->send(LittleFS, cfg::LAST_RESULT_FILE,
                              "application/json");
              });

  // ----------------------------------------------------------
  //  Administración: usuarios
  // ----------------------------------------------------------
  s_server.on("/api/admin/users", HTTP_GET,
              [](AsyncWebServerRequest* request) {
                if (!requireAuth(request)) return;
                JsonDocument document;
                JsonArray array = document["users"].to<JsonArray>();
                for (const User& user : storage.users()) {
                  JsonObject item = array.add<JsonObject>();
                  item["id"] = user.id;
                  item["uid"] = user.uid;
                  item["name"] = user.name;
                  item["active"] = user.active;
                  item["createdAt"] = user.createdAt;
                }
                String output;
                serializeJson(document, output);
                sendJson(request, 200, output);
              });

  registerJsonHandler(
      "/api/admin/users", HTTP_POST,
      [](AsyncWebServerRequest* request, JsonVariant& json) {
        if (!requireAuth(request)) return;
        const String uid = json["uid"] | "";
        const String name = json["name"] | "";
        if (!storage.addUser(uid, name)) {
          sendError(request, 400, "UID inválido o duplicado, o nombre vacío");
          return;
        }
        sendJson(request, 200, "{\"ok\":true}");
      });

  registerJsonHandler(
      "/api/admin/users", HTTP_PUT,
      [](AsyncWebServerRequest* request, JsonVariant& json) {
        if (!requireAuth(request)) return;
        const uint16_t id = json["id"] | 0;
        const String uid = json["uid"] | "";
        const String name = json["name"] | "";
        const bool active = json["active"] | true;
        if (id == 0 || !storage.updateUser(id, uid, name, active)) {
          sendError(request, 400, "No se pudo actualizar el usuario");
          return;
        }
        sendJson(request, 200, "{\"ok\":true}");
      });

  s_server.on("/api/admin/users", HTTP_DELETE,
              [](AsyncWebServerRequest* request) {
                if (!requireAuth(request)) return;
                if (!request->hasParam("id")) {
                  sendError(request, 400, "Falta el id");
                  return;
                }
                const uint16_t id =
                    request->getParam("id")->value().toInt();
                if (id == 0 || !storage.removeUser(id)) {
                  sendError(request, 404, "Usuario no encontrado");
                  return;
                }
                sendJson(request, 200, "{\"ok\":true}");
              });

  // ----------------------------------------------------------
  //  Administración: herramientas
  // ----------------------------------------------------------
  s_server.on("/api/admin/tools", HTTP_GET,
              [](AsyncWebServerRequest* request) {
                if (!requireAuth(request)) return;
                JsonDocument document;
                JsonArray array = document["tools"].to<JsonArray>();
                for (uint8_t i = 0; i < TOOL_COUNT; i++) {
                  const ToolConfig& tool = storage.tool(i);
                  JsonObject item = array.add<JsonObject>();
                  item["id"] = tool.id;
                  item["name"] = tool.name;
                  item["expectedClass"] = tool.expectedClass;
                  item["active"] = tool.active;
                }
                String output;
                serializeJson(document, output);
                sendJson(request, 200, output);
              });

  registerJsonHandler(
      "/api/admin/tools", HTTP_PUT,
      [](AsyncWebServerRequest* request, JsonVariant& json) {
        if (!requireAuth(request)) return;
        const int id = json["id"] | -1;
        if (id < 0 || id >= TOOL_COUNT) {
          sendError(request, 400, "Herramienta inválida");
          return;
        }

        String name = json["name"] | "";
        name.trim();
        if (name.length() == 0 || name.length() >= NAME_MAX_LEN) {
          sendError(request, 400, "Nombre inválido");
          return;
        }

        ToolConfig& tool = storage.tool(static_cast<uint8_t>(id));

        String expectedClass = json["expectedClass"] | "";
        expectedClass.trim();
        if (expectedClass.length() >= sizeof(tool.expectedClass)) {
          sendError(request, 400, "Clase esperada demasiado larga");
          return;
        }

        copyText(tool.name, sizeof(tool.name), name.c_str());
        copyText(tool.expectedClass, sizeof(tool.expectedClass),
                 expectedClass.c_str());
        tool.active = json["active"] | true;

        if (!storage.saveTools()) {
          sendError(request, 500, "No se pudo guardar");
          return;
        }
        sendJson(request, 200, "{\"ok\":true}");
      });

  // ----------------------------------------------------------
  //  Administración: ajustes y mantenimiento
  // ----------------------------------------------------------
  s_server.on("/api/admin/settings", HTTP_GET,
              [](AsyncWebServerRequest* request) {
                if (!requireAuth(request)) return;
                const Settings& settings = storage.settings();
                JsonDocument document;
                document["maxEvents"] = settings.maxEvents;
                document["cameraEnabled"] = settings.cameraEnabled;
                document["telegramEnabled"] = settings.telegramEnabled;
                document["telegramToken"] = settings.telegramToken;
                document["telegramChatId"] = settings.telegramChatId;
                document["sessionTimeoutMin"] = settings.sessionTimeoutMin;
                String output;
                serializeJson(document, output);
                sendJson(request, 200, output);
              });

  registerJsonHandler(
      "/api/admin/settings", HTTP_PUT,
      [](AsyncWebServerRequest* request, JsonVariant& json) {
        if (!requireAuth(request)) return;

        Settings& settings = storage.settings();
        const uint16_t previousMax = settings.maxEvents;

        const int maxEvents = json["maxEvents"] | settings.maxEvents;
        if (maxEvents < 50 || maxEvents > cfg::MAX_EVENTS) {
          sendError(request, 400, "maxEvents fuera de rango");
          return;
        }
        settings.maxEvents = static_cast<uint16_t>(maxEvents);
        settings.cameraEnabled = json["cameraEnabled"] | settings.cameraEnabled;
        settings.telegramEnabled =
            json["telegramEnabled"] | settings.telegramEnabled;
        settings.sessionTimeoutMin =
            json["sessionTimeoutMin"] | settings.sessionTimeoutMin;

        copyText(settings.telegramToken, sizeof(settings.telegramToken),
                 json["telegramToken"] | settings.telegramToken);
        copyText(settings.telegramChatId, sizeof(settings.telegramChatId),
                 json["telegramChatId"] | settings.telegramChatId);

        if (settings.sessionTimeoutMin > 1440) settings.sessionTimeoutMin = 1440;

        if (!storage.saveSettings()) {
          sendError(request, 500, "No se pudo guardar");
          return;
        }

        JsonDocument document;
        document["ok"] = true;
        document["rebootRequired"] = settings.maxEvents != previousMax;
        String output;
        serializeJson(document, output);
        sendJson(request, 200, output);
      });

  s_server.on("/api/admin/history/clear", HTTP_POST,
              [](AsyncWebServerRequest* request) {
                if (!requireAuth(request)) return;
                if (!storage.clearEvents()) {
                  sendError(request, 500, "No se pudo borrar el historial");
                  return;
                }
                sendJson(request, 200, "{\"ok\":true}");
              });

  s_server.on("/api/admin/camera/analyze", HTTP_POST,
              [](AsyncWebServerRequest* request) {
                if (!requireAuth(request)) return;
                s_forceAnalysis = true;
                sendJson(request, 202, "{\"ok\":true}");
              });

  s_server.on("/api/admin/restart", HTTP_POST,
              [](AsyncWebServerRequest* request) {
                if (!requireAuth(request)) return;
                s_restartRequested = true;
                sendJson(request, 200, "{\"ok\":true}");
              });

  s_server.on("/api/admin/wifi/reset", HTTP_POST,
              [](AsyncWebServerRequest* request) {
                if (!requireAuth(request)) return;
                s_wifiResetRequested = true;
                sendJson(request, 200, "{\"ok\":true}");
              });

  // Cualquier otra ruta /api/ devuelve JSON, no HTML.
  s_server.on("^/api/.*", HTTP_ANY,
              [](AsyncWebServerRequest* request) {
                sendError(request, 404, "No encontrado");
              });

  // ----------------------------------------------------------
  //  WebSocket y archivos estáticos
  // ----------------------------------------------------------
  s_ws.onEvent(handleWsEvent);
  s_server.addHandler(&s_ws);

  s_server.serveStatic("/", LittleFS, "/")
      .setDefaultFile("index.html")
      .setCacheControl("no-cache");

  s_server.onNotFound([](AsyncWebServerRequest* request) {
    if (request->url().startsWith("/api/")) {
      sendError(request, 404, "No encontrado");
      return;
    }
    request->send(LittleFS, "/index.html", "text/html");
  });

  s_server.begin();
  Serial.println("[WEB] Servidor HTTP y WebSocket iniciados en el puerto 80");
}

void loop() {
  const uint32_t now = millis();
  if (now - s_lastCleanup >= 1000) {
    s_lastCleanup = now;
    s_ws.cleanupClients();
  }
}

void broadcastToolChange(uint8_t toolId) {
  const ToolRuntime& runtime = g_state.tools[toolId];
  char slot[3];
  slotForTool(toolId, slot);

  JsonDocument document;
  document["type"] = "tool_changed";
  JsonObject data = document["data"].to<JsonObject>();
  data["toolId"] = toolId;
  data["slot"] = slot;
  data["name"] = storage.tool(toolId).name;
  data["present"] = runtime.present;
  data["owner"] = runtime.owner;
  data["takenAt"] = runtime.takenAtEpoch;
  data["cameraPresent"] = runtime.cameraPresent;
  data["cameraConfidence"] = runtime.cameraConfidence;
  data["source"] = "ir";

  String output;
  serializeJson(document, output);
  broadcast(output);
}

void broadcastSnapshot() {
  broadcast(snapshotJson());
}

void broadcastEvent(const Event& event) {
  const uint32_t epoch = storage.computeEventEpoch(event);
  const char* timeSource =
      event.timeValid ? "ntp" : (epoch > 0 ? "reconstructed" : "unknown");

  JsonDocument document;
  document["type"] = "event";
  JsonObject data = document["data"].to<JsonObject>();
  data["id"] = event.id;
  data["action"] = actionName(event.action);
  data["toolId"] = event.toolId;
  data["slot"] = event.slot;
  data["user"] = event.user;
  data["returnedBy"] = event.returnedBy;
  data["epoch"] = epoch;
  data["timeValid"] = epoch > 0;
  data["timeSource"] = timeSource;
  data["iso"] = epoch > 0 ? timesync::isoTime(epoch) : "";
  data["source"] = sourceName(event.source);
  data["confidence"] = event.confidence;

  String output;
  serializeJson(document, output);
  broadcast(output);
}

void broadcastAlert(const char* code, int8_t toolId) {
  JsonDocument document;
  document["type"] = "alert";
  JsonObject data = document["data"].to<JsonObject>();
  data["code"] = code;
  data["toolId"] = toolId;
  String output;
  serializeJson(document, output);
  broadcast(output);
}

void broadcastCameraStatus() {
  JsonDocument document;
  document["type"] = "camera_status";
  JsonObject data = document["data"].to<JsonObject>();
  data["online"] = g_state.cameraOnline;
  data["lastSeen"] = g_state.cameraLastSeen;
  String output;
  serializeJson(document, output);
  broadcast(output);
}

bool consumeRestartRequest() {
  if (!s_restartRequested) return false;
  s_restartRequested = false;
  return true;
}

bool consumeForceAnalysis() {
  if (!s_forceAnalysis) return false;
  s_forceAnalysis = false;
  return true;
}

bool consumeWifiResetRequest() {
  if (!s_wifiResetRequested) return false;
  s_wifiResetRequested = false;
  return true;
}

}  // namespace webserver
}  // namespace tt
