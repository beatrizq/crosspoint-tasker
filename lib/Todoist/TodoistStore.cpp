#include "TodoistStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

#include <cstring>

void TodoistStore::toJson(JsonDocument& doc) const {
  doc["token_obf"] = obfuscation::obfuscateToBase64(token);
  // In the clear: a filter query is not a secret, and it is the one setting worth
  // being able to fix from a PC without retyping it on a touch keyboard.
  doc["filter"] = filter;
  doc["filter2"] = filter2;
}

bool TodoistStore::fromJson(JsonVariantConst doc) {
  token.clear();
  // An absent key is a card written before the filter existed, and takes the
  // default. An empty string is a filter the user cleared, which the API would
  // reject, so it takes the default too - there is no useful "no filter".
  filter = doc["filter"] | DEFAULT_FILTER;
  if (filter.empty()) filter = DEFAULT_FILTER;
  if (filter.size() > MAX_FILTER_LEN) filter.resize(MAX_FILTER_LEN);
  filter2 = doc["filter2"] | DEFAULT_FILTER;
  if (filter2.empty()) filter2 = DEFAULT_FILTER;
  if (filter2.size() > MAX_FILTER_LEN) filter2.resize(MAX_FILTER_LEN);

  const char* obfuscated = doc["token_obf"] | "";
  if (obfuscated[0] != '\0') {
    bool ok = false;
    bool tooLong = false;
    token = obfuscation::deobfuscateFromBase64(obfuscated, MAX_TOKEN_LEN, &ok, &tooLong);
    if (!ok) {
      // Wrong device (the key is the MAC) or a corrupt file: drop it rather
      // than sending garbage to the API on every sync.
      LOG_ERR("TDS", "Token failed to decode (%s), clearing", tooLong ? "too long" : "bad base64");
      token.clear();
    }
    return true;
  }

  // Plaintext fallback so the token can be dropped into the JSON by hand from a
  // PC; it is re-saved obfuscated on load.
  const char* plain = doc["token"] | "";
  if (plain[0] != '\0' && strlen(plain) <= MAX_TOKEN_LEN) {
    token = plain;
    LOG_DBG("TDS", "Plaintext token found, resaving obfuscated");
    requestResave();
  }
  return true;
}

void TodoistStore::setToken(const std::string& value) {
  token = value.size() > MAX_TOKEN_LEN ? value.substr(0, MAX_TOKEN_LEN) : value;
}

void TodoistStore::clearToken() { token.clear(); }

void TodoistStore::setFilter(const std::string& value) {
  // Clearing the filter falls back to the default rather than being stored: an
  // empty query is a 400 from the API, so it would only ever look like a broken
  // sync.
  if (value.empty()) {
    filter = DEFAULT_FILTER;
    return;
  }
  filter = value.size() > MAX_FILTER_LEN ? value.substr(0, MAX_FILTER_LEN) : value;
}

void TodoistStore::setFilter2(const std::string& value) {
  // Same rule as setFilter(): an empty query would only ever look like a broken
  // sync, so clearing it falls back to the default.
  if (value.empty()) {
    filter2 = DEFAULT_FILTER;
    return;
  }
  filter2 = value.size() > MAX_FILTER_LEN ? value.substr(0, MAX_FILTER_LEN) : value;
}

std::string TodoistStore::getCombinedFilter() const {
  if (filtersMatch()) return filter;
  std::string combined = "(" + filter + ") | (" + filter2 + ")";
  return combined.size() > MAX_FILTER_LEN ? filter : combined;
}
