#pragma once
#include <Arduino.h>
#include "tasks.h"
void pikaExtrasBegin();
bool pikaAlertEnabled();
int pikaAlertLeadMinutes();
int pikaAlertDurationSeconds();
bool pikaSaveAlertSettings(bool enabled,int lead,int duration);
bool pikaFindDueAlert(BrainTaskRecord& task);
void pikaMarkAlert(const BrainTaskRecord& task);
uint64_t pikaNextAlertWakeUs();
bool pikaClockMode();
void pikaSetClockMode(bool enabled);

int pikaAlertMelody();
bool pikaSaveSoundSettings(int melody,int volume);
String pikaButtonAction(int slot);
bool pikaSaveButtonActions(const String* actions);
void pikaQueueSoundTest(int melody,int volume);
void pikaServiceSoundTest();
void pikaPlayAlertMelody();

bool pikaStrictCapture();
bool pikaSaveStrictCapture(bool enabled);

bool pikaVoiceRecordActions();
bool pikaSaveVoiceRecordActions(bool enabled);

bool pikaNotifyEnabled();
bool pikaSaveNotify(bool enabled);
void pikaPlayNotify(int kind); // 0 recording, 1 Ask answer, 2 error, 3 memory, 4 mail, 5 wake ready, 6 sleep

int pikaNotifyMelody(int kind);
bool pikaSaveNotifyMelodies(const int* values);
void pikaQueueMailNotify();

void pikaVibrationBegin();
bool pikaVibrationEnabled();
int pikaVibrationPattern(int kind);
bool pikaSaveVibration(bool enabled,const int* patterns);
void pikaVibrate(int kind);
void pikaVibrationWait();

void pikaVibrationArmWake(bool enabled);
void pikaVibrationWakeReady();
void pikaVibrationPrepareSleep();
void pikaVibrationPrintStatus();

bool pikaVibrationTest(int pattern);

int pikaSleepMinutes();
uint32_t pikaSleepIdleMs();
bool pikaSaveSleepMinutes(int minutes);

float pikaBatteryCalibration();
bool pikaSaveBatteryCalibration(float factor);
