#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <vector>

#include "types.h"

namespace tt {

class Storage {
 public:
  bool begin(const char* defaultAdminPassword);

  // Usuarios RFID
  const std::vector<User>& users() const { return users_; }
  int findUserByUid(const String& uid) const;
  const User* userById(uint16_t id) const;
  bool addUser(const String& uid, const String& name, uint16_t* outId = nullptr);
  bool updateUser(uint16_t id, const String& uid, const String& name,
                  bool active);
  bool removeUser(uint16_t id);

  // Herramientas
  ToolConfig& tool(uint8_t id) { return tools_[id]; }
  const ToolConfig& tool(uint8_t id) const { return tools_[id]; }
  bool saveTools();

  // Ajustes
  Settings& settings() { return settings_; }
  const Settings& settings() const { return settings_; }
  bool saveSettings();

  // Eventos
  bool loadEvents();
  bool appendEvent(Event& event);
  bool clearEvents();
  size_t eventCount() const { return eventCount_; }
  const Event& eventFromNewest(size_t index) const;
  String eventsJson(size_t limit, size_t offset) const;
  void eventsCsv(Print& out) const;
  uint32_t computeEventEpoch(const Event& event) const;
  String computeEventIso(const Event& event) const;

  // Autenticación
  bool verifyPassword(const String& password);
  bool changePassword(const String& currentPassword, const String& newPassword);
  bool mustChangePassword() const { return mustChange_; }

  // Mantenimiento
  void resetUsers();

 private:
  bool loadUsers();
  bool saveUsers();
  bool loadTools();
  bool loadSettings();
  bool writeFileAtomic(const char* path, const String& content);
  bool rewriteEvents();
  void addToRing(const Event& event);
  bool setPassword(const String& password, bool mustChange);
  static bool pbkdf2(const String& password, const uint8_t* salt,
                     size_t saltLen, uint8_t* out);
  static bool constantTimeEquals(const uint8_t* a, const uint8_t* b,
                                 size_t length);
  static String normalizeUid(const String& uid);
  static bool validUid(const String& uid);

  std::vector<User> users_;
  ToolConfig tools_[TOOL_COUNT]{};
  Settings settings_{};
  Preferences prefs_;

  Event* events_ = nullptr;
  size_t capacity_ = 0;
  size_t eventCount_ = 0;
  size_t ringStart_ = 0;
  size_t fileEventCount_ = 0;
  uint32_t nextEventId_ = 1;
  uint32_t nextUserId_ = 1;

  uint8_t salt_[16]{};
  uint8_t hash_[32]{};
  bool mustChange_ = true;
  bool ready_ = false;
};

extern Storage storage;

}  // namespace tt
