#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
struct PikaMailItem{String id,threadId,from,subject,snippet,body,brief;int64_t received=0;bool unread=false,bodyFetched=false;};
void pikaMailBegin();
bool pikaMailConfigured();
void pikaMailSettingsJson(JsonObject settings);
bool pikaMailSave(JsonObjectConst request);
bool pikaMailDisconnect();
void pikaMailRequestRefresh();
bool pikaMailPending();
bool pikaMailRefresh();
const std::vector<PikaMailItem>& pikaMailItems();
String pikaMailStatus();
String pikaMailSaved();
String pikaMailDate(int64_t milliseconds);
String pikaMailSpeech(int page);
String pikaMailItemSpeech(int index,bool includeSubject=true);

bool pikaMailAssigned();

bool pikaMailSaveBrief(const String& id,const String& speech);

int pikaMailUnreadCount();
bool pikaMailQuestion(const String& question);
String pikaMailAskContext();

bool pikaMailModificationRequested(const String& question);
