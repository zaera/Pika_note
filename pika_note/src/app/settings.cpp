#include <ArduinoJson.h>
#include "settings.h"
#include <Preferences.h>
#include "notes.h"
#include "settings_pair.h"

static Preferences prefs;
static bool ready = false;
static int cachedWifiCount = -1;
static int cachedApiConfigured = -1;

static bool supportedLanguage(const String& language) {
  return language == "de" || language == "en" || language == "fr" ||
         language == "es" || language == "it" || language == "pl" ||
         language == "ru" || language == "cs" || language == "uk" ||
         language == "nl" || language == "pt";
}

static String wifiKey(int index, const char* suffix) {
  return "wifi" + String(index) + suffix;
}

static String storedString(const String& key) {
  if (!ready || !prefs.isKey(key.c_str())) return "";
  return prefs.getString(key.c_str(), "");
}

static bool storeWifiPair(int index, const String& ssid, const String& password) {
  return saveSettingsPair(ssid, password,
    [&](bool secret) { return storedString(wifiKey(index, secret ? "p" : "s")); },
    [&](bool secret, const String& text) { prefs.putString(wifiKey(index, secret ? "p" : "s").c_str(), text); });
}

static bool storeLegacyWifi(const PikaWifiProfile& profile) {
  return saveSettingsPair(profile.ssid, profile.password,
    [&](bool secret) { return storedString(secret ? "wifi_pass" : "wifi_ssid"); },
    [&](bool secret, const String& text) { prefs.putString(secret ? "wifi_pass" : "wifi_ssid", text); });
}

void pikaSettingsBegin() {
  if (ready) return;
  ready = prefs.begin("pika", false);
  if (!ready) return;
  // Migrate the original single-network setting without losing it.
  if (!storedString("wifi0s").length()) {
    const String legacySsid = storedString("wifi_ssid");
    if (legacySsid.length()) {
      storeWifiPair(0, legacySsid, storedString("wifi_pass"));
    }
  }
}

static String value(const char* key) {
  pikaSettingsBegin();
  return ready ? storedString(key) : String();
}

int pikaWifiProfileCount() {
  pikaSettingsBegin();
  if (!ready) return 0;
  if (cachedWifiCount >= 0) return cachedWifiCount;
  int count = 0;
  for (int i = 0; i < PIKA_MAX_WIFI_PROFILES; i++) {
    if (storedString(wifiKey(i, "s")).length()) count++;
  }
  cachedWifiCount = count;
  return cachedWifiCount;
}

PikaWifiProfile pikaWifiProfile(int index) {
  pikaSettingsBegin();
  if (!ready || index < 0 || index >= PIKA_MAX_WIFI_PROFILES) return {};
  return {storedString(wifiKey(index, "s")), storedString(wifiKey(index, "p"))};
}

bool pikaSaveWifiProfile(const String& ssid, const String& password) {
  pikaSettingsBegin();
  if (!ready || !ssid.length() || ssid.length() > 32 || password.length() > 63) return false;
  int target = -1;
  for (int i = 0; i < PIKA_MAX_WIFI_PROFILES; i++) {
    const String stored = storedString(wifiKey(i, "s"));
    if (stored == ssid) { target = i; break; }
    if (target < 0 && !stored.length()) target = i;
  }
  if (target < 0) return false;
  const bool ok = storeWifiPair(target, ssid, password);
  cachedWifiCount = -1;
  // Keep legacy keys synchronized for downgrade compatibility.
  if (ok && target == 0) {
    if (!storeLegacyWifi({ssid, password})) Serial.println("[Settings] legacy Wi-Fi mirror write failed");
  }
  return ok;
}

bool pikaRemoveWifiProfile(const String& ssid) {
  pikaSettingsBegin();
  if (!ready || !ssid.length()) return false;
  int found = -1;
  for (int i = 0; i < PIKA_MAX_WIFI_PROFILES; i++) {
    if (storedString(wifiKey(i, "s")) == ssid) { found = i; break; }
  }
  if (found < 0) return false;
  PikaWifiProfile before[PIKA_MAX_WIFI_PROFILES];
  for (int i = 0; i < PIKA_MAX_WIFI_PROFILES; ++i) before[i] = pikaWifiProfile(i);
  bool ok = true;
  for (int i = found; i < PIKA_MAX_WIFI_PROFILES && ok; ++i) {
    const PikaWifiProfile next = i + 1 < PIKA_MAX_WIFI_PROFILES ? before[i + 1] : PikaWifiProfile{};
    ok = storeWifiPair(i, next.ssid, next.password);
  }
  if (ok) ok = storeLegacyWifi(pikaWifiProfile(0));
  if (!ok) {
    for (int i = 0; i < PIKA_MAX_WIFI_PROFILES; ++i) storeWifiPair(i, before[i].ssid, before[i].password);
    storeLegacyWifi(before[0]);
    cachedWifiCount = -1;
    Serial.println("[Settings] Wi-Fi removal failed; rollback attempted");
    return false;
  }
  cachedWifiCount = -1;
  return true;
}

String pikaWifiSsid() { return pikaWifiProfile(0).ssid; }
String pikaWifiPassword() { return pikaWifiProfile(0).password; }
String pikaLastWifiSsid() { return value("last_wifi"); }
bool pikaSetLastWifiSsid(const String& ssid) {
  pikaSettingsBegin();
  if (!ready || !ssid.length() || ssid.length() > 32) return false;
  return storedString("last_wifi")==ssid || prefs.putString("last_wifi", ssid) > 0;
}
String pikaOpenAiKey() { return value("openai_key"); }
String pikaSpeechVoice() {
  String voice = value("tts_voice");
  return voice == "coral" ? "coral" : "cedar";
}
String pikaUiLanguage() {
  const String language = value("ui_lang");
  return supportedLanguage(language) ? language : "en";
}
bool pikaSetUiLanguage(const String& language) {
  pikaSettingsBegin();
  if (!ready || !supportedLanguage(language)) return false;
  return prefs.putString("ui_lang", language) > 0;
}
int pikaPlaybackVolume() {
  pikaSettingsBegin();
  if (!ready) return 80;
  const int stored = prefs.getInt("volume", 80);
  const int step = constrain((stored + 10) / 20, 1, 5);
  return step * 20;
}
bool pikaSetPlaybackVolume(int volume) {
  pikaSettingsBegin();
  if (!ready) return false;
  const int step = constrain((volume + 10) / 20, 1, 5);
  return prefs.putInt("volume", step * 20) == sizeof(int32_t);
}
bool pikaHasWifiLegacy() { return pikaWifiProfileCount() > 0 || pikaOpenWifiFallback(); }
bool pikaHasOpenAiKey() {
  if (cachedApiConfigured < 0) cachedApiConfigured = pikaOpenAiKey().length() > 10 ? 1 : 0;
  return cachedApiConfigured == 1;
}

bool pikaSaveSettings(const String& ssid, const String& password, const String& apiKey,
                      const String& speechVoice) {
  pikaSettingsBegin();
  if (!ready || ssid.length() > 32 || password.length() > 63 || (!ssid.length() && password.length()) ||
      apiKey.length() > 200 || (apiKey.length() && apiKey.length() < 11) ||
      (speechVoice != "cedar" && speechVoice != "coral")) return false;
  // Updating only the voice/key must not require adding a new network.
  bool ok = ssid.length() ? pikaSaveWifiProfile(ssid, password) : true;
  if (!ok) return false;
  if (apiKey.length() > 0) {
    const bool stored = prefs.putString("openai_key", apiKey) == apiKey.length();
    ok = stored && ok;
    if (stored) {
      cachedApiConfigured = apiKey.length() > 10 ? 1 : 0;
      resumeBlockedCloudNotes();
    }
  }
  ok = (prefs.putString("tts_voice", speechVoice) == speechVoice.length()) && ok;
  return ok;
}

bool pikaClearOpenAiKey() {
  pikaSettingsBegin();
  if (!ready) return false;
  if (prefs.isKey("openai_key") && !prefs.remove("openai_key")) return false;
  cachedApiConfigured = 0;
  return true;
}

bool pikaOpenWifiFallback() { pikaSettingsBegin(); return ready && prefs.getBool("open_fallback", false); }
bool pikaSetOpenWifiFallback(bool enabled) { pikaSettingsBegin(); return ready && prefs.putBool("open_fallback", enabled) == 1; }
String pikaGoogleCalendarId() { return value("gcal_id"); }
String pikaGoogleServiceJson() { return value("gcal_service"); }
bool pikaGoogleSyncEnabled() { pikaSettingsBegin(); return ready && prefs.getBool("gcal_enabled", false); }
bool pikaSaveGoogleSettings(const String& calendarId, const String& serviceJson, bool enabled) {
  pikaSettingsBegin();
  if (!ready || !calendarId.length() || calendarId.length() > 255 || calendarId == "primary") return false;
  const String key = serviceJson.length() ? serviceJson : pikaGoogleServiceJson();
  JsonDocument doc;
  if (deserializeJson(doc, key) || String(doc["type"] | "") != "service_account" ||
      !String(doc["client_email"] | "").endsWith(".gserviceaccount.com") ||
      !String(doc["private_key"] | "").startsWith("-----BEGIN PRIVATE KEY-----")) return false;
  const String oldCalendar = pikaGoogleCalendarId(), oldService = pikaGoogleServiceJson();
  bool ok = prefs.putString("gcal_service", key) == key.length() && prefs.putString("gcal_id", calendarId) == calendarId.length();
  if (!ok) { prefs.putString("gcal_service", oldService); prefs.putString("gcal_id", oldCalendar); return false; }
  return prefs.putBool("gcal_enabled", enabled) == 1;
}
bool pikaClearGoogleSettings() {
  pikaSettingsBegin(); if (!ready) return false;
  prefs.putBool("gcal_enabled", false);
  if (prefs.isKey("gcal_service") && !prefs.remove("gcal_service")) return false;
  if (prefs.isKey("gcal_id") && !prefs.remove("gcal_id")) return false;
  return true;
}
bool pikaUpdateWifiProfile(const String& oldSsid, const String& ssid, const String& password) {
  pikaSettingsBegin();
  if (!ready || !ssid.length() || ssid.length()>32 || password.length()>63) return false;
  if (!oldSsid.length()) return pikaSaveWifiProfile(ssid, password);
  int target=-1;
  for (int i=0;i<PIKA_MAX_WIFI_PROFILES;++i) {
    const PikaWifiProfile p=pikaWifiProfile(i);
    if (p.ssid == oldSsid) target=i;
    if (p.ssid == ssid && p.ssid != oldSsid) return false;
  }
  if (target<0) return false;
  const String keep = password.length() ? password : pikaWifiProfile(target).password;
  const bool ok=storeWifiPair(target,ssid,keep);
  cachedWifiCount=-1;
  if (ok && target==0) return storeLegacyWifi({ssid,keep});
  return ok;
}

bool pikaHasWifi(){return true;}
