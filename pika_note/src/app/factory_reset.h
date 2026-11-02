#pragma once
#include <Arduino.h>
bool pikaFactoryConfigSuppressed();
bool pikaFactoryPrepareBoot();
bool pikaFactoryRecoverData(String& error);
bool pikaFactoryRequest(const String& pin,String& error);

bool pikaFactoryPending();
