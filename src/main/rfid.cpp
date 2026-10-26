#include "rfid.h"

#include <MFRC522.h>
#include <SPI.h>

#include "config.h"

namespace tt {
namespace rfid {

namespace {
MFRC522 s_reader(cfg::PIN_RFID_SS, cfg::PIN_RFID_RST);
uint32_t s_lastScan = 0;
uint32_t s_lastCardAt = 0;
String s_lastUid;
bool s_ready = false;

String uidToString(const MFRC522::Uid& uid) {
  String result;
  result.reserve(uid.size * 2);
  char buffer[3];
  for (uint8_t i = 0; i < uid.size; i++) {
    snprintf(buffer, sizeof(buffer), "%02X", uid.uidByte[i]);
    result += buffer;
  }
  return result;
}
}  // namespace

bool begin() {
  SPI.begin(cfg::PIN_RFID_SCK, cfg::PIN_RFID_MISO, cfg::PIN_RFID_MOSI,
            cfg::PIN_RFID_SS);
  s_reader.PCD_Init();
  delay(50);

  const uint8_t version = s_reader.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.printf("[RFID] RC522 version 0x%02X\n", version);

  if (version == 0x00 || version == 0xFF) {
    Serial.println("[RFID] Lector no detectado; revisar SPI y alimentación");
    s_ready = false;
    return false;
  }

  s_ready = true;
  return true;
}

String poll() {
  if (!s_ready) return String();

  const uint32_t now = millis();
  if (now - s_lastScan < cfg::RFID_SCAN_MS) return String();
  s_lastScan = now;

  if (!s_reader.PICC_IsNewCardPresent()) return String();
  if (!s_reader.PICC_ReadCardSerial()) return String();

  const String uid = uidToString(s_reader.uid);

  s_reader.PICC_HaltA();
  s_reader.PCD_StopCrypto1();

  // Evita repetir la misma tarjeta si se mantiene cerca del lector.
  if (uid == s_lastUid && (now - s_lastCardAt) < 3000) return String();

  s_lastUid = uid;
  s_lastCardAt = now;

  Serial.printf("[RFID] Tarjeta %s (%u bytes)\n", uid.c_str(),
                s_reader.uid.size);
  return uid;
}

}  // namespace rfid
}  // namespace tt
