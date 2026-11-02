#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "tasks.h"
#include "json_memory.h"

bool managerMergeTasks(std::vector<BrainTaskRecord>& tasks);
int managerSetTaskDone(const String& id, bool done);
bool managerMutation(JsonVariantConst request, String& id, String& error);
String managerReadText(int note, size_t limit = 32000);
String managerLocalToUtc(const String& local, bool dayOnly, bool* valid = nullptr);
String managerUtcToLocalInput(const String& utc, bool dayOnly = false);
bool managerTextOnly(int note);
bool managerNoteExists(int note);
uint32_t managerRevision();
void managerTouch();

bool managerArchived(int note);

int managerImportGoogleEvent(const BrainTaskRecord& task, const String& baseline, String& error);
String managerGoogleBaseline(const String& id);

bool managerKnownRecord(const String& id);

// Reuse one durable store read within a single UI/request/planning operation.
void managerBeginReadSnapshot();
void managerEndReadSnapshot();
class ManagerReadSnapshot {
 public:ManagerReadSnapshot(){managerBeginReadSnapshot();}~ManagerReadSnapshot(){managerEndReadSnapshot();}
 ManagerReadSnapshot(const ManagerReadSnapshot&)=delete;
 ManagerReadSnapshot& operator=(const ManagerReadSnapshot&)=delete;
};

void managerInvalidateReadSnapshot();
bool managerBumpRevision(uint32_t floor);

bool managerPhoneReceipts(JsonDocument& out);
