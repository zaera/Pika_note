#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
void pikaError(const char* area,const char* stage,const String& reason,int code=0,uint32_t duration=0);
void pikaErrorService();
void pikaErrorRead(JsonDocument& out,unsigned offset=0,unsigned limit=24);
