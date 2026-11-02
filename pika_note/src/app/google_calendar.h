#pragma once
#include <Arduino.h>
#include <vector>
void pikaGoogleSyncTick(bool force = false);
void pikaGoogleScheduleSync();
void pikaGoogleResetSession();
String pikaGoogleStatus();
bool pikaGoogleVerified();

// Bounded sync pass for a wake/voice session while Wi-Fi is already connected.
void pikaGoogleSyncBatch(int preferredNote = -1);

bool pikaGoogleRequestImport(const String& fromDay, const String& throughDay);

bool pikaGoogleHasPendingChanges();
void pikaGoogleRequestVerify();

// Forget local sync journal entries without deleting remote events.
bool pikaGoogleForgetLocalEvents(const std::vector<String>& ids);

void pikaGooglePortalTick();

void pikaGooglePortalPause();
