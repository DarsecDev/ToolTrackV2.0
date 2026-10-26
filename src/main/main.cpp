#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

namespace {

constexpr uint32_t HEARTBEAT_MS = 10000;
uint32_t lastHeartbeat = 0;

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
  Serial.printf("  Herramientas: %u (%ux%u)\n", cfg::TOOL_COUNT, cfg::GRID_ROWS,
                cfg::GRID_COLS);
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

void configureSafeOutputs() {
  const uint8_t outputs[] = {cfg::PIN_LOCK, cfg::PIN_LED_GREEN, cfg::PIN_LED_RED,
                             cfg::PIN_BUZZER, cfg::PIN_LED_STRIP};
  for (uint8_t pin : outputs) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  Serial.println("[GPIO] Salidas inicializadas en estado seguro");
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(cfg::MAIN_HOSTNAME);

  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setConnectTimeout(20);
  wifiManager.setHostname(cfg::MAIN_HOSTNAME);

  Serial.println("[WIFI] Conectando (portal 'ToolTrack-Setup' si falla)...");
  if (!wifiManager.autoConnect("ToolTrack-Setup")) {
    Serial.println("[WIFI] Sin conexión. Reinicio en 5 s");
    delay(5000);
    ESP.restart();
  }

  Serial.printf("[WIFI] Conectado a '%s' con IP %s (RSSI %d dBm)\n",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                WiFi.RSSI());
}

void printHeartbeat() {
  Serial.printf(
      "[HB] uptime=%lus heap=%u min_heap=%u psram=%u wifi=%d rssi=%d\n",
      static_cast<unsigned long>(millis() / 1000),
      static_cast<unsigned>(ESP.getFreeHeap()),
      static_cast<unsigned>(ESP.getMinFreeHeap()),
      static_cast<unsigned>(ESP.getFreePsram()),
      static_cast<int>(WiFi.status()), WiFi.RSSI());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  printBanner();

  configureSafeOutputs();
  mountStorage();

  if (strlen(SECRET_DEFAULT_ADMIN_PASSWORD) == 0) {
    Serial.println("[ADMIN] ADVERTENCIA: contraseña inicial vacía");
  } else {
    Serial.println("[ADMIN] Contraseña inicial configurada");
  }

  connectWiFi();
  printHeartbeat();
}

void loop() {
  const uint32_t now = millis();
  if (now - lastHeartbeat >= HEARTBEAT_MS) {
    lastHeartbeat = now;
    printHeartbeat();
  }
}
