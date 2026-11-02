#pragma once
#include <Arduino.h>

struct PikaPerformanceStats {
  uint32_t transcriptionMs = 0;
  uint32_t embeddingMs = 0;
  uint32_t answerMs = 0;
  uint32_t speechMs = 0;
};

PikaPerformanceStats pikaPerformanceStats();
void pikaSetBackgroundNetworkMode(bool active);
bool pikaBackgroundNetworkMode();

bool transcribe(const String& wavPath, int noteNum);
bool transcribeQuestion(const String& wavPath, String& question);
bool understandNote(int noteNum);
bool answerFromBrain(const String& question, String& answer, std::vector<int>& sourceNotes);
bool answerAgendaQuestion(const String& question, String& answer, std::vector<int>& sourceNotes);
bool buildDailyBriefing(String& answer, int& taskCount, int& eventCount);
bool buildWeeklyBriefing(String& answer, int& taskCount, int& eventCount);
bool synthesizeBrainSpeech(const String& text, const char* outputPath, bool fast = true, bool russianOnly = false);
bool playBrainSpeech(const String& text, const char* fallbackPath, int sourceCount, bool fast = true, void (*speechScreen)() = nullptr, bool keepWifi = false, bool* cancelled = nullptr, bool russianOnly = false, bool cancelOnLower = false);
int processPendingNotes(int preferredNote = -1, int maxNotes = 1);

void setupTransferServer();
void stopTransferMode();

bool matchAnswerLanguage(const String& question, String& answer);

void pikaManagerIdleService();


bool applyAskAssistant(const String& question,String& answer,std::vector<int>& sources);
void pikaRememberAskTurn(const String& question,const String& answer);

void pikaSetVoiceMutationContext(bool allowed);
bool pikaVoiceMutationContext();
bool pikaMailBrief(const String& sender,const String& subject,const String& body,String& speech);
void pikaAskSetDryRun(bool value);

bool pikaUnifiedAsk(const String& question,String& answer,std::vector<int>& sources,bool allowActions=true);
bool pikaAskRenew();
String pikaAskContextPath();
String pikaAskHistoryPath();
void pikaAskAppendHistory(const String& question,const String& answer);

void pikaReleaseWifi();

void pikaAskResetCloudAfterRestore();

void pikaPhoneRpcService();

bool pikaAskImportTurn(const String& id,const String& question,const String& answer,const String& captured);
