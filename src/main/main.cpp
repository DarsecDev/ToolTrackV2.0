#include <Arduino.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "actuators.h"
#include "camera_link.h"
#include "config.h"
#include "ir_sensors.h"
#include "rfid.h"
#include "state.h"
#include "storage.h"
#include "time_sync.h"
#include "types.h"
#include "webserver.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

namespace {

constexpr uint32_t HEARTBEAT_MS = 10000;
constexpr uint32_t CAMERA_HEALTH_MS = 60000;

uint32_t s_lastHeartbeat = 0;
uint32_t s_sessionStart = 0;
bool s_analysisPending = false;
bool s_cameraBusy = false;
uint32_t s_analysisDue = 0;
bool s_cameraOfflineLogged = false;

TaskHandle_t s_cameraTask = nullptr;
portMUX_TYPE s_cameraResultMux = portMUX_INITIALIZER_UNLOCKED;
tt::camera_link::AnalysisResult s_cameraResult{};
bool s_cameraResultOk = false;
bool s_cameraResultReady = false;
volatile bool s_cameraStatusDirty = false;
bool s_acRaw = true;
bool s_acStable = true;
uint32_t s_acRawChanged = 0;

void copyText(char* destination, size_t size, const char* source) {
  if (size == 0) return;
  std::strncpy(destination, source ? source : "", size - 1);
  destination[size - 1] = '\0';
}

void printBanner() {
  Serial.println();
  Serial.println("============================================================");
  Serial.printf("  %s %s (%s)\n", cfg::PROJECT_NAME, cfg::FIRMWARE_VERSION,
                cfg::HARDWARE_REVISION);
  Serial.println("  Control de inventario de herramientas");
  Serial.println("============================================================");
  Serial.printf("  Rol        : placa maestra\n");
  Serial.printf("  Placa      : %s rev %u\n", ESP.getChipModel(),
                ESP.getChipRevision());
  Serial.printf("  CPU        : %u MHz, %u nucleos\n", ESP.getCpuFreqMHz(),
                ESP.getChipCores());
  Serial.printf("  Flash      : %u MB\n",
                static_cast<unsigned>(ESP.getFlashChipSize() / (1024 * 1024)));
  if (psramFound()) {
    Serial.printf("  PSRAM      : %u MB\n",
                  static_cast<unsigned>(ESP.getPsramSize() / (1024 * 1024)));
  } else {
    Serial.println("  PSRAM      : no detectada");
  }
  Serial.printf("  Herramientas: %u (%ux%u)\n", cfg::TOOL_COUNT,
                cfg::GRID_ROWS, cfg::GRID_COLS);
  Serial.println("------------------------------------------------------------");
}

bool mountStorage() {
  if (!LittleFS.begin(true)) {
    Serial.println("[FS] Error al montar LittleFS");
    return false;
  }
  Serial.printf("[FS] LittleFS montado: %u KB usados de %u KB\n",
                static_cast<unsigned>(LittleFS.usedBytes() / 1024),
                static_cast<unsigned>(LittleFS.totalBytes() / 1024));
  return true;
}

bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(cfg::MAIN_HOSTNAME);

  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.setConnectTimeout(20);
  wifiManager.setHostname(cfg::MAIN_HOSTNAME);

  Serial.println("[WIFI] Conectando (portal 'ToolTrack-Setup' si falla)...");
  if (!wifiManager.autoConnect("ToolTrack-Setup")) {
    Serial.println("[WIFI] Sin conexión; el armario sigue operando en local");
    return false;
  }

  Serial.printf("[WIFI] Conectado a '%s' con IP %s (RSSI %d dBm)\n",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                WiFi.RSSI());

  if (MDNS.begin(cfg::MAIN_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[MDNS] http://%s.local\n", cfg::MAIN_HOSTNAME);
  } else {
    Serial.println("[MDNS] No se pudo iniciar mDNS");
  }
  return true;
}

void logEvent(tt::EventAction action, int8_t toolId, const char* user,
              const char* returnedBy, tt::EventSource source,
              uint8_t confidence) {
  tt::Event event{};
  event.action = action;
  event.toolId = toolId;
  tt::slotForTool(toolId, event.slot);
  event.source = source;
  event.confidence = confidence;
  event.bootId = tt::g_state.bootId;
  event.uptimeMs = millis();
  event.epoch = tt::timesync::nowEpoch();
  event.timeValid = event.epoch > 0;
  copyText(event.user, sizeof(event.user), user);
  copyText(event.returnedBy, sizeof(event.returnedBy), returnedBy);

  tt::storage.appendEvent(event);
  tt::webserver::broadcastEvent(event);

  Serial.printf("[EVT] %s %s user='%s' returnedBy='%s' epoch=%u\n",
                tt::actionName(event.action), event.slot, event.user,
                event.returnedBy, static_cast<unsigned>(event.epoch));
}

void cameraTask(void*) {
  uint32_t lastHealth = millis() - CAMERA_HEALTH_MS;

  for (;;) {
    const bool request =
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) > 0;

    if (request) {
      tt::camera_link::AnalysisResult result;
      const bool ok = tt::camera_link::analyze(result);
      if (ok) tt::camera_link::fetchPhoto();

      portENTER_CRITICAL(&s_cameraResultMux);
      s_cameraResult = result;
      s_cameraResultOk = ok;
      s_cameraResultReady = true;
      portEXIT_CRITICAL(&s_cameraResultMux);

      // Evita lanzar un health-check inmediatamente después del análisis.
      lastHealth = millis();
    }

    const uint32_t now = millis();
    if (tt::storage.settings().cameraEnabled &&
        (now - lastHealth) >= CAMERA_HEALTH_MS) {
      lastHealth = now;
      tt::camera_link::health();
      s_cameraStatusDirty = true;
    }
  }
}

void startCameraTask() {
  if (!cfg::FEATURE_CAMERA) return;

  if (xTaskCreatePinnedToCore(cameraTask, "camera", 12288, nullptr, 1,
                              &s_cameraTask, 0) != pdPASS) {
    s_cameraTask = nullptr;
    Serial.println("[CAM] No se pudo crear la tarea de visión");
    return;
  }
  Serial.println("[CAM] Tarea de visión iniciada en segundo plano");
}

void scheduleAnalysis(uint32_t delayMs = cfg::CAMERA_SETTLE_MS) {
  if (!cfg::FEATURE_CAMERA || s_cameraTask == nullptr ||
      !tt::storage.settings().cameraEnabled) {
    return;
  }
  s_analysisPending = true;
  s_analysisDue = millis() + delayMs;
}

void handleRfid(const String& uid) {
  const int index = tt::storage.findUserByUid(uid);
  if (index >= 0 && tt::storage.users()[index].active) {
    const tt::User& user = tt::storage.users()[index];
    tt::stateSetSession(user.name, user.uid);
    s_sessionStart = millis();

    tt::actuators::unlock(cfg::LOCK_PULSE_MS);
    tt::actuators::ledStripOn(cfg::LED_STRIP_ON_MS);

    Serial.printf("[RFID] Acceso autorizado: %s\n", user.name);
    logEvent(tt::ACTION_ACCESS_GRANTED, -1, user.name, "", tt::SOURCE_RFID, 0);

    tt::webserver::broadcastSnapshot();
    scheduleAnalysis();
    return;
  }

  const String unknown = "UID " + uid;
  tt::actuators::denyAlert();
  Serial.printf("[RFID] Acceso denegado: %s\n", uid.c_str());
  logEvent(tt::ACTION_ACCESS_DENIED, -1, unknown.c_str(), "", tt::SOURCE_RFID,
           0);
  tt::webserver::broadcastAlert("access_denied", -1);
}

void handleIrChange(const tt::ir::Change& change) {
  const uint8_t toolId = change.toolId;
  tt::ToolRuntime& runtime = tt::g_state.tools[toolId];

  char previousOwner[tt::NAME_MAX_LEN];
  copyText(previousOwner, sizeof(previousOwner), runtime.owner);

  if (!change.present) {
    const char* owner =
        tt::stateHasSession() ? tt::g_state.sessionUser : "Sin sesión";
    tt::stateSetToolPresent(toolId, false, owner, tt::timesync::nowEpoch());
    logEvent(tt::ACTION_TAKEN, toolId, owner, "", tt::SOURCE_IR, 0);

    if (!tt::stateHasSession()) {
      tt::actuators::denyAlert();
      tt::webserver::broadcastAlert("taken_without_session", toolId);
    }
  } else {
    const char* returnedBy =
        tt::stateHasSession() ? tt::g_state.sessionUser : "Sin sesión";
    tt::stateSetToolPresent(toolId, true, nullptr, 0);
    logEvent(tt::ACTION_RETURNED, toolId,
             previousOwner[0] != '\0' ? previousOwner : "Sin sesión",
             returnedBy, tt::SOURCE_IR, 0);
  }

  if (tt::stateHasSession()) s_sessionStart = millis();

  tt::webserver::broadcastToolChange(toolId);
  scheduleAnalysis();
}

void processCameraResult(bool ok,
                         const tt::camera_link::AnalysisResult& result) {
  if (!tt::storage.settings().cameraEnabled) return;

  if (!ok) {
    Serial.println("[CAM] Análisis no disponible; decide el IR");
    if (!s_cameraOfflineLogged) {
      logEvent(tt::ACTION_CAMERA_OFFLINE, -1, "Sistema", "", tt::SOURCE_SYSTEM,
               0);
      s_cameraOfflineLogged = true;
    }
    tt::webserver::broadcastCameraStatus();
    return;
  }

  s_cameraOfflineLogged = false;

  bool mismatch = false;
  for (uint8_t i = 0; i < tt::TOOL_COUNT; i++) {
    const tt::camera_link::AnalysisSlot& slot = result.slots[i];

    // El estado global solo se modifica desde el loop principal. La tarea de
    // cámara se limita a devolver el resultado por la cola protegida.
    if (slot.reported) {
      tt::stateSetCameraResult(i, slot.present, slot.confidence);
    }

    if (!tt::g_state.tools[i].known) continue;
    if (slot.confidence < cfg::CAMERA_MIN_CONFIDENCE) continue;

    const bool irPresent = tt::g_state.tools[i].present;
    if (slot.present != irPresent) {
      logEvent(tt::ACTION_MISMATCH, static_cast<int8_t>(i), "Sistema", "",
               tt::SOURCE_CAMERA, slot.confidence);
      tt::webserver::broadcastAlert("ir_camera_mismatch",
                                    static_cast<int8_t>(i));
      mismatch = true;
      continue;
    }

    if (slot.present && slot.predictedTool >= 0 &&
        slot.predictedTool != i) {
      logEvent(tt::ACTION_MISMATCH, static_cast<int8_t>(i), "Sistema", "",
               tt::SOURCE_CAMERA, slot.confidence);
      tt::webserver::broadcastAlert("tool_wrong_slot",
                                    static_cast<int8_t>(i));
      mismatch = true;
    }
  }

  if (mismatch) tt::actuators::denyAlert();

  // La lámpara ya se encendió al lanzar el análisis; se extiende el tiempo
  // para que el usuario pueda ver el armario mientras revisa la web.
  tt::actuators::ledStripOn(cfg::LED_STRIP_ON_MS);

  // Refrescar en la web la confianza y la lectura de cámara de cada ranura.
  tt::webserver::broadcastSnapshot();
  tt::webserver::broadcastCameraStatus();
}

void initPowerSense() {
  if (!cfg::FEATURE_AC_SENSE) return;

  pinMode(cfg::PIN_AC_SENSE, INPUT_PULLUP);
  const bool high = digitalRead(cfg::PIN_AC_SENSE) == HIGH;
  s_acRaw = cfg::AC_SENSE_ACTIVE_HIGH ? high : !high;
  s_acStable = s_acRaw;
  s_acRawChanged = millis();
  tt::g_state.acPresent = s_acStable;
  Serial.printf("[POWER] Red %s\n", s_acStable ? "presente" : "ausente");
}

void pollPowerSense() {
  if (!cfg::FEATURE_AC_SENSE) return;

  const bool high = digitalRead(cfg::PIN_AC_SENSE) == HIGH;
  const bool present = cfg::AC_SENSE_ACTIVE_HIGH ? high : !high;
  const uint32_t now = millis();

  if (present != s_acRaw) {
    s_acRaw = present;
    s_acRawChanged = now;
  }

  if (s_acRaw != s_acStable && (now - s_acRawChanged) >= 500) {
    s_acStable = s_acRaw;
    tt::g_state.acPresent = s_acStable;

    if (s_acStable) {
      logEvent(tt::ACTION_POWER_RESTORED, -1, "Sistema", "", tt::SOURCE_SYSTEM,
               0);
      tt::webserver::broadcastSnapshot();
      Serial.println("[POWER] Red restaurada");
    } else {
      logEvent(tt::ACTION_POWER_LOST, -1, "Sistema", "", tt::SOURCE_SYSTEM, 0);
      tt::webserver::broadcastAlert("power_lost", -1);
      Serial.println("[POWER] Corte de red; operando con UPS");
    }
  }
}

void printHeartbeat() {
  Serial.printf(
      "[HB] uptime=%lus heap=%u min_heap=%u psram=%u wifi=%d rssi=%d "
      "session='%s' camera=%d events=%u ac=%d\n",
      static_cast<unsigned long>(millis() / 1000),
      static_cast<unsigned>(ESP.getFreeHeap()),
      static_cast<unsigned>(ESP.getMinFreeHeap()),
      static_cast<unsigned>(ESP.getFreePsram()),
      static_cast<int>(WiFi.status()), WiFi.RSSI(), tt::g_state.sessionUser,
      tt::g_state.cameraOnline, static_cast<unsigned>(tt::storage.eventCount()),
      tt::g_state.acPresent);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  printBanner();

  if (!mountStorage()) {
    Serial.println("[FS] Continuando sin LittleFS; el histórico no funcionará");
  }

  tt::stateInit();
  tt::g_state.bootId = esp_random();
  Serial.printf("[BOOT] bootId=%s\n", String(tt::g_state.bootId, HEX).c_str());

  if (!tt::storage.begin(SECRET_DEFAULT_ADMIN_PASSWORD)) {
    Serial.println("[STORAGE] Error fatal; reinicio en 5 s");
    delay(5000);
    ESP.restart();
  }

  tt::actuators::begin();
  tt::ir::begin();
  initPowerSense();

  for (uint8_t i = 0; i < tt::TOOL_COUNT; i++) {
    tt::stateSetToolPresent(i, tt::ir::readPresent(i), "", 0);
  }

  tt::rfid::begin();

  connectWiFi();
  tt::timesync::begin();
  tt::webserver::begin();
  startCameraTask();

  printHeartbeat();
}

void loop() {
  const uint32_t now = millis();

  tt::timesync::update();
  tt::actuators::update();
  tt::webserver::loop();
  pollPowerSense();

  tt::ir::Change change;
  if (tt::ir::poll(change)) {
    handleIrChange(change);
  }

  const String uid = tt::rfid::poll();
  if (uid.length() > 0) {
    handleRfid(uid);
  }

  // Cierre de sesión por inactividad (opcional).
  const uint16_t timeout = tt::storage.settings().sessionTimeoutMin;
  if (timeout > 0 && tt::stateHasSession()) {
    const uint32_t limit = static_cast<uint32_t>(timeout) * 60UL * 1000UL;
    if (now - s_sessionStart >= limit) {
      Serial.printf("[SESSION] Cerrando sesión de '%s'\n",
                    tt::g_state.sessionUser);
      tt::stateClearSession();
      tt::webserver::broadcastSnapshot();
    }
  }

  if (tt::webserver::consumeForceAnalysis()) {
    scheduleAnalysis(50);
  }

  if (s_cameraStatusDirty) {
    s_cameraStatusDirty = false;
    // Si el health vuelve a responder, permitir que un fallo posterior
    // vuelva a registrarse como cámara offline.
    if (tt::g_state.cameraOnline) s_cameraOfflineLogged = false;
    tt::webserver::broadcastCameraStatus();
  }

  if (s_analysisPending && !s_cameraBusy && s_cameraTask != nullptr &&
      static_cast<int32_t>(now - s_analysisDue) >= 0) {
    s_analysisPending = false;
    s_cameraBusy = true;
    // Encender la lámpara antes de que la tarea de visión capture el frame.
    tt::actuators::ledStripOn(cfg::LED_STRIP_ON_MS);
    if (xTaskNotifyGive(s_cameraTask) != pdPASS) {
      s_cameraBusy = false;
      Serial.println("[CAM] No se pudo despertar la tarea de visión");
    }
  }

  bool resultReady = false;
  bool resultOk = false;
  tt::camera_link::AnalysisResult cameraResult{};
  portENTER_CRITICAL(&s_cameraResultMux);
  if (s_cameraResultReady) {
    cameraResult = s_cameraResult;
    resultOk = s_cameraResultOk;
    s_cameraResultReady = false;
    resultReady = true;
  }
  portEXIT_CRITICAL(&s_cameraResultMux);

  if (resultReady) {
    s_cameraBusy = false;
    processCameraResult(resultOk, cameraResult);
  }

  if (now - s_lastHeartbeat >= HEARTBEAT_MS) {
    s_lastHeartbeat = now;
    printHeartbeat();
  }

  if (tt::webserver::consumeWifiResetRequest()) {
    Serial.println("[SYS] Borrando credenciales WiFi desde la web");
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    delay(300);
    ESP.restart();
  }

  if (tt::webserver::consumeRestartRequest()) {
    Serial.println("[SYS] Reinicio solicitado desde la web");
    delay(300);
    ESP.restart();
  }

  // Evita que el watchdog se queje en bucles muy activos.
  delay(2);
}
