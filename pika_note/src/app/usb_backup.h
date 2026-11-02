#pragma once
bool pikaUsbBackupRequest();
bool pikaUsbBackupDue();
bool pikaUsbBackupPending();
// Runs exclusively after the main loop has stopped Companion and audio.
void pikaUsbBackupRun();

// Consumes a one-shot RTC marker written only by the read-only USB exit.
bool pikaUsbBackupResume();
