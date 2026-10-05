#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>

/**
 * Singleton holding the Todoist API token on the SD card.
 *
 * The token is XOR-obfuscated with the device's hardware MAC and base64-encoded
 * before it is written (same scheme as the KOReader and OPDS credentials): not
 * cryptographically secure, but it keeps the token out of plain sight on a card
 * that is routinely mounted on a PC, and it cannot be decoded on another chip.
 */
class TodoistStore : public PersistableStore<TodoistStore> {
 public:
  // The filter a fresh install syncs with, before anything is typed.
  //
  // "view all" is Todoist's documented universal query: every task, dated and
  // undated, so a first sync fills all five tabs including No date. Deliberately
  // not "#All" - "#" is the *project* prefix, so that would match only a project
  // literally named "All" and return nothing on most accounts.
  static constexpr const char* DEFAULT_FILTER = "view all";

 private:
  std::string token;
  // The Todoist filter query the sync asks for, in the app's own filter syntax.
  // What the Tasks screen holds is whatever this matches; the tabs only split it
  // afterwards. Defaults to DEFAULT_FILTER on a card that has never had one.
  std::string filter = DEFAULT_FILTER;

  TodoistStore() = default;
  ~TodoistStore() = default;

  friend class PersistableStore<TodoistStore>;

 public:
  // Personal API tokens are 40 hex characters; the cap leaves room for the
  // longer OAuth access tokens without letting a corrupt file allocate freely.
  static constexpr size_t MAX_TOKEN_LEN = 128;

  // Todoist caps a filter query at 1,024 characters, so anything longer would be
  // rejected by the API anyway.
  static constexpr size_t MAX_FILTER_LEN = 1024;

  static const char* getFilePath() { return "/.crosspoint/todoist.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  void setToken(const std::string& value);
  const std::string& getToken() const { return token; }
  bool hasToken() const { return !token.empty(); }

  void setFilter(const std::string& value);
  const std::string& getFilter() const { return filter; }
};

#define TODOIST_STORE TodoistStore::getInstance()
