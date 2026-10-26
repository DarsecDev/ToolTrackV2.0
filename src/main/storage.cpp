#include "storage.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_random.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

#include <cstring>
#include <time.h>

#include "config.h"
#include "state.h"
#include "time_sync.h"

namespace tt {

Storage storage;

namespace {

constexpr char USERS_FILE[] = "/users.json";
constexpr char TOOLS_FILE[] = "/tools.json";
constexpr char SETTINGS_FILE[] = "/settings.json";
constexpr char EVENTS_FILE[] = "/events.jsonl";
constexpr uint32_t PBKDF2_ITERATIONS = 10000;
constexpr size_t PBKDF2_KEY_LENGTH = 32;

void copyString(char* destination, size_t size, const char* source) {
  if (size == 0) return;
  std::strncpy(destination, source ? source : "", size - 1);
  destination[size - 1] = '\0';
}

void csvField(Print& out, const char* value) {
  out.print('"');
  if (value != nullptr) {
    for (const char* character = value; *character != '\0'; ++character) {
      if (*character == '"') out.print('"');
      out.print(*character);
    }
  }
  out.print('"');
}

}  // namespace

bool Storage::begin(const char* defaultAdminPassword) {
  if (!LittleFS.begin(true)) {
    Serial.println("[STORAGE] Error al montar LittleFS");
    return false;
  }

  prefs_.begin("tooltrack", false);
  nextEventId_ = prefs_.getUInt("evtSeq", 0) + 1;

  loadSettings();
  loadUsers();
  loadTools();

  capacity_ = settings_.maxEvents;
  if (capacity_ < 50) capacity_ = 50;
  if (capacity_ > cfg::MAX_EVENTS) capacity_ = cfg::MAX_EVENTS;

  events_ = static_cast<Event*>(ps_malloc(sizeof(Event) * capacity_));
  if (events_ == nullptr) {
    capacity_ = 200;
    events_ = static_cast<Event*>(malloc(sizeof(Event) * capacity_));
  }
  if (events_ == nullptr) {
    Serial.println("[STORAGE] Sin memoria para el historial");
    capacity_ = 0;
  }

  loadEvents();

  const size_t hashLength = prefs_.getBytesLength("adminHash");
  if (hashLength != sizeof(hash_)) {
    setPassword(defaultAdminPassword, true);
    Serial.println("[ADMIN] Contraseña inicial creada");
  } else {
    prefs_.getBytes("adminSalt", salt_, sizeof(salt_));
    prefs_.getBytes("adminHash", hash_, sizeof(hash_));
    mustChange_ = prefs_.getBool("mustChange", true);
  }

  ready_ = true;
  Serial.printf("[STORAGE] %u usuarios, %u eventos, límite %u\n",
                static_cast<unsigned>(users_.size()),
                static_cast<unsigned>(eventCount_),
                static_cast<unsigned>(settings_.maxEvents));
  return true;
}

// ============================================================
//  Usuarios
// ============================================================
bool Storage::loadUsers() {
  users_.clear();
  users_.reserve(MAX_USERS);

  if (!LittleFS.exists(USERS_FILE)) {
    resetUsers();
    return true;
  }

  File file = LittleFS.open(USERS_FILE, "r");
  if (!file) return false;

  JsonDocument document;
  const DeserializationError error = deserializeJson(document, file);
  file.close();

  if (error) {
    Serial.printf("[STORAGE] users.json inválido: %s\n", error.c_str());
    resetUsers();
    return false;
  }

  JsonArray array = document.as<JsonArray>();
  for (JsonObject item : array) {
    if (users_.size() >= MAX_USERS) break;
    User user{};
    user.id = item["id"] | 0;
    const String normalized = normalizeUid(item["uid"] | "");
    copyString(user.uid, sizeof(user.uid), normalized.c_str());
    copyString(user.name, sizeof(user.name), item["name"] | "");
    user.active = item["active"] | true;
    user.createdAt = item["createdAt"] | 0;
    if (user.id == 0 || !validUid(user.uid) || user.name[0] == '\0') continue;
    if (findUserByUid(user.uid) >= 0) continue;
    users_.push_back(user);
  }

  nextUserId_ = 1;
  for (const User& user : users_) {
    if (user.id >= nextUserId_) nextUserId_ = user.id + 1;
  }

  Serial.printf("[STORAGE] Cargados %u usuarios\n",
                static_cast<unsigned>(users_.size()));
  return true;
}

bool Storage::saveUsers() {
  JsonDocument document;
  JsonArray array = document.to<JsonArray>();

  for (const User& user : users_) {
    JsonObject item = array.add<JsonObject>();
    item["id"] = user.id;
    item["uid"] = user.uid;
    item["name"] = user.name;
    item["active"] = user.active;
    item["createdAt"] = user.createdAt;
  }

  String output;
  serializeJson(document, output);
  return writeFileAtomic(USERS_FILE, output);
}

void Storage::resetUsers() {
  users_.clear();

  const struct {
    const char* uid;
    const char* name;
  } defaults[] = {
      {"1A212828", "Benjamin V."},
      {"A97EC6B2", "Arturo F."},
      {"C29A713D", "Mario M."},
  };

  uint16_t id = 1;
  for (const auto& item : defaults) {
    User user{};
    user.id = id++;
    copyString(user.uid, sizeof(user.uid), item.uid);
    copyString(user.name, sizeof(user.name), item.name);
    user.active = true;
    user.createdAt = 0;
    users_.push_back(user);
  }

  nextUserId_ = id;
  saveUsers();
}

String Storage::normalizeUid(const String& uid) {
  String normalized;
  normalized.reserve(16);
  for (size_t i = 0; i < uid.length(); i++) {
    const char character = uid.charAt(i);
    if (character == ' ' || character == ':' || character == '-') continue;
    if (character >= '0' && character <= '9') {
      normalized += character;
    } else if (character >= 'a' && character <= 'f') {
      normalized += static_cast<char>(character - 'a' + 'A');
    } else if (character >= 'A' && character <= 'F') {
      normalized += character;
    }
  }
  return normalized;
}

bool Storage::validUid(const String& uid) {
  if (uid.length() < 8 || uid.length() > 14 || (uid.length() % 2) != 0) {
    return false;
  }
  for (size_t i = 0; i < uid.length(); i++) {
    const char character = uid.charAt(i);
    const bool hex = (character >= '0' && character <= '9') ||
                     (character >= 'A' && character <= 'F');
    if (!hex) return false;
  }
  return true;
}

int Storage::findUserByUid(const String& uid) const {
  const String normalized = normalizeUid(uid);
  for (size_t i = 0; i < users_.size(); i++) {
    if (normalized.equalsIgnoreCase(users_[i].uid)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

const User* Storage::userById(uint16_t id) const {
  for (const User& user : users_) {
    if (user.id == id) return &user;
  }
  return nullptr;
}

bool Storage::addUser(const String& uid, const String& name, uint16_t* outId) {
  if (users_.size() >= MAX_USERS) return false;

  const String normalized = normalizeUid(uid);
  if (!validUid(normalized)) return false;
  if (findUserByUid(normalized) >= 0) return false;

  String trimmedName = name;
  trimmedName.trim();
  if (trimmedName.length() == 0 || trimmedName.length() >= NAME_MAX_LEN) {
    return false;
  }

  User user{};
  user.id = nextUserId_++;
  copyString(user.uid, sizeof(user.uid), normalized.c_str());
  copyString(user.name, sizeof(user.name), trimmedName.c_str());
  user.active = true;
  user.createdAt = timesync::nowEpoch();
  users_.push_back(user);

  if (outId != nullptr) *outId = user.id;
  return saveUsers();
}

bool Storage::updateUser(uint16_t id, const String& uid, const String& name,
                         bool active) {
  for (User& user : users_) {
    if (user.id != id) continue;

    const String normalized = normalizeUid(uid);
    if (!validUid(normalized)) return false;

    const int existing = findUserByUid(normalized);
    if (existing >= 0 && users_[existing].id != id) return false;

    String trimmedName = name;
    trimmedName.trim();
    if (trimmedName.length() == 0 || trimmedName.length() >= NAME_MAX_LEN) {
      return false;
    }

    copyString(user.uid, sizeof(user.uid), normalized.c_str());
    copyString(user.name, sizeof(user.name), trimmedName.c_str());
    user.active = active;
    return saveUsers();
  }
  return false;
}

bool Storage::removeUser(uint16_t id) {
  for (auto it = users_.begin(); it != users_.end(); ++it) {
    if (it->id == id) {
      users_.erase(it);
      return saveUsers();
    }
  }
  return false;
}

// ============================================================
//  Herramientas
// ============================================================
bool Storage::loadTools() {
  for (uint8_t i = 0; i < TOOL_COUNT; i++) {
    tools_[i].id = i;
    snprintf(tools_[i].name, sizeof(tools_[i].name), "Herramienta %u", i + 1);
    snprintf(tools_[i].expectedClass, sizeof(tools_[i].expectedClass),
             "tool_%u", i + 1);
    tools_[i].active = true;
  }

  if (!LittleFS.exists(TOOLS_FILE)) {
    return saveTools();
  }

  File file = LittleFS.open(TOOLS_FILE, "r");
  if (!file) return false;

  JsonDocument document;
  const DeserializationError error = deserializeJson(document, file);
  file.close();
  if (error) {
    Serial.printf("[STORAGE] tools.json inválido: %s\n", error.c_str());
    return false;
  }

  JsonArray array = document.as<JsonArray>();
  for (JsonObject item : array) {
    const int id = item["id"] | -1;
    if (id < 0 || id >= TOOL_COUNT) continue;
    copyString(tools_[id].name, sizeof(tools_[id].name), item["name"] | "");
    copyString(tools_[id].expectedClass, sizeof(tools_[id].expectedClass),
               item["expectedClass"] | "");
    tools_[id].active = item["active"] | true;
  }
  return true;
}

bool Storage::saveTools() {
  JsonDocument document;
  JsonArray array = document.to<JsonArray>();

  for (uint8_t i = 0; i < TOOL_COUNT; i++) {
    JsonObject item = array.add<JsonObject>();
    item["id"] = tools_[i].id;
    item["name"] = tools_[i].name;
    item["expectedClass"] = tools_[i].expectedClass;
    item["active"] = tools_[i].active;
  }

  String output;
  serializeJson(document, output);
  return writeFileAtomic(TOOLS_FILE, output);
}

// ============================================================
//  Ajustes
// ============================================================
bool Storage::loadSettings() {
  settings_.maxEvents = cfg::MAX_EVENTS;
  settings_.cameraEnabled = cfg::FEATURE_CAMERA;
  settings_.telegramEnabled = false;
  settings_.telegramToken[0] = '\0';
  settings_.telegramChatId[0] = '\0';
  settings_.sessionTimeoutMin = 0;

  if (!LittleFS.exists(SETTINGS_FILE)) {
    return saveSettings();
  }

  File file = LittleFS.open(SETTINGS_FILE, "r");
  if (!file) return false;

  JsonDocument document;
  const DeserializationError error = deserializeJson(document, file);
  file.close();
  if (error) {
    Serial.printf("[STORAGE] settings.json inválido: %s\n", error.c_str());
    return false;
  }

  settings_.maxEvents = document["maxEvents"] | cfg::MAX_EVENTS;
  settings_.cameraEnabled = document["cameraEnabled"] | cfg::FEATURE_CAMERA;
  settings_.telegramEnabled = document["telegramEnabled"] | false;
  copyString(settings_.telegramToken, sizeof(settings_.telegramToken),
             document["telegramToken"] | "");
  copyString(settings_.telegramChatId, sizeof(settings_.telegramChatId),
             document["telegramChatId"] | "");
  settings_.sessionTimeoutMin = document["sessionTimeoutMin"] | 0;

  if (settings_.maxEvents < 50) settings_.maxEvents = 50;
  if (settings_.maxEvents > cfg::MAX_EVENTS) settings_.maxEvents = cfg::MAX_EVENTS;
  return true;
}

bool Storage::saveSettings() {
  JsonDocument document;
  document["maxEvents"] = settings_.maxEvents;
  document["cameraEnabled"] = settings_.cameraEnabled;
  document["telegramEnabled"] = settings_.telegramEnabled;
  document["telegramToken"] = settings_.telegramToken;
  document["telegramChatId"] = settings_.telegramChatId;
  document["sessionTimeoutMin"] = settings_.sessionTimeoutMin;

  String output;
  serializeJson(document, output);
  return writeFileAtomic(SETTINGS_FILE, output);
}

// ============================================================
//  Eventos
// ============================================================
bool Storage::loadEvents() {
  eventCount_ = 0;
  ringStart_ = 0;
  fileEventCount_ = 0;

  if (!LittleFS.exists(EVENTS_FILE)) {
    File file = LittleFS.open(EVENTS_FILE, "w");
    if (file) file.close();
    return true;
  }

  File file = LittleFS.open(EVENTS_FILE, "r");
  if (!file) return false;

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    JsonDocument document;
    const DeserializationError error = deserializeJson(document, line);
    if (error) continue;

    Event event{};
    event.id = document["id"] | 0;
    event.bootId = document["bootId"] | 0;
    event.uptimeMs = document["uptimeMs"] | 0;
    event.epoch = document["epoch"] | 0;
    event.timeValid = document["timeValid"] | false;
    event.toolId = document["toolId"] | -1;
    copyString(event.slot, sizeof(event.slot), document["slot"] | "--");
    event.action = actionFromName(document["action"] | "unknown");
    event.source = sourceFromName(document["source"] | "system");
    event.confidence = document["confidence"] | 0;
    copyString(event.user, sizeof(event.user), document["user"] | "");
    copyString(event.returnedBy, sizeof(event.returnedBy),
               document["returnedBy"] | "");

    addToRing(event);
    fileEventCount_++;
    if (event.id >= nextEventId_) nextEventId_ = event.id + 1;
  }
  file.close();

  Serial.printf("[STORAGE] Cargados %u eventos (%u en archivo)\n",
                static_cast<unsigned>(eventCount_),
                static_cast<unsigned>(fileEventCount_));
  return true;
}

void Storage::addToRing(const Event& event) {
  if (capacity_ == 0 || events_ == nullptr) return;

  if (eventCount_ < capacity_) {
    const size_t index = (ringStart_ + eventCount_) % capacity_;
    events_[index] = event;
    eventCount_++;
  } else {
    events_[ringStart_] = event;
    ringStart_ = (ringStart_ + 1) % capacity_;
  }
}

const Event& Storage::eventFromNewest(size_t index) const {
  const size_t position = (ringStart_ + eventCount_ - 1 - index) % capacity_;
  return events_[position];
}

bool Storage::appendEvent(Event& event) {
  if (!ready_) return false;

  if (event.id == 0) event.id = nextEventId_++;
  if (event.bootId == 0) event.bootId = g_state.bootId;
  if (event.uptimeMs == 0) event.uptimeMs = millis();
  event.timeValid = event.epoch > 0;

  prefs_.putUInt("evtSeq", nextEventId_);

  JsonDocument document;
  document["id"] = event.id;
  document["bootId"] = event.bootId;
  document["uptimeMs"] = event.uptimeMs;
  document["epoch"] = event.epoch;
  document["timeValid"] = event.timeValid;
  document["toolId"] = event.toolId;
  document["slot"] = event.slot;
  document["action"] = actionName(event.action);
  document["source"] = sourceName(event.source);
  document["confidence"] = event.confidence;
  document["user"] = event.user;
  document["returnedBy"] = event.returnedBy;

  String line;
  serializeJson(document, line);

  File file = LittleFS.open(EVENTS_FILE, FILE_APPEND);
  if (!file) {
    Serial.println("[STORAGE] No se pudo abrir events.jsonl");
    return false;
  }
  file.println(line);
  file.close();

  addToRing(event);
  fileEventCount_++;

  if (fileEventCount_ > settings_.maxEvents + 32) {
    rewriteEvents();
  }

  return true;
}

bool Storage::rewriteEvents() {
  const String temporary = String(EVENTS_FILE) + ".tmp";
  File file = LittleFS.open(temporary, "w");
  if (!file) return false;

  for (size_t i = eventCount_; i > 0; --i) {
    const Event& event = eventFromNewest(i - 1);
    JsonDocument document;
    document["id"] = event.id;
    document["bootId"] = event.bootId;
    document["uptimeMs"] = event.uptimeMs;
    document["epoch"] = event.epoch;
    document["timeValid"] = event.timeValid;
    document["toolId"] = event.toolId;
    document["slot"] = event.slot;
    document["action"] = actionName(event.action);
    document["source"] = sourceName(event.source);
    document["confidence"] = event.confidence;
    document["user"] = event.user;
    document["returnedBy"] = event.returnedBy;

    String line;
    serializeJson(document, line);
    file.println(line);
  }
  file.close();

  if (LittleFS.exists(EVENTS_FILE)) LittleFS.remove(EVENTS_FILE);
  if (!LittleFS.rename(temporary, EVENTS_FILE)) {
    Serial.println("[STORAGE] Error al rotar el historial");
    return false;
  }

  fileEventCount_ = eventCount_;
  Serial.printf("[STORAGE] Historial rotado a %u eventos\n",
                static_cast<unsigned>(eventCount_));
  return true;
}

bool Storage::clearEvents() {
  File file = LittleFS.open(EVENTS_FILE, "w");
  if (!file) return false;
  file.close();

  eventCount_ = 0;
  ringStart_ = 0;
  fileEventCount_ = 0;
  return true;
}

uint32_t Storage::computeEventEpoch(const Event& event) const {
  if (event.epoch > 0 && event.timeValid) return event.epoch;
  if (timesync::valid() && event.bootId == g_state.bootId) {
    return timesync::bootEpoch() + (event.uptimeMs / 1000);
  }
  return 0;
}

String Storage::computeEventIso(const Event& event) const {
  return timesync::isoTime(computeEventEpoch(event));
}

String Storage::eventsJson(size_t limit, size_t offset) const {
  if (limit == 0) limit = 50;
  if (limit > 100) limit = 100;

  JsonDocument document;
  document["total"] = eventCount_;
  document["limit"] = limit;
  document["offset"] = offset;

  JsonArray array = document["events"].to<JsonArray>();
  const size_t end = offset + limit;
  for (size_t i = offset; i < end && i < eventCount_; i++) {
    const Event& event = eventFromNewest(i);
    const uint32_t epoch = computeEventEpoch(event);
    const char* timeSource = event.timeValid ? "ntp" : "unknown";
    if (!event.timeValid && epoch > 0) timeSource = "reconstructed";

    JsonObject item = array.add<JsonObject>();
    item["id"] = event.id;
    item["bootId"] = String(event.bootId, HEX);
    item["uptimeMs"] = event.uptimeMs;
    item["epoch"] = epoch;
    item["timeValid"] = epoch > 0;
    item["timeSource"] = timeSource;
    item["iso"] = epoch > 0 ? timesync::isoTime(epoch) : "";
    item["toolId"] = event.toolId;
    item["slot"] = event.slot;
    item["action"] = actionName(event.action);
    item["source"] = sourceName(event.source);
    item["confidence"] = event.confidence;
    item["user"] = event.user;
    item["returnedBy"] = event.returnedBy;
  }

  String output;
  serializeJson(document, output);
  return output;
}

void Storage::eventsCsv(Print& out) const {
  out.print("id,fecha_iso,epoch,herramienta,slot,accion,usuario,devuelto_por,fuente,confianza\n");

  for (size_t i = eventCount_; i > 0; --i) {
    const Event& event = eventFromNewest(i - 1);
    const uint32_t epoch = computeEventEpoch(event);

    out.print(event.id);
    out.print(',');
    csvField(out, timesync::isoTime(epoch).c_str());
    out.print(',');
    out.print(epoch);
    out.print(',');
    out.print(static_cast<int>(event.toolId) + 1);
    out.print(',');
    csvField(out, event.slot);
    out.print(',');
    csvField(out, actionName(event.action));
    out.print(',');
    csvField(out, event.user);
    out.print(',');
    csvField(out, event.returnedBy);
    out.print(',');
    csvField(out, sourceName(event.source));
    out.print(',');
    out.print(event.confidence);
    out.print('\n');
  }
}

// ============================================================
//  Contraseña de administrador
// ============================================================
bool Storage::pbkdf2(const String& password, const uint8_t* salt,
                     size_t saltLen, uint8_t* out) {
  mbedtls_md_context_t context;
  mbedtls_md_init(&context);

  const mbedtls_md_info_t* info =
      mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == nullptr) {
    mbedtls_md_free(&context);
    return false;
  }

  if (mbedtls_md_setup(&context, info, 1) != 0) {
    mbedtls_md_free(&context);
    return false;
  }

  const int result = mbedtls_pkcs5_pbkdf2_hmac(
      &context, reinterpret_cast<const unsigned char*>(password.c_str()),
      password.length(), salt, saltLen, PBKDF2_ITERATIONS,
      PBKDF2_KEY_LENGTH, out);
  mbedtls_md_free(&context);
  return result == 0;
}

bool Storage::constantTimeEquals(const uint8_t* a, const uint8_t* b,
                                 size_t length) {
  uint8_t difference = 0;
  for (size_t i = 0; i < length; i++) {
    difference |= static_cast<uint8_t>(a[i] ^ b[i]);
  }
  return difference == 0;
}

bool Storage::setPassword(const String& password, bool mustChange) {
  esp_fill_random(salt_, sizeof(salt_));
  if (!pbkdf2(password, salt_, sizeof(salt_), hash_)) {
    Serial.println("[AUTH] Error al derivar la contraseña");
    return false;
  }
  prefs_.putBytes("adminSalt", salt_, sizeof(salt_));
  prefs_.putBytes("adminHash", hash_, sizeof(hash_));
  prefs_.putBool("mustChange", mustChange);
  mustChange_ = mustChange;
  return true;
}

bool Storage::verifyPassword(const String& password) {
  if (password.length() == 0) return false;
  uint8_t candidate[PBKDF2_KEY_LENGTH]{};
  if (!pbkdf2(password, salt_, sizeof(salt_), candidate)) return false;
  return constantTimeEquals(candidate, hash_, sizeof(hash_));
}

bool Storage::changePassword(const String& currentPassword,
                             const String& newPassword) {
  if (!verifyPassword(currentPassword)) return false;
  if (newPassword.length() < 4) return false;
  return setPassword(newPassword, false);
}

bool Storage::writeFileAtomic(const char* path, const String& content) {
  const String temporary = String(path) + ".tmp";
  File file = LittleFS.open(temporary, "w");
  if (!file) return false;

  const size_t written = file.print(content);
  file.close();

  if (written != content.length()) {
    LittleFS.remove(temporary);
    return false;
  }

  if (LittleFS.exists(path)) LittleFS.remove(path);
  if (!LittleFS.rename(temporary, path)) {
    Serial.printf("[STORAGE] Error al renombrar %s\n", path);
    return false;
  }
  return true;
}

}  // namespace tt
