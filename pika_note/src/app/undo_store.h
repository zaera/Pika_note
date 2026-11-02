#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
bool pikaUndoActive();
bool pikaUndoBegin(const std::vector<JsonDocument>& requests,const String& label,String& error);
bool pikaUndoFinish(String& error);
void pikaUndoAbort();
bool pikaUndoApply(const String& operationKey,String& error);
bool pikaUndoRecover(String& error);
bool pikaUndoClear();
void pikaUndoStatus(JsonObject out);

bool pikaUndoAlreadyApplied(const String& operationKey);
