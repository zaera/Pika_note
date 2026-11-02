#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
void pikaOwnerBegin();bool pikaOwnerBound();bool pikaOwnerLegacyBondOnly();String pikaDeviceSerial();
bool pikaOwnerAuthenticate(const String& nonce,const String& proof,const String& claimKey,const String& peer,bool previouslyBonded);
String pikaOwnerExportSettings();bool pikaOwnerImportSettings(JsonVariantConst settings,String& error);
bool pikaOwnerForget(const String& serial,const String& receipt,String& error);String pikaOwnerForgetReceipt();
void showPairingOnly();
extern uint32_t pikaOwnerRestartAt;

bool pikaOwnerRecoverSettings(String& error);

#ifdef PIKA_TEST_SETTINGS_RECOVERY
bool pikaOwnerArmRecoveryProbe();
#endif
