#pragma once

#include <Arduino.h>

// The device UI intentionally exposes only Record, Ask, processing, answer,
// Companion, error, and sleep. Organization belongs to the Web Companion.
bool pikaHomeRefreshBusy();
void showIdle(int pendingCount = 0, int batteryPercent = -1);
void showTodayAgenda(bool events, int page = 0);
int todayAgendaPageCount(bool events);
void showRecording();
void showRecordingStopped(bool question);
void showRecordingProgress(uint32_t elapsedMs, int moments);
void showSaved(int noteNumber);
void showMemoryProcessing(int noteNumber, const char* phase);
void showBacklogProcessing(int completed, int total, const char* phase);
void showAskListening();
void showAskThinking();
void showAskProgress(const char* action, const char* object = nullptr);
void showAskSpeaking(const String& answer, int sourceCount);
int askAnswerPageCount(const String& answer);
void showAskSpeakingPage(const String& answer, int sourceCount, int page);
void showTransferConnecting();
void showTransferMode(const char* ip, const char* apPassword = nullptr);
void showVolumeSetting(int percent);
void showSetupRequired();
void showError(const char* message);
bool showSleepLocked();
void showUltraSleepScreen();

void showClockScreen();
void showEventAlert(const String& title,const String& starts);

void showMonthCalendar();
void showWeatherScreen();

void showMailScreen(int page);

void showWakeUpdate(int completed,int total,const char* phase);

void showAskActionProgress(const char* phase,const char* object,int completed,int total);

void showNoConnection(int stage);

bool showCachedWakeHome(bool afterUsbRestart=false);
String cachedWakeHomeDay();

void showRebooting();
void showRebootSleep();
