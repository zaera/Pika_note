#pragma once
#include <Arduino.h>
inline String previousBrand(){const char name[]={112,97,108,97,0};return String(name);}
inline String currentBrand(String value){value.replace(previousBrand(),"pika");return value;}
bool pikaMigrateSettings();
bool pikaMigrateCard();

bool pikaBrandNeedsMigration();
