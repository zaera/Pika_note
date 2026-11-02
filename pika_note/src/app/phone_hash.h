#pragma once
#include <Arduino.h>
void pikaPhoneHashTick();
void pikaPhoneHashCancel();
int pikaPhoneHashRequest(const String& name,String& digest);
