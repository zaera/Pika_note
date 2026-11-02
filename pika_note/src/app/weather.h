#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
struct PikaWeatherDay{String date;float minimum=0,maximum=0,precipitation=0;bool valid=false;int code=-1;float daytime=0,nighttime=0;bool dayNightValid=false;};
struct PikaWeatherHour{String time;float temperature=0;int code=-1;bool day=true,valid=false;};
struct PikaWeatherNow{float temperature=0,humidity=0,wind=0,direction=0;int code=-1;bool day=true,valid=false;String time;};
struct PikaWeatherLocation{bool enabled=false;String name;double latitude=0,longitude=0;PikaWeatherDay days[2];String updated;PikaWeatherNow current;PikaWeatherHour hours[4];};
void pikaWeatherBegin();
bool pikaWeatherConfigured();
bool pikaWeatherPending();
void pikaWeatherRequestRefresh();
bool pikaWeatherRefresh();
void pikaWeatherSettingsJson(JsonObject settings);
bool pikaWeatherSave(JsonArrayConst locations);
const PikaWeatherLocation& pikaWeatherLocation(int index);

void pikaWeatherPortalTick();

void pikaWeatherPortalPause();
