#pragma once
#include <Arduino.h>

void pikaSettingsBegin();
constexpr int PIKA_MAX_WIFI_PROFILES = 5;
struct PikaWifiProfile {
  String ssid;
  String password;
};
int pikaWifiProfileCount();
PikaWifiProfile pikaWifiProfile(int index);
bool pikaSaveWifiProfile(const String& ssid, const String& password);
bool pikaRemoveWifiProfile(const String& ssid);
String pikaWifiSsid();
String pikaWifiPassword();
String pikaLastWifiSsid();
bool pikaSetLastWifiSsid(const String& ssid);
String pikaOpenAiKey();
String pikaSpeechVoice();
String pikaUiLanguage();
bool pikaSetUiLanguage(const String& language);
int pikaPlaybackVolume();
bool pikaSetPlaybackVolume(int volume);
bool pikaHasWifi();
bool pikaHasOpenAiKey();
bool pikaSaveSettings(const String& ssid, const String& password, const String& apiKey,
                      const String& speechVoice);
bool pikaClearOpenAiKey();

bool pikaOpenWifiFallback();
bool pikaSetOpenWifiFallback(bool enabled);
String pikaGoogleCalendarId();
String pikaGoogleServiceJson();
bool pikaGoogleSyncEnabled();
bool pikaSaveGoogleSettings(const String& calendarId, const String& serviceJson, bool enabled);
bool pikaClearGoogleSettings();
bool pikaUpdateWifiProfile(const String& oldSsid, const String& ssid, const String& password);
