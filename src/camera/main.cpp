#include <Arduino.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "camera_config.h"
#include "camera_vision.h"
#include "camera_webserver.h"

namespace {

void printBanner() {
  Serial.println();
  Serial.println("============================================================");
  Serial.printf("  %s %s\n", camcfg::PROJECT_NAME, camcfg::FIRMWARE_VERSION);
  Serial.println("  Nodo de visión (XIAO ESP32-S3 Sense)");
  Serial.println("============================================================");
  Serial.printf("  Flash : %u MB\n",
                static_cast<unsigned>(ESP.getFlashChipSize() / (1024 * 1024)));
  if (psramFound()) {
    Serial.printf("  PSRAM : %u MB\n",
                  static_cast<unsigned>(ESP.getPsramSize() / (1024 * 1024)));
  } else {
    Serial.println("  PSRAM : no detectada (la cámara funcionará en VGA)");
  }
  Serial.println("------------------------------------------------------------");
}

bool mountStorage() {
  if (!LittleFS.begin(true)) {
    Serial.println("[FS] Error al montar LittleFS");
    return false;
  }
  Serial.printf("[FS] LittleFS: %u KB usados de %u KB\n",
                static_cast<unsigned>(LittleFS.usedBytes() / 1024),
                static_cast<unsigned>(LittleFS.totalBytes() / 1024));
  return true;
}

void connectWiFi() {
  if (!camcfg::ENABLE_WIFI) {
    Serial.println("[WIFI] Deshabilitado");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(camcfg::HOSTNAME);

  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(camcfg::WIFI_PORTAL_TIMEOUT_MS / 1000);
  wifiManager.setConnectTimeout(20);
  wifiManager.setHostname(camcfg::HOSTNAME);

  Serial.println("[WIFI] Conectando (portal 'ToolTrack-Cam' si falla)...");
  if (!wifiManager.autoConnect("ToolTrack-Cam")) {
    Serial.println("[WIFI] Sin conexión; el servidor queda en modo local");
    return;
  }

  Serial.printf("[WIFI] Conectado a '%s' con IP %s (RSSI %d dBm)\n",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                WiFi.RSSI());

  if (MDNS.begin(camcfg::HOSTNAME)) {
    MDNS.addService("http", "tcp", camcfg::HTTP_PORT);
    Serial.printf("[MDNS] http://%s.local\n", camcfg::HOSTNAME);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  printBanner();

  mountStorage();

  if (!camvision::begin()) {
    Serial.println("[CAM] Nodo sin cámara; /health lo reportará como no disponible");
  }

  connectWiFi();
  camws::begin();
}

void loop() {
  delay(1000);
}
