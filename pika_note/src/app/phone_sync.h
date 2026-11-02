#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "tasks.h"
void pikaPhoneSyncInvalidate();
void pikaPhoneSyncForceScan();
void pikaPhoneFileTouched(const char* path);
bool pikaPhoneSyncTick();
uint32_t pikaPhoneSyncGeneration();
void pikaPhoneSyncStatus(JsonDocument& out);
void pikaPhoneSyncChanges(JsonDocument& out, uint32_t since, const String& epoch, bool full);
void pikaPhoneSyncRecord(JsonDocument& ack,const String& kind,const String& id);
void pikaPhoneSyncLease(bool active);
void pikaPhoneSyncProgress();
bool pikaPhoneSyncActive();
// Implemented beside the existing manager renderer; runs only in the SD owner loop.
bool pikaPhoneBuildSnapshot(JsonDocument& out);

bool pikaPhonePublishLiveSnapshot();

bool pikaPhoneFileWillChange(const char* path);
bool pikaPhoneCatalogSnapshot(JsonDocument& out);
bool pikaPhoneBuildMemoryRecord(JsonObject out,int note);
void pikaPhoneBuildTaskRecord(JsonObject out,const BrainTaskRecord& task);
void pikaPhoneBuildMeta(JsonDocument& out);

int pikaPhoneCachedMemoryCount();

void pikaPhoneCatalogPrintStatus();
void pikaPhoneCatalogBenchmark();
