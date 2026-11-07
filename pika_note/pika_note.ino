#include <esp_heap_caps.h>
#include "src/app/phone_lan.h"
#include "src/app/brand_compat.h"
#include "src/app/recovery_buttons.h"
#include "src/app/device_owner.h"
#include "src/app/offline_ask.h"
#include "src/app/error_log.h"
#include "src/app/phone_sync.h"
#include "src/app/phone_hash.h"
bool usbPhoneTlsRequested=false,usbPhoneSpeechRequested=false,usbPhonePauseRequested=false;uint32_t usbPhonePauseUntil=0;
#include <HTTPClient.h>
#include "src/app/phone_link.h"
#include "src/app/phone_server.h"
#include "src/app/factory_reset.h"
#include "src/app/data_backup.h"
#include "src/app/undo_store.h"
#include "src/app/deferred_http.h"
#include "src/app/ask_stage.h"
#include "src/app/voice_command_policy.h"
#include "src/app/mail_policy.h"
#include "src/app/mail.h"
#include "src/app/home_buttons.h"
#include "src/app/usb_backup.h"
#include "src/app/activity_led.h"
#include "src/app/weather.h"
#include "src/app/web_pin.h"
#include <WiFiClientSecure.h>
#include <Arduino.h>
// Ask planning + TLS certificate verification exceed the default 8 KB stack.
SET_LOOP_TASK_STACK_SIZE(24 * 1024);
#include <SD_MMC.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <esp_partition.h>
#include <vector>
#include <algorithm>

extern "C" {
#include "config.h"
#include "src/i2c_bsp/i2c_bsp.h"
#include "src/audio/audio_bsp.h"
}

#include "types.h"
#include "globals.h"
#include "sounds.h"
#include "src/power/board_power_bsp.h"
#include "src/display/epaper_driver_bsp.h"
#include "src/app/brain.h"
#include "src/app/battery.h"
#include "src/app/network.h"
#include "src/app/notes.h"
#include "src/app/record.h"
#include "src/app/rtc.h"
#include "src/app/settings.h"
#include "src/app/device_extras.h"
#include "src/app/manager_store.h"
#include "src/app/google_calendar.h"
#include "src/app/sleep.h"
#include "src/app/tasks.h"
#include "src/app/ui.h"

board_power_bsp_t board(EPD_PWR_PIN, Audio_PWR_PIN, VBAT_PWR_PIN);
epaper_driver_display* display = nullptr;
std::vector<NoteEntry> noteIndex;

AppState state = STATE_IDLE;
int lastRecNum = -1;
uint32_t lastActivityMs = 0;

PhoneWebServer transferServer(80);
bool transferServerActive = false;
String transferUrl;

bool timeReady = false;
bool audioPlaying = false;
bool stopPlayback = false;

void resetActivity();

enum class PressResult { Released, LongSingle, Chord };

namespace {

bool companionExitArmed = false;
bool idleInputArmed = true;
uint32_t companionGestureStartedAt = 0;
uint32_t nextBackgroundAttemptAt = 0;
bool wakeGoogleSyncPending = true;
bool backlogSyncActive = false;
int backlogSyncTotal = 0;
uint32_t nextHomeClockCheckAt = 0;
uint32_t lastUpperTapAt = 0;
uint32_t lastLowerTapAt = 0;
volatile bool upperPressLatched = false;
volatile bool lowerPressLatched = false;
bool companionUpperWasDown = false;
bool companionLowerWasDown = false;
uint32_t companionVolumeShownAt = 0;
uint32_t companionSingleStartedAt = 0;
int companionSinglePin = -1;
String companionApPassword;
String displayedHomeDay;
bool agendaShowsEvents = false;
uint32_t agendaShownAt = 0;
int agendaPage = 0;
bool setupRequired = false;
uint32_t setupCompanionHoldStartedAt = 0;
int processPendingMemories(int preferredNote, int maxNotes);

String usbSetupLine;

struct FlashSetupHeader {
  char magic[8];
  uint32_t jsonLength;
  uint32_t checksum;
};

uint32_t setupChecksum(const uint8_t* data, size_t length) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < length; i++) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return hash;
}

bool importFlashSetup() {
  const esp_partition_t* partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "pikacfg");
  if (!partition) return false;

  FlashSetupHeader header{};
  if (esp_partition_read(partition, 0, &header, sizeof(header)) != ESP_OK ||
      memcmp(header.magic, "PIKACFG1", 8) != 0 ||
      header.jsonLength == 0 || header.jsonLength > 700) return false;

  std::vector<uint8_t> bytes(header.jsonLength);
  if (esp_partition_read(partition, sizeof(header), bytes.data(), bytes.size()) != ESP_OK ||
      setupChecksum(bytes.data(), bytes.size()) != header.checksum) {
    std::fill(bytes.begin(), bytes.end(), 0);
    esp_partition_erase_range(partition, 0, partition->size);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, bytes.data(), bytes.size())) {
    std::fill(bytes.begin(), bytes.end(), 0);
    esp_partition_erase_range(partition, 0, partition->size);
    return false;
  }
  const String ssid = doc["ssid"] | "";
  const String password = doc["password"] | "";
  const String apiKey = doc["apiKey"] | "";
  const String voice = doc["voice"] | "cedar";
  const String language = doc["language"] | "en";
  const bool valid = ssid.length() && ssid.length() <= 32 && password.length() <= 63 &&
      apiKey.length() >= 11 && apiKey.length() <= 200 &&
      (voice == "cedar" || voice == "coral") &&
      (language == "de" || language == "en" || language == "fr" ||
       language == "es" || language == "it" || language == "pl" || language == "ru" ||
       language == "cs" || language == "uk" || language == "nl" || language == "pt");
       
  std::fill(bytes.begin(), bytes.end(), 0);
  if (!valid) {
    esp_partition_erase_range(partition, 0, partition->size);
    return false;
  }

  const bool saved = pikaSaveSettings(ssid, password, apiKey, voice) &&
                     pikaSetUiLanguage(language);
  // The temporary transfer area contains credentials. Remove it after the
  // single import attempt even if Preferences rejects the write, so a raw API
  // key is never left indefinitely in the staging partition.
  esp_partition_erase_range(partition, 0, partition->size);
  if (saved) {
    Serial.println("[Setup] One-step configuration imported");
  }
  return saved;
}

void replyUsbSetup(const char* status, const char* detail = nullptr) {
  Serial.print("PIKA_SETUP_");
  Serial.print(status);
  if (detail) { Serial.print(':'); Serial.print(detail); }
  Serial.println();
}

String usbAskTestQuestion;
bool usbAskRenewRequested=false;
bool usbFeedbackTestRequested=false;
bool usbReminderTest=false;
bool usbSleepRequested=false;uint32_t usbKeepAwakeUntil=0;
bool usbWeatherRequested=false;
bool usbCalendarRequested=false;
bool usbWifiPerfRequested=false,usbStatusRequested=false;int usbVoiceBench=0;

void applyUsbSetupLine(const String& line) {
  if(line=="CATALOG_STATUS"){pikaPhoneCatalogPrintStatus();return;}
  if(line=="CATALOG_BENCH"){pikaPhoneCatalogBenchmark();return;}
  if(line=="REBOOT"){pikaRecoveryRequestRestart();return;}
#ifdef PIKA_TEST_SETTINGS_RECOVERY
  if(line=="SETTINGS_RECOVERY_PROBE"){Serial.println(pikaOwnerArmRecoveryProbe()?"PIKA_SETTINGS_PROBE_ARMED":"PIKA_SETTINGS_PROBE_FAILED");return;}
#endif
  if(line=="VOICE_BENCH_FAST"){usbVoiceBench=2;return;}
  if(line=="VOICE_BENCH_SLOW"){usbVoiceBench=1;return;}
  if(line=="PERF_WIFI"){usbWifiPerfRequested=true;return;}
  if(line=="PHONE_PAUSE_TEST"){usbPhonePauseRequested=true;return;}
  if(line=="VIBRATION_STATUS"){pikaVibrationPrintStatus();return;}
  if(line=="WAKE_STATUS"){Serial.printf("PIKA_WAKE cause=%d reset=%d pins=0x%llx pwr=%d clock=%d timer_us=%llu uptime=%lu\n",int(esp_sleep_get_wakeup_cause()),int(esp_reset_reason()),(unsigned long long)esp_sleep_get_ext1_wakeup_status(),digitalRead(BTN_PWR),pikaClockMode(),(unsigned long long)pikaNextAlertWakeUs(),(unsigned long)millis());return;}
  if(line=="ERROR_TEST"){pikaError("diagnostic","self-test","Persistent journal test",-999);pikaErrorService();Serial.println("PIKA_ERROR_TEST_QUEUED");return;}
  if(line=="ERROR_STATUS"){JsonDocument doc;pikaErrorRead(doc,0,1);String body;serializeJson(doc,body);Serial.println("PIKA_ERRORS "+body);return;}
  if(line=="PHONE_STATUS"){Serial.println(pikaPhoneDiagnostics());pikaHomeButtonsPrintTrace();return;}
  if(line=="PHONE_SPEECH_TEST"){usbPhoneSpeechRequested=true;return;}
  if(line=="PHONE_TLS_TEST"){usbPhoneTlsRequested=true;return;}
  if(line=="PIKA_STATUS"){usbStatusRequested=true;return;}
  if(line=="ASK_RENEW"){usbAskRenewRequested=true;Serial.println("PIKA_ASK_RENEW_QUEUED");return;}
  if(line=="ASK_FEEDBACK_TEST"){usbFeedbackTestRequested=true;Serial.println("PIKA_FEEDBACK_TEST_QUEUED");return;}
  if(line.startsWith("ASK_EXECUTE ")){usbAskTestQuestion="EXECUTE: "+line.substring(12);Serial.println("PIKA_ASK_EXECUTE_QUEUED");return;}
  if(line=="ASK_TEST"||line.startsWith("ASK_TEST ")){usbAskTestQuestion=line.length()>9?line.substring(9):String("Создай задачи из моей единственной заметки");Serial.println("PIKA_ASK_TEST_QUEUED");return;}
  if(line=="CALENDAR"){usbCalendarRequested=true;Serial.println("PIKA_CALENDAR_QUEUED");return;}
  if(line=="WEATHER"){usbWeatherRequested=true;Serial.println("PIKA_WEATHER_QUEUED");return;}
  if(line=="REMINDER_TEST"){usbReminderTest=true;Serial.println("PIKA_REMINDER_TEST_QUEUED");return;}
  if(line=="TEST_HOLD"){usbKeepAwakeUntil=millis()+600000;Serial.println("PIKA_TEST_HOLD_10_MIN");return;}
  if(line=="TEST_RELEASE"){usbKeepAwakeUntil=0;resetActivity();Serial.println("PIKA_TEST_HOLD_RELEASED");return;}
  if(line=="SLEEP"){usbSleepRequested=true;Serial.println("PIKA_SLEEP_QUEUED");return;}
  if (!line.startsWith("PIKA_SETUP_V1 ")) return;
  if (line.length() > 700) { replyUsbSetup("ERROR", "TOO_LONG"); return; }

  JsonDocument doc;
  if (deserializeJson(doc, line.substring(14))) {
    replyUsbSetup("ERROR", "INVALID_JSON");
    return;
  }
  const String ssid = doc["ssid"] | "";
  const String password = doc["password"] | "";
  const String apiKey = doc["apiKey"] | "";
  const String voice = doc["voice"] | "cedar";
  const String language = doc["language"] | "en";
  if (!ssid.length() || ssid.length() > 32 || password.length() > 63 ||
      apiKey.length() < 11 || apiKey.length() > 200 ||
      (voice != "cedar" && voice != "coral") ||
      (language != "de" && language != "en" && language != "fr" &&
       language != "es" && language != "it" && language != "pl" && language != "ru" &&
       language != "cs" && language != "uk" && language != "nl" && language != "pt")) {
    replyUsbSetup("ERROR", "INVALID_VALUES");
    return;
  }
  if (!pikaSaveSettings(ssid, password, apiKey, voice) || !pikaSetUiLanguage(language)) {
    replyUsbSetup("ERROR", "SAVE_FAILED");
    return;
  }
  replyUsbSetup("OK");
  Serial.flush();
  // Give WebSerial enough time to receive the acknowledgement before USB is
  // torn down by the restart. A short delay was unreliable on macOS/Chrome.
  delay(1500);
  ESP.restart();
}

void handleUsbSetup() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n') {
      usbSetupLine.trim();
      applyUsbSetupLine(usbSetupLine);
      usbSetupLine = "";
    } else if (c != '\r') {
      if (usbSetupLine.length() < 720) usbSetupLine += c;
      else usbSetupLine = "";
    }
  }
}

bool bothButtonsDown() {
  return digitalRead(BTN_REC) == LOW && digitalRead(BTN_PWR) == LOW;
}

bool bothButtonsUp() {
  return digitalRead(BTN_REC) == HIGH && digitalRead(BTN_PWR) == HIGH;
}

void latchUpperPress() { upperPressLatched = true; }
void latchLowerPress() { lowerPressLatched = true;pikaSleepLowerEdge(); }

void clearLatchedPresses() {
  noInterrupts();
  upperPressLatched = false;
  lowerPressLatched = false;
  interrupts();
}

bool takeLatchedPress(bool upper) {
  noInterrupts();
  const bool latched = upper ? upperPressLatched : lowerPressLatched;
  if (upper) upperPressLatched = false;
  else lowerPressLatched = false;
  interrupts();
  return latched;
}

void powerOnPeripherals() {
  pinMode(PWR_HOLD_PIN, OUTPUT);
  digitalWrite(PWR_HOLD_PIN, HIGH);
  board.VBAT_POWER_ON();
  board.POWEER_EPD_ON();
  board.POWEER_Audio_ON();
  delay(200);
}

// Short foreground lease avoids reassociation between successive questions.
uint32_t wifiLeaseEndsAt=0;
void stopWifi(bool force=false){if(force||!pikaLanConfigured())WiFi.mode(WIFI_OFF);}
void serviceWifiLease(){}

String localDayKey() {
  String local = utcToLocalDeviceLabel(currentUtcIso());
  return local.length() >= 10 && local != "time not set" ? local.substring(0, 10) : "";
}

void refreshPhoneHome(){if(!pikaOwnerBound()){showPairingOnly();return;}pikaHomeButtonsEnable(true);loadIndex();const BatteryReading battery=readBattery();showIdle(0,battery.valid?battery.percent:-1);}
void renderReadyHome() {
  if(!pikaOwnerBound()){pikaHomeButtonsEnable(false);showPairingOnly();return;}
  pikaHomeButtonsReset();idleInputArmed=false;lastUpperTapAt=0;lastLowerTapAt=0;clearLatchedPresses();
  loadIndex(); // Refresh the catalog before computing all three Home counters.
  const BatteryReading battery = readBattery();
  // The home screen only exposes recordings that are not transcribed yet.
  // Rebuilding structure/vector indexes is silent maintenance: the transcript
  // is already searchable and must not leave a permanent "waiting" badge.
  int untranscribed = 0;
  for (const NoteEntry& note : noteIndex) {
    if (!note.hasText && noteQueueState(note.num) != "blocked") untranscribed++;
  }
  pikaHomeButtonsEnable(true); // Recognize fresh presses during the panel refresh.
  showIdle(untranscribed, battery.valid ? battery.percent : -1);
  displayedHomeDay = localDayKey();
  nextHomeClockCheckAt = millis() + 60000UL;
  clearLatchedPresses();
  pikaHomeButtonsEnable(true); // Capture the first fresh gesture before background work resumes.
}

void returnFromBackgroundSync() {
  renderReadyHome();
  // Presses during network/display work must not become agenda shortcuts.
  clearLatchedPresses();
  idleInputArmed = false;
  lastUpperTapAt = 0;
  lastLowerTapAt = 0;
}

void returnToReady() {
  pikaScreenButtonsEnable(false);
  pikaPhoneRequestHomeRefresh();nextBackgroundAttemptAt=millis()+BACKGROUND_RETRY_MS;
  pikaHomeButtonsEnable(false);
  stopWifi();
  state = STATE_IDLE;
  idleInputArmed = false;
  resetActivity();
  // Discard the edge that selected Back, then retain any new edge occurring
  // while the e-paper is busy redrawing Home.
  clearLatchedPresses();
  renderReadyHome();pikaPhoneHomeDirty();
  audio_idle_suspend();pikaPhoneForeground(false);
}

void browseAnswerPages(const String& answer, int sourceCount) {
  // The REC press that stopped speech belongs to playback, not navigation.
  // The caller has already displayed page one. Avoid another panel refresh.
  bool upperArmed = digitalRead(BTN_REC) == HIGH;
  const int pages = askAnswerPageCount(answer);
  int page = 0;
  bool lowerWasDown = digitalRead(BTN_PWR) == LOW;
  uint32_t closesAt = millis() + 15000UL;
  while (static_cast<int32_t>(millis() - closesAt) < 0) {
    if (digitalRead(BTN_REC) == HIGH) upperArmed = true;
    else if (upperArmed) {
      break;
    }
    const bool lowerDown = digitalRead(BTN_PWR) == LOW;
    if (lowerDown && !lowerWasDown) {
      page = (page + 1) % pages;
      showAskSpeakingPage(answer, sourceCount, page);
      closesAt = millis() + 15000UL;
    }
    lowerWasDown = lowerDown;
    delay(10);
  }
}

bool connectConfiguredWifi(uint8_t attempts=20,bool refreshWeather=false){
 PikaConnectionLight light;const bool ok=pikaPhoneWait(std::min<uint32_t>(8000,attempts*400UL));if(ok&&refreshWeather)pikaWeatherRefresh();return ok;
}

void handleWakeGoogleSync(){
 if(!wakeGoogleSyncPending||!pikaPhoneOnline()||!idleInputArmed||!bothButtonsUp()||pikaHomeButtonsBusy()||pikaPhoneTransferRecent())return;
 const bool clock=rtcNtpSyncDue(),weather=pikaWeatherPending(),calendar=pikaGoogleHasPendingChanges();
 wakeGoogleSyncPending=false;if(!clock&&!weather&&!calendar)return;
 pikaPhoneBackgroundReset();pikaSetBackgroundNetworkMode(true);
 if(clock){rtcMarkNtpAttempt();syncTimeFromNTP(4000);}
 if(weather&&!pikaHomeButtonsBusy())pikaWeatherRefresh();
 if(calendar&&!pikaHomeButtonsBusy())pikaGoogleSyncBatch();
 pikaSetBackgroundNetworkMode(false);if(pikaPhoneBackgroundInterrupted())wakeGoogleSyncPending=true;else pikaPhoneChanged();
}

int processPendingMemories(int preferredNote = -1, int maxNotes = 1) {
  const uint8_t attempts = pikaBackgroundNetworkMode() ? 8 : 20;
  if (!pikaHasOpenAiKey() || !connectConfiguredWifi(attempts)) {
    stopWifi();
    return 0;
  }
  loadIndex();
  const int processed = processPendingNotes(preferredNote, maxNotes);
  loadIndex();
  // Keep one stable Wi-Fi/TLS environment while the background queue still
  // has work. Rapid WIFI_OFF -> reconnect cycles produced invalid sockets on
  // the ESP32-S3. Foreground actions still shut Wi-Fi down immediately.
  if (!pikaBackgroundNetworkMode()) stopWifi();
  return processed;
}

void enterCompanion(){
 pikaPhoneForeground(false);pikaHomeButtonsEnable(false);resetActivity();pikaSetClockMode(false);
 companionGestureStartedAt=0;companionSingleStartedAt=0;companionSinglePin=-1;clearLatchedPresses();
 state=STATE_IDLE;pikaPhoneRequestSync();renderReadyHome();
 Serial.println("PIKA_PHONE_SYNC_REQUESTED");
}

void leaveCompanion() {
  soundBack();
  stopTransferMode();
  companionExitArmed = false;
  companionGestureStartedAt = 0;
  companionSingleStartedAt = 0;
  companionSinglePin = -1;
  state = STATE_IDLE;
  idleInputArmed = false;
  clearLatchedPresses();
  resetActivity();
  renderReadyHome();
  clearLatchedPresses();
}

bool processCapturedMemoryNow(int noteNum) {
  PikaBusyLight busyLight;
  // A disconnected phone is normal for a pocket recorder: the WAV/index are
  // already durable. Do not wait for a transport before acknowledging capture.
  if(!pikaPhoneOnline()){
    writeNoteQueueState(noteNum,"queued","saved locally; waiting for companion");
    pikaPlayNotify(2);showMemoryProcessing(noteNum,"QUEUED");delay(250);return false;
  }
  showMemoryProcessing(noteNum, "CONNECTING");
  if (!pikaHasWifi() || !pikaHasOpenAiKey() || !connectConfiguredWifi(14)) {
    stopWifi();
    writeNoteQueueState(noteNum, "queued", "saved locally; waiting for connection");
    pikaPlayNotify(2);
    showMemoryProcessing(noteNum, "QUEUED");
    delay(250);
    return false;
  }

  loadIndex();
  bool ready = false;
  // Exactly one durable network stage per iteration. The screen therefore
  // always describes the operation that can currently take time.
  // Finish the two user-visible essentials now: transcription and structured
  // understanding. The optional semantic embedding is derived maintenance and
  // must never keep a freshly captured thought on a processing screen.
  for (int stage = 0; stage < 2; stage++) {
    auto entry = std::find_if(noteIndex.begin(), noteIndex.end(), [noteNum](const NoteEntry& note) {
      return note.num == noteNum;
    });
    if (entry == noteIndex.end()) break;
    if (!entry->hasText) showMemoryProcessing(noteNum, "DECODING");
    else if (!brainMemoryIsCurrent(noteNum)) showMemoryProcessing(noteNum, "UNDERSTANDING");
    else if (!brainEmbeddingExists(noteNum)) showMemoryProcessing(noteNum, "REMEMBERING");
    else { ready = true; break; }

    if (processPendingNotes(noteNum, 1) <= 0) break;
    loadIndex();
  }
  ready = ready || brainMemoryIsCurrent(noteNum);
  stopWifi();
  if (noteQueueError(noteNum) == "no speech recognized") {
    pikaPlayNotify(2);showError(pikaUiLanguage() == "de" ? "KEINE SPRACHE" : "NO SPEECH");
    delay(900);
    return false;
  }
  if (ready) {
    writeNoteQueueState(noteNum, "ready");
    // Build/publish while the processing screen is visible, not behind Saved.
    pikaPhonePublishLiveSnapshot();
    pikaPlayNotify(3);
    showSaved(noteNum);
    delay(350);
  } else {
    pikaPhonePublishLiveSnapshot();pikaPlayNotify(2);
    if (noteQueueState(noteNum) == "blocked") showError("PROCESSING FAILED");
    else showMemoryProcessing(noteNum, "QUEUED");
    delay(250);
  }
  return ready;
}

void redrawCompanionHome() {
  showTransferMode(transferUrl.c_str(),
                   companionApPassword.length() ? companionApPassword.c_str() : nullptr);
}

void captureMemory(int holdPin=BTN_REC) {
  PikaBusyLight busyLight;
  constexpr uint64_t kMinimumFreeBytes = 10ULL * 1024ULL * 1024ULL;
  if (SD_MMC.totalBytes() <= SD_MMC.usedBytes() ||
      SD_MMC.totalBytes() - SD_MMC.usedBytes() < kMinimumFreeBytes) {
    showError("STORAGE FULL");
    delay(1600);
    returnToReady();
    return;
  }
  state = STATE_RECORDING;
  // E-paper refreshes are blocking. Starting one here used to delay I2S reads
  // and cut the first spoken words from the recording.
  pikaSoundSetEnabled(false);
  const bool recorded = record(holdPin);
  pikaSoundSetEnabled(true);

  if (!recorded&&recordWasTooShort()){returnToReady();return;}
  if (!recorded) {
    showError("RECORD AGAIN");
    delay(1300);
    returnToReady();
    return;
  }

  if (!saveTag(lastRecNum, "Inbox")) {
    // The WAV is retained for orphan recovery; never claim a failed index
    // commit succeeded or launch cloud work against an uncommitted capture.
    showError("STORAGE FULL"); delay(1300); returnToReady(); return;
  }
  pikaPlayNotify(3);
  showSaved(lastRecNum);
  delay(350);

  // The WAV is durable before any network work starts. Process this specific
  // memory visibly now; if the network fails it remains safely queued.
  processCapturedMemoryNow(lastRecNum);
  // Do not make the freshly returned home screen mysteriously unresponsive by
  // immediately starting unrelated backlog work.
  nextBackgroundAttemptAt = pikaPhoneOnline() ? millis() + BACKGROUND_RETRY_MS : 0;
  returnToReady();
}

bool phoneRequired(){if(pikaPhoneOnline())return true;showAskProgress("CONNECTING","PHONE");if(pikaPhoneWait(8000))return true;pikaError("connection","phoneRequired","Companion unavailable after reconnect wait",0,8000);pikaPlayNotify(2);for(int i=1;i<=3;i++){uint32_t at=millis();showNoConnection(i);while(millis()-at<333)delay(1);}returnToReady();return false;}
void askBrain(int holdPin=BTN_PWR) {
  PikaBusyLight busyLight;
  if (!pikaHasWifi() || !pikaHasOpenAiKey()) {
    enterCompanion();
    return;
  }

  state = STATE_ASKING;
  constexpr const char* kQuestionPath = NOTES_DIR "/.ask.wav";
  constexpr const char* kAnswerPath = NOTES_DIR "/.pika-answer.wav";

  pikaSoundSetEnabled(false);
  const bool recorded = recordQuestion(kQuestionPath, holdPin);
  pikaSoundSetEnabled(true);
  if (!recorded&&recordWasTooShort()){returnToReady();return;}
  if (!recorded) {
    showError("ASK AGAIN");
    delay(1200);
    returnToReady();
    return;
  }

  const String offlineCapturedUtc=currentUtcIso();
  auto queueAsk=[&](){stopWifi();if(pikaQueueOfflineAsk(kQuestionPath,offlineCapturedUtc)){showMemoryProcessing(0,"QUEUED");delay(250);}else{showError("SAVE FAILED");pikaPlayNotify(2);delay(800);}returnToReady();};
  if(pikaOfflineAskPending()){queueAsk();return;}
  if(!pikaPhoneOnline()&&!pikaPhoneWait(1500)){queueAsk();return;}
  showAskProgress("CONNECTING", nullptr);
  if (!connectConfiguredWifi()) {
    stopWifi();
    queueAsk();return;
  }

  loadIndex();

  String question;
  String answer;
  std::vector<int> sources;
  showAskProgress("DECODING", "YOUR QUESTION");
  const bool questionReady = transcribeQuestion(kQuestionPath, question);
  if (!questionReady) {queueAsk();return;}
  struct MutationScope{MutationScope(bool allowed){pikaSetVoiceMutationContext(allowed);}~MutationScope(){pikaSetVoiceMutationContext(false);}} mutationScope(voiceMutationGestureAllowed(holdPin,BTN_PWR));
  if(pikaMailConfigured()&&pikaMailQuestion(question)){showAskProgress("UPDATING","MAIL");pikaMailRequestRefresh();pikaRunAskStage([](void*){pikaMailRefresh();},nullptr);}
  const uint32_t askBeforeRevision=managerRevision();
  showAskThinking();
  bool answered = pikaUnifiedAsk(question,answer,sources);
  if (answered) {answered = matchAnswerLanguage(question, answer);}
  if (!answered) {
    if(managerRevision()==askBeforeRevision){queueAsk();return;}
    stopWifi();
    showError("NO ANSWER");
    delay(1400);
    returnToReady();
    return;
  }

#if PIKA_PRIVATE_DIAGNOSTICS
  Serial.printf("[Ask] %s\n[Answer] %s\n", question.c_str(), answer.c_str());
#endif
  showAskProgress("BUILDING", "VOICE");
  if(pikaGoogleSyncEnabled()&&pikaGoogleHasPendingChanges()){
    bool synced=false;
    const bool ran=pikaRunAskStage([](void* p){
      showAskActionProgress("SYNCING","CALENDAR",0,1);pikaGoogleSyncBatch();
      *static_cast<bool*>(p)=!pikaGoogleHasPendingChanges();
      showAskActionProgress("SYNCING",*static_cast<bool*>(p)?"CALENDAR":"PENDING",*static_cast<bool*>(p)?1:0,1);
    },&synced);
    const bool ru=pikaMostlyCyrillic(question.c_str());
    answer+=ru?(ran&&synced?"\nКалендарь Google синхронизирован.":"\nОтправка в Google Calendar ожидает синхронизации; локальные записи сохранены."):(ran&&synced?"\nGoogle Calendar synced.":"\nGoogle Calendar sync is pending; local records are saved.");
  }
  pikaRememberAskTurn(question,answer);
  if(managerRevision()!=askBeforeRevision){const bool pushed=pikaPhonePublishLiveSnapshot();Serial.printf("PIKA_EARLY_RECORDS_PUSH queued=%d\n",pushed);}
  showAskProgress("BUILDING", "VOICE");
  pikaPlayNotify(1);
  const bool speechReady = playBrainSpeech(answer, kAnswerPath, static_cast<int>(sources.size()),true,nullptr,true);
  if (!speechReady) {
    stopWifi();
    showError("VOICE FAILED");
    delay(1000);
    showAskSpeaking(answer, static_cast<int>(sources.size()));
  }
  stopWifi();
  busyLight.stop();
  browseAnswerPages(answer, static_cast<int>(sources.size()));
  returnToReady();
}

void playDailyBriefing() {
  PikaBusyLight busyLight;
  lastUpperTapAt = 0;
  lastLowerTapAt = 0;
  if (!pikaHasWifi() || !pikaHasOpenAiKey()) {
    enterCompanion();
    return;
  }
  state = STATE_ASKING;
  constexpr const char* kBriefingPath = NOTES_DIR "/.pika-briefing.wav";
  showAskProgress("CONNECTING", "TODAY");
  if (!connectConfiguredWifi()) {
    stopWifi();
    showError("NO WIFI");
    delay(1400);
    returnToReady();
    return;
  }
  loadIndex();
  showAskProgress("THINKING", "DAILY BRIEF");
  String answer;
  int tasks = 0, events = 0;
  if (!buildDailyBriefing(answer, tasks, events)) {
    stopWifi();
    showError("BRIEF FAILED");
    delay(1400);
    returnToReady();
    return;
  }
  Serial.printf("[Briefing] %d tasks, %d events; answer=%u characters\n", tasks, events, answer.length());
  showAskProgress("BUILDING", "VOICE");
  const bool speechReady = playBrainSpeech(answer, kBriefingPath, tasks + events, true);
  if (!speechReady) {
    stopWifi();
    showError("VOICE FAILED");
    delay(1000);
    showAskSpeaking(answer, tasks + events);
  }
  stopWifi();
  busyLight.stop();
  browseAnswerPages(answer, tasks + events);
  returnToReady();
}

void playWeeklyBriefing() {
  PikaBusyLight busyLight;
  lastUpperTapAt = 0;
  lastLowerTapAt = 0;
  if (!pikaHasWifi() || !pikaHasOpenAiKey()) {
    enterCompanion();
    return;
  }
  state = STATE_ASKING;
  constexpr const char* kBriefingPath = NOTES_DIR "/.pika-weekly.wav";
  showAskProgress("CONNECTING", "THIS WEEK");
  if (!connectConfiguredWifi()) {
    stopWifi();
    showError("NO WIFI");
    delay(1400);
    returnToReady();
    return;
  }
  loadIndex();
  showAskProgress("THINKING", "WEEKLY BRIEF");
  String answer;
  int tasks = 0, events = 0;
  if (!buildWeeklyBriefing(answer, tasks, events)) {
    stopWifi();
    showError("BRIEF FAILED");
    delay(1400);
    returnToReady();
    return;
  }
  showAskProgress("BUILDING", "VOICE");
  const bool speechReady = playBrainSpeech(answer, kBriefingPath, tasks + events, true);
  if (!speechReady) {
    stopWifi();
    showError("VOICE FAILED");
    delay(1000);
    showAskSpeaking(answer, tasks + events);
  }
  stopWifi();
  busyLight.stop();
  browseAnswerPages(answer, tasks + events);
  returnToReady();
}

PressResult classifyPress(int pin) {
  if (digitalRead(pin) != LOW) return PressResult::Released;
  delay(14);
  if (digitalRead(pin) != LOW) return PressResult::Released;
  const uint32_t pressedAt = millis();
  while (digitalRead(pin) == LOW && millis() - pressedAt < REC_HOLD_MS) {
    // A two-button gesture always wins, even when the fingers land a little
    // apart. The old blocking hold detector missed this transition and began
    // Capture/Ask before Companion could see both buttons.
    if (bothButtonsDown()) {
      companionGestureStartedAt = millis();
      lastUpperTapAt = 0;
      lastLowerTapAt = 0;
      return PressResult::Chord;
    }
    delay(4);
  }
  return digitalRead(pin) == LOW ? PressResult::LongSingle : PressResult::Released;
}

void runEventReminder(const BrainTaskRecord& event,bool test=false){
  pikaHomeButtonsEnable(false);
  if(!test)pikaMarkAlert(event);clearLatchedPresses();showEventAlert(event.text,managerUtcToLocalInput(event.dueUtc));
  const uint32_t started=millis();uint32_t nextBeep=0;bool armed=bothButtonsUp();
  while(millis()-started<(test?3000UL:(uint32_t)pikaAlertDurationSeconds()*1000UL)){
    if(bothButtonsUp())armed=true;
    if(armed&&(upperPressLatched||lowerPressLatched||!bothButtonsUp())){while(!bothButtonsUp())delay(5);break;}
    if(millis()-started>=nextBeep){Serial.println("PIKA_EVENT_REMINDER_BEEP");pikaPlayAlertMelody();nextBeep+=10000;}
    delay(10);
  }
  clearLatchedPresses();
  if(pikaClockMode()){state=STATE_CLOCK;showClockScreen();}else returnToReady();
}
void handleEventReminder(){BrainTaskRecord event;if(pikaFindDueAlert(event))runEventReminder(event);}
void handleClockMode(){
  // Clock intentionally has no idle timeout; refresh every minute until any key.
  // Stay in the input loop: deep-sleep boot/refresh windows could swallow Back.
  if(upperPressLatched||lowerPressLatched||!bothButtonsUp()){
    pikaSetClockMode(false);while(!bothButtonsUp())delay(5);
    clearLatchedPresses();returnToReady();return;
  }
  static time_t displayedMinute=0;const time_t minute=time(nullptr)/60;
  if(minute!=displayedMinute){displayedMinute=minute;showClockScreen();}
}

bool extraScreenArmed=false;
void armExtraScreen(){while(!bothButtonsUp())delay(4);clearLatchedPresses();pikaScreenButtonsEnable(true);extraScreenArmed=true;}
void handleExtraScreen(){
 if(!extraScreenArmed){if(bothButtonsUp()){extraScreenArmed=true;clearLatchedPresses();}return;}
 if(pikaScreenButtonsTake()||upperPressLatched||lowerPressLatched||!bothButtonsUp()){Serial.printf("PIKA_SCREEN_BACK at=%lu\n",(unsigned long)millis());while(!bothButtonsUp())delay(5);clearLatchedPresses();returnToReady();return;}
 // E-paper retains its last image in deep sleep: restore Today first.
 if(millis()-lastActivityMs>=pikaSleepIdleMs()){returnToReady();enterUltraSleep();}
}
void openWeatherScreen(){if(!phoneRequired())return;armExtraScreen();PikaBusyLight busyLight;if(pikaWeatherConfigured()){pikaWeatherRequestRefresh();showAskProgress("CONNECTING","WEATHER");if(connectConfiguredWifi(8,false)){if(!pikaWeatherRefresh()&&!pikaScreenButtonsPending()&&bothButtonsUp())pikaPlayNotify(2);}else if(!pikaScreenButtonsPending()&&bothButtonsUp())pikaPlayNotify(2);stopWifi();}else{pikaError("weather","configuration","Weather location not configured");pikaPlayNotify(2);}state=STATE_WEATHER;resetActivity();showWeatherScreen();pikaPhoneForeground(false);}
int mailPage=0;uint32_t mailShownAt=0;bool mailBackRequested=false;
void renderMailPage(){showMailScreen(mailPage);}
void speakMailPage(){
  if((pikaScreenButtonsPending()&2)||lowerPressLatched||digitalRead(BTN_PWR)==LOW){mailBackRequested=true;return;}
  PikaBusyLight busyLight;
 if(!pikaHasOpenAiKey()||pikaMailItems().empty())return;
 if(WiFi.status()!=WL_CONNECTED&&!connectConfiguredWifi(8))return;
 bool stopped=false;String lastSubject;
 for(int i=mailPage*4;i<(int)pikaMailItems().size()&&i<mailPage*4+4&&!stopped;i++){
  if((pikaScreenButtonsPending()&2)||lowerPressLatched||digitalRead(BTN_PWR)==LOW){mailBackRequested=true;break;}
  const auto& mail=pikaMailItems()[i];const bool repeatSubject=mail.subject==lastSubject&&lastSubject.length();String speech=repeatSubject?String():mail.brief;
  if(!speech.length()){
   if(pikaMailBrief(mail.from,repeatSubject?String():mail.subject,mail.body.length()?mail.body:mail.snippet,speech)){if(!repeatSubject)pikaMailSaveBrief(mail.id,speech);}
   else speech=pikaMailItemSpeech(i,!repeatSubject);
  }
  lastSubject=mail.subject;
  const auto chunks=pikaMailSpeechChunks(std::string(speech.c_str()));
  for(const auto& chunk:chunks){
   if((pikaScreenButtonsPending()&2)||lowerPressLatched||digitalRead(BTN_PWR)==LOW){mailBackRequested=true;stopped=true;break;}
   bool cancelled=false;
   if(!playBrainSpeech(String(chunk.c_str()),NOTES_DIR "/.mail-speech.wav",0,true,renderMailPage,true,&cancelled,true,true)||cancelled){if(!cancelled&&!(pikaScreenButtonsPending()&2)&&!lowerPressLatched&&digitalRead(BTN_PWR)!=LOW)pikaPlayNotify(2);stopped=true;break;}
  }
 }
 if((pikaScreenButtonsPending()&2)||lowerPressLatched||digitalRead(BTN_PWR)==LOW)mailBackRequested=true;
 SD_MMC.remove(NOTES_DIR "/.mail-speech.wav");stopWifi();
}

void openMailScreen(){if(!phoneRequired())return;
  PikaBusyLight busyLight;
 pikaHomeButtonsEnable(false);while(!bothButtonsUp())delay(4);clearLatchedPresses();pikaScreenButtonsEnable(true);mailBackRequested=false;state=STATE_MAIL;mailPage=0;pikaMailRequestRefresh();
 if(pikaMailConfigured()){showAskProgress("CONNECTING","MAIL");if(connectConfiguredWifi(8)){if(!pikaMailRefresh()&&!(pikaScreenButtonsPending()&2)&&!lowerPressLatched&&digitalRead(BTN_PWR)!=LOW)pikaPlayNotify(2);}else if(!(pikaScreenButtonsPending()&2)&&!lowerPressLatched&&digitalRead(BTN_PWR)!=LOW)pikaPlayNotify(2);}else{pikaError("mail","configuration","Mail account not configured");pikaPlayNotify(2);}
 if((pikaScreenButtonsPending()&2)||lowerPressLatched||digitalRead(BTN_PWR)==LOW)mailBackRequested=true;
 if(!mailBackRequested){renderMailPage();speakMailPage();}stopWifi();pikaPhoneForeground(false);
 if(mailBackRequested||lowerPressLatched){while(!bothButtonsUp())delay(4);clearLatchedPresses();returnToReady();return;}clearLatchedPresses();
 while(!bothButtonsUp())delay(4);mailShownAt=millis();resetActivity();
}
void handleMailScreen(){
 if(millis()-mailShownAt>=pikaSleepIdleMs()){returnToReady();enterUltraSleep();return;}
 const unsigned presses=pikaScreenButtonsTake();const bool lower=(presses&2)||lowerPressLatched||digitalRead(BTN_PWR)==LOW,upper=(presses&1)||upperPressLatched||digitalRead(BTN_REC)==LOW;
 if(!lower&&!upper)return;
 while(!bothButtonsUp())delay(4);clearLatchedPresses();
 if(lower||++mailPage>=max(1,((int)pikaMailItems().size()+3)/4)){returnToReady();return;}
 renderMailPage();speakMailPage();stopWifi();if(mailBackRequested||lowerPressLatched){while(!bothButtonsUp())delay(4);clearLatchedPresses();returnToReady();return;}while(!bothButtonsUp())delay(4);clearLatchedPresses();mailShownAt=millis();resetActivity();
}
void dispatchHomeAction(int slot,int pin){
  pikaHomeButtonsEnable(false);
  const String action=pikaButtonAction(slot);lastUpperTapAt=0;lastLowerTapAt=0;clearLatchedPresses();
  if(action=="record"||action=="ask"||action=="daily"||action=="weekly"||action=="mail"||action=="weather"){pikaPhoneHashCancel();pikaPhoneForeground(true);}
  if(action=="record"){captureMemory(pin);return;}
  if(action=="ask"){askBrain(pin);return;}
  if(slot==2||slot==6){while(digitalRead(pin)==LOW)delay(5);clearLatchedPresses();}
  if(action=="daily"){playDailyBriefing();return;}
  if(action=="weekly"){playWeeklyBriefing();return;}
  if(action=="companion"){enterCompanion();return;}
  if(action=="calendar"){armExtraScreen();state=STATE_CALENDAR;resetActivity();showMonthCalendar();return;}
  if(action=="mail"){openMailScreen();return;}
  if(action=="weather"){openWeatherScreen();return;}
  if(action=="clock"){stopWifi(true);audio_idle_suspend();pikaSetClockMode(true);state=STATE_CLOCK;showClockScreen();return;}
  if(action=="tasks"||action=="events"){state=STATE_AGENDA;agendaShowsEvents=action=="events";agendaPage=0;agendaShownAt=millis();showTodayAgenda(agendaShowsEvents,0);}
}
void handleIdle(){
  pikaHomeButtonsEnable(true);
  idleInputArmed=bothButtonsUp();
  int slot;
  if(!pikaHomeButtonsTake(slot))return;
  Serial.printf("PIKA_HOME_ACTION slot=%d delay_ms=%lu held=%d\n",slot,(unsigned long)pikaHomeButtonsDispatchDelay(),digitalRead(slot<4?BTN_REC:BTN_PWR)==LOW);
  resetActivity();clearLatchedPresses();
  if(slot==8){pikaHomeButtonsEnable(false);soundSelect();enterCompanion();return;}
  dispatchHomeAction(slot,slot<4?BTN_REC:BTN_PWR);
}

void handleAgenda() {
  if (millis() - agendaShownAt >= 30000UL) {
    returnToReady();
    return;
  }
  if (bothButtonsDown()) {
    if (companionGestureStartedAt == 0) companionGestureStartedAt = millis();
    if (millis() - companionGestureStartedAt >= COMPANION_HOLD_MS) enterCompanion();
    return;
  }
  companionGestureStartedAt = 0;

  const bool upperLatched = upperPressLatched;
  const bool lowerLatched = lowerPressLatched;
  if (digitalRead(BTN_REC) == LOW || (upperLatched && !lowerLatched)) {
    takeLatchedPress(true);
    const PressResult press = digitalRead(BTN_REC) == LOW
      ? classifyPress(BTN_REC) : PressResult::Released;
    if (press == PressResult::Chord) return;
    resetActivity();
    if (!agendaShowsEvents && agendaPage + 1 < todayAgendaPageCount(false)) {
      agendaPage++;
      agendaShownAt = millis();
      showTodayAgenda(false, agendaPage);
    } else returnToReady();
    return;
  }
  if (digitalRead(BTN_PWR) == LOW || lowerLatched) {
    takeLatchedPress(false);
    const PressResult press = digitalRead(BTN_PWR) == LOW
      ? classifyPress(BTN_PWR) : PressResult::Released;
    if (press == PressResult::Chord) return;
    resetActivity();
    if (agendaShowsEvents && agendaPage + 1 < todayAgendaPageCount(true)) {
      agendaPage++;
      agendaShownAt = millis();
      showTodayAgenda(true, agendaPage);
    } else returnToReady();
  }
}

void handleBackgroundBrain(){
 if(pikaPhoneSyncActive())return;
 if(!pikaPhoneOnline()||!pikaHasOpenAiKey()||pikaHomeButtonsBusy()||!bothButtonsUp()||pikaPhoneTransferRecent())return;
 if(millis()-lastActivityMs<BACKGROUND_IDLE_MS||millis()-lastActivityMs>=pikaSleepIdleMs())return;
 if(nextBackgroundAttemptAt&&int32_t(millis()-nextBackgroundAttemptAt)<0)return;
 if(!pikaPhoneOnline())return;
 pikaPhoneBackgroundReset();pikaSetBackgroundNetworkMode(true);const int done=processPendingMemories(-1,1);pikaSetBackgroundNetworkMode(false);
 nextBackgroundAttemptAt=millis()+(pikaPhoneBackgroundInterrupted()?3000:BACKGROUND_RETRY_MS);
 if(done>0)pikaPhoneChanged();
}

void handleCompanion() {
  if (transferServerActive) transferServer.handleClient();
  pikaManagerIdleService();
  pikaServiceSoundTest();

  const bool upperDown = digitalRead(BTN_REC) == LOW;
  const bool lowerDown = digitalRead(BTN_PWR) == LOW;

  if (upperDown || lowerDown) resetActivity();
  if (millis() - lastActivityMs >= COMPANION_IDLE_SLEEP_MS) {
    leaveCompanion();
    enterUltraSleep();
    return;
  }

  if (!companionExitArmed) {
    companionUpperWasDown = upperDown;
    companionLowerWasDown = lowerDown;
    if (!upperDown && !lowerDown) companionExitArmed = true;
    return;
  }

  if (upperDown && lowerDown) {
    companionSingleStartedAt = 0;
    companionSinglePin = -1;
    if (companionGestureStartedAt == 0) companionGestureStartedAt = millis();
    if (millis() - companionGestureStartedAt >= COMPANION_HOLD_MS) {
      leaveCompanion();
      return;
    }
    companionUpperWasDown = true;
    companionLowerWasDown = true;
    return;
  }

  companionGestureStartedAt = 0;
  if (upperDown && !companionUpperWasDown) {
    companionSingleStartedAt = millis();
    companionSinglePin = BTN_REC;
  } else if (lowerDown && !companionLowerWasDown) {
    companionSingleStartedAt = millis();
    companionSinglePin = BTN_PWR;
  }

  if (companionSinglePin >= 0 && millis() - companionSingleStartedAt >= 140UL) {
    if (!bothButtonsDown()) {
      const int volume = companionSinglePin == BTN_REC
        ? min(100, pikaPlaybackVolume() + 20)
        : max(20, pikaPlaybackVolume() - 20);
      pikaSetPlaybackVolume(volume);
      showVolumeSetting(volume);
      companionVolumeShownAt = millis();
    }
    companionSingleStartedAt = 0;
    companionSinglePin = -1;
  } else if (companionVolumeShownAt && millis() - companionVolumeShownAt >= 1800UL) {
    companionVolumeShownAt = 0;
    redrawCompanionHome();
  }

  companionUpperWasDown = upperDown;
  companionLowerWasDown = lowerDown;
}

void initializeDisplay() {
  custom_lcd_spi_t config = {};
  config.cs = EPD_CS_PIN;
  config.dc = EPD_DC_PIN;
  config.rst = EPD_RST_PIN;
  config.busy = EPD_BUSY_PIN;
  config.mosi = EPD_MOSI_PIN;
  config.scl = EPD_SCK_PIN;
  config.spi_host = EPD_SPI_NUM;
  config.buffer_len = (200 * 200) / 8;

  display = new epaper_driver_display(200, 200, config);
  // Do not visibly clear the panel here. The first actual UI frame establishes
  // the controller base image, avoiding two slow full refreshes during wake.
}

void initializeStorage(bool recoverOrphans=true) {
  Serial.println("[Storage] mount");SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  if (!SD_MMC.begin("/sdcard", true)) {
    showError("SD ERROR");
    while (true) delay(1000);
  }
  Serial.println("[Storage] brand migration");if(!pikaMigrateCard()){Serial.println("[Migration] SD migration failed");showError("MIGRATION ERROR");while(true)delay(1000);}
  Serial.println("[Storage] migration done");String recoveryError;
  if(!pikaFactoryRecoverData(recoveryError)){Serial.println("[Reset] "+recoveryError);showError("RESET ERROR");while(true)delay(1000);}
  if(!pikaRestoreRecover(recoveryError)){Serial.println("[Recovery] "+recoveryError);showError("RESTORE ERROR");while(true)delay(1000);}
  if (!SD_MMC.exists(NOTES_DIR)) SD_MMC.mkdir(NOTES_DIR);
  if(!pikaUndoRecover(recoveryError)){Serial.println("[Recovery] "+recoveryError);showError("UNDO ERROR");while(true)delay(1000);}
  // Only boot performs the comparatively expensive orphan-WAV recovery scan.
  // Normal reloads read the compact catalog, while search/vector indexes are
  // rebuilt lazily by the first operation that actually needs them.
#ifdef PIKA_PHONE_MODE
  // The checksummed sync catalog decides whether a recovery scan is needed.
  loadIndex(false);
#else
  loadIndex(recoverOrphans);
#endif
  Serial.printf("[SD] %d memories\n", static_cast<int>(noteIndex.size()));
}

}  // namespace

void pikaReleaseWifi(){stopWifi();}

void setup() {
  Serial.begin(115200);
  const bool rebootResume=pikaRecoveryResume();
  const bool usbResume=pikaUsbBackupResume()||rebootResume;
  Serial.setTxTimeoutMs(0); // A closed USB monitor must never delay wake/input.
  delay(40);
  Serial.println("\n=== Pika Note " FIRMWARE_VERSION " ===");

  releasePeripheralPowerHolds();
  board.VBAT_POWER_ON(); // Keep battery power latched before NVS/config initialization.
  pinMode(BTN_REC, INPUT_PULLUP);
  pinMode(BTN_PWR, INPUT_PULLUP);
  if(!pikaRecoveryButtonsBegin()) {
    Serial.println("RECOVERY_BUTTON_TASK_FAILED");
  }
  if(pikaBrandNeedsMigration()){SD_MMC.setPins(SD_CLK,SD_CMD,SD_D0);if(!SD_MMC.begin("/sdcard",true)){Serial.println("[Migration] SD unavailable");while(true)delay(1000);}
  if(!pikaMigrateSettings()){while(true){Serial.println("[Migration] Settings migration failed");delay(1000);}}
  if(!pikaFactoryPrepareBoot()){Serial.println("[Reset] Settings recovery failed");while(true)delay(1000);}
  }
  String settingsRecoveryError;
  if(!pikaOwnerRecoverSettings(settingsRecoveryError)){Serial.println("[Settings recovery] "+settingsRecoveryError);while(true)delay(1000);}
  pikaSettingsBegin();
  pikaExtrasBegin();
  pikaActivityLedBegin();
  pikaLocalConfigBegin();
  if(!pikaFactoryConfigSuppressed())importFlashSetup();
  batteryBegin();
  attachInterrupt(digitalPinToInterrupt(BTN_REC), latchUpperPress, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_PWR), latchLowerPress, FALLING);
  board.VBAT_POWER_ON();

  const bool wokeFromUltraSleep = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
  Serial.printf("[Wake] cause=%d\n", static_cast<int>(esp_sleep_get_wakeup_cause()));
  if (wokeFromUltraSleep) {
    pikaActivityLedWake();
    // EXT1 keeps the pin that caused wake-up even when a quick tap has already
    // been released by the time Arduino setup runs. Reading only digitalRead()
    // here lost exactly those quick lower-button taps.
    const uint64_t wakePins = esp_sleep_get_ext1_wakeup_status();
    Serial.printf("[Wake] ext1 pins=0x%llx source=%s\n",
                  static_cast<unsigned long long>(wakePins),
                  (wakePins & (1ULL << BTN_PWR)) ? "pwr" : "unknown");
  }

  resetActivity();
  powerOnPeripherals();
  initializeDisplay();
  pikaVibrationArmWake(esp_sleep_get_wakeup_cause()!=ESP_SLEEP_WAKEUP_TIMER);
  bool homeKeysStarted=false,cachedWakeHome=false;
  if((wokeFromUltraSleep||usbResume)&&pikaHasOpenAiKey()){
    homeKeysStarted=pikaHomeButtonsBegin();
    if(!homeKeysStarted){Serial.println("HOME_BUTTON_TASK_FAILED");while(true)delay(1000);}
    // Keep the inverted sleep frame until audio and storage can accept a hold.
  }

  const size_t psramBytes = ESP.getPsramSize();
  Serial.printf("[HW] Flash: %u MB, PSRAM: %u MB\n",
                static_cast<unsigned>(ESP.getFlashChipSize() / (1024 * 1024)),
                static_cast<unsigned>(psramBytes / (1024 * 1024)));
  if (!psramFound() || psramBytes < 8UL * 1024UL * 1024UL) {
    showError("8/8 ONLY");
    while (true) delay(1000);
  }

  heap_caps_malloc_extmem_enable(512); // Keep large String/JSON/network buffers out of scarce internal RAM.
  pikaAskStagesBegin(); // Reserve the 32KB stack before BLE/audio fragment internal RAM.
  Serial.printf("V7_BOOT phase=ble reset=%d heap=%u\n",int(esp_reset_reason()),ESP.getFreeHeap());
  if(!pikaFactoryPending())pikaPhoneBegin(); // Never advertise until a pending factory erase finishes.
  Serial.println("V7_BOOT phase=i2c");
  i2c_master_Init();
  rtcSyncSystemFromChip();
  delay(50);
  Serial.println("V7_BOOT phase=audio");
  audio_bsp_init();
  audio_play_init();
  Serial.println("V7_BOOT phase=storage");
  initializeStorage(!wokeFromUltraSleep&&!usbResume);
  const auto resetCause=esp_reset_reason();if(resetCause==ESP_RST_PANIC||resetCause==ESP_RST_INT_WDT||resetCause==ESP_RST_TASK_WDT||resetCause==ESP_RST_BROWNOUT)pikaError("device","boot","Abnormal previous reset",int(resetCause));
  pikaPhoneBegin();if(!pikaOwnerBound())pikaSetClockMode(false);setupTransferServer();
  Serial.printf("[BootDiagnostic] reset=%d heap=%u stack=%u wake=%d pins=0x%llx\n",int(esp_reset_reason()),ESP.getFreeHeap(),uxTaskGetStackHighWaterMark(nullptr),int(esp_sleep_get_wakeup_cause()),(unsigned long long)esp_sleep_get_ext1_wakeup_status());
  pikaWeatherBegin();
  pikaMailBegin();
  // A normal key wake restores the cached Home as soon as audio/storage are
  // ready. Catalog rebuilding belongs to the cooperative owner-loop job,
  // which yields to fresh Home gestures instead of delaying wake by ~10 s.
  if(pikaHasOpenAiKey()&&!wokeFromUltraSleep&&!usbResume){const uint32_t at=millis();pikaPhoneSyncTick();Serial.printf("PIKA_SYNC_BOOT ms=%lu\n",(unsigned long)(millis()-at));}

  setupRequired = !pikaHasOpenAiKey();
  if(!pikaOwnerBound()){state=STATE_IDLE;showPairingOnly();pikaActivityLedHomeReady();}
  else if (setupRequired) {
    state = STATE_IDLE;
    showSetupRequired();
    pikaActivityLedHomeReady();
  } else {
    if(pikaClockMode()&&esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_TIMER){state=STATE_CLOCK;showClockScreen();}
    else {
      pikaSetClockMode(false);state=STATE_IDLE;
      if((wokeFromUltraSleep||usbResume)&&pikaOwnerBound()){if(wokeFromUltraSleep||rebootResume)pikaPlayNotify(5);clearLatchedPresses();pikaHomeButtonsEnable(true);cachedWakeHome=showCachedWakeHome(usbResume);}
      if(cachedWakeHome){displayedHomeDay=cachedWakeHomeDay();nextHomeClockCheckAt=millis();Serial.printf("PIKA_WAKE_SCREEN ready boot_ms=%lu\n",(unsigned long)millis());}
      else renderReadyHome();
    }
  }
  if(esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_TIMER){
    wakeGoogleSyncPending=false;handleEventReminder();
    if(state!=STATE_CLOCK)enterUltraSleep();
  }

  // Waking is deliberately a separate interaction: PWR only wakes to Home.
  // Ignore the held wake press until both buttons are released so it cannot
  // accidentally open Appointments or start recording during boot.
  if (wokeFromUltraSleep && state == STATE_IDLE) idleInputArmed = false;
  // The wake press itself only wakes to Home. New presses from this point on
  // are retained even if they happen during a display update.
  clearLatchedPresses();
  if(!homeKeysStarted&&!pikaHomeButtonsBegin()){Serial.println("HOME_BUTTON_TASK_FAILED");while(true)delay(1000);}
  if(state==STATE_IDLE&&pikaOwnerBound())pikaHomeButtonsEnable(true);
  Serial.printf("PIKA_HOME_READY boot_ms=%lu wake=%d\n",(unsigned long)millis(),int(esp_sleep_get_wakeup_cause()));
}

void traceHomeWork(const char* name,void(*work)()){
 const uint32_t at=millis();work();const uint32_t elapsed=millis()-at;
 if(elapsed>=100)Serial.printf("PIKA_HOME_WORK name=%s ms=%lu\n",name,(unsigned long)elapsed);
}
void loop() {
  if(usbKeepAwakeUntil&&(digitalRead(BTN_REC)==LOW||digitalRead(BTN_PWR)==LOW)){usbKeepAwakeUntil=0;Serial.println("PIKA_TEST_HOLD_RELEASED_BY_BUTTON");}
  if(usbKeepAwakeUntil&&int32_t(usbKeepAwakeUntil-millis())>0)resetActivity();
  if(pikaRecoveryRestartRequested()){pikaHomeButtonsEnable(false);pikaPhoneForeground(true);showRebooting();delay(1000);showRebootSleep();delay(350);pikaRecoveryMarkResume();ESP.restart();return;}
  pikaRtcServicePhoneTime();
  if(pikaOwnerRestartAt){if(int32_t(millis()-pikaOwnerRestartAt)>=0)ESP.restart();pikaPhoneService();delay(2);return;}
  if(!pikaOwnerBound()){pikaHomeButtonsEnable(false);pikaPhoneService();handleUsbSetup();delay(5);return;}
  if(pikaUsbBackupDue()){pikaPhoneForeground(true);stopTransferMode();stopWifi();audio_idle_suspend();pikaUsbBackupRun();return;}
  if(pikaUsbBackupPending()){pikaPhoneService();handleUsbSetup();delay(1);return;}
  static uint32_t nextBootTrace=0;if(millis()<25000&&millis()>=nextBootTrace){nextBootTrace=millis()+500;pikaHomeButtonsPrintTrace();}
  // A reader screen's Back is handled before any SD scan, RPC or hash work.
  if(state==STATE_CALENDAR||state==STATE_WEATHER){handleExtraScreen();if(state==STATE_IDLE){delay(1);return;}}
  if(state==STATE_MAIL){handleMailScreen();if(state==STATE_IDLE){delay(1);return;}}

  if(state==STATE_IDLE){handleIdle();if(state==STATE_IDLE&&pikaHomeButtonsBusy()){resetActivity();pikaPhoneService();delay(1);return;}}
  // Panel refresh must never hold the owner loop away from a fresh microphone hold.
  if(state==STATE_IDLE&&pikaHomeRefreshBusy()){pikaPhoneService();delay(1);return;}
  if(state==STATE_IDLE&&!pikaHomeButtonsForegroundGrace()&&!audioPlaying&&!pikaHomeButtonsBusy()&&bothButtonsUp())traceHomeWork("catalog",[](){pikaPhoneSyncTick();});
  pikaPhoneService();
  if(state==STATE_IDLE&&!audioPlaying&&!pikaHomeButtonsBusy()&&bothButtonsUp())pikaServiceSoundTest();
  if((state!=STATE_IDLE||!pikaHomeButtonsForegroundGrace())&&state!=STATE_RECORDING&&state!=STATE_ASKING&&!audioPlaying&&!pikaHomeButtonsBusy()&&bothButtonsUp())traceHomeWork("rpc",[](){pikaPhoneRpcService();});
  if(state==STATE_IDLE&&!pikaHomeButtonsForegroundGrace()&&!pikaHomeButtonsBusy()&&bothButtonsUp()&&pikaPhoneHomeDirty())traceHomeWork("home",[](){refreshPhoneHome();});
  if((state!=STATE_IDLE||!pikaHomeButtonsForegroundGrace())&&state!=STATE_RECORDING&&state!=STATE_ASKING&&!audioPlaying&&!pikaHomeButtonsBusy()&&bothButtonsUp())pikaPhoneHashTick();
  serviceWifiLease();pikaDeferredHttpReap();
  handleUsbSetup();
  if(state==STATE_IDLE&&!pikaHomeButtonsBusy()&&!pikaHomeRefreshBusy())pikaErrorService();
  if(usbPhonePauseRequested&&state==STATE_IDLE){usbPhonePauseRequested=false;pikaPhoneHashCancel();pikaPhoneForeground(true);state=STATE_ASKING;usbPhonePauseUntil=millis()+2500;Serial.println("PIKA_PHONE_PAUSE_STARTED");}
  if(usbPhonePauseUntil&&int32_t(millis()-usbPhonePauseUntil)>=0){usbPhonePauseUntil=0;state=STATE_IDLE;pikaPhoneForeground(false);Serial.println("PIKA_PHONE_PAUSE_ENDED");}
  if(usbVoiceBench){const bool fast=usbVoiceBench==2;usbVoiceBench=0;if(state!=STATE_IDLE||!pikaPhoneOnline()){Serial.println("PIKA_VOICE_BENCH_OFFLINE");return;}pikaPhoneHashCancel();pikaHomeButtonsEnable(false);pikaPhoneForeground(true);state=STATE_ASKING;resetActivity();showAskProgress("TESTING",nullptr);pikaPhoneUseWindowUpload(fast);File sample=SD_MMC.open(NOTES_DIR "/.ask.wav");const size_t bytes=sample?sample.size():0;if(sample)sample.close();const uint32_t at=millis();String transcript;const bool ok=bytes>44&&connectConfiguredWifi(8,false)&&transcribeQuestion(NOTES_DIR "/.ask.wav",transcript);Serial.printf("PIKA_VOICE_BENCH mode=%s bytes=%u ok=%d total_ms=%lu text_bytes=%u\n",fast?"window":"legacy",unsigned(bytes),ok,(unsigned long)(millis()-at),unsigned(transcript.length()));pikaPhoneUseWindowUpload(true);returnToReady();return;}
  if(usbReminderTest&&state==STATE_IDLE){usbReminderTest=false;BrainTaskRecord event;event.text="Reminder sound test";event.dueUtc=currentUtcIso();runEventReminder(event,true);Serial.println("PIKA_REMINDER_TEST_DONE");return;}
  if(usbPhoneSpeechRequested){usbPhoneSpeechRequested=false;pikaPhoneHashCancel();pikaPhoneForeground(true);state=STATE_ASKING;pikaPhonePublishLiveSnapshot();bool ok=playBrainSpeech("Voice playback test. One two three. This sentence should play smoothly without interruptions.",NOTES_DIR "/.speech-test.wav",0);Serial.printf("PIKA_PHONE_SPEECH ok=%d\n",ok);returnToReady();}
  if(usbPhoneTlsRequested){usbPhoneTlsRequested=false;if(state==STATE_IDLE&&pikaPhoneOnline()){PhoneSecureClient client;HTTPClient http;int status=-1;int bytes=0;if(http.begin(client,"https://api.open-meteo.com/v1/forecast?latitude=50.45&longitude=30.52&current=temperature_2m")){http.setConnectTimeout(20000);http.setTimeout(30000);status=http.GET();if(status==200)bytes=http.getString().length();http.end();}Serial.printf("PIKA_PHONE_TLS status=%d bytes=%d wifi=%d\n",status,bytes,int(WiFi.status()));resetActivity();}else Serial.println("PIKA_PHONE_TLS_OFFLINE");}
  if(usbStatusRequested){
    usbStatusRequested=false;
    if(state==STATE_IDLE){ManagerReadSnapshot snapshot;int memories=0,tasks=0,events=0;
      for(const auto& note:noteIndex)if(!managerArchived(note.num))++memories;
      const String today=managerUtcToLocalInput(currentUtcIso(),true);
      for(const auto& task:loadBrainTasks()){if(task.done||task.archived)continue;
        if(task.type=="event"){const String day=managerUtcToLocalInput(task.endUtc.length()?task.endUtc:task.dueUtc,true);if(day.length()==10&&day>=today)++events;}
        else ++tasks;
      }
      Serial.printf("PIKA_STATUS version=%s wifi=%d memories=%d tasks=%d events=%d heap=%u stack=%u\n",FIRMWARE_VERSION,int(WiFi.status()),memories,tasks,events,ESP.getFreeHeap(),uxTaskGetStackHighWaterMark(nullptr));resetActivity();
    }else Serial.println("PIKA_STATUS_BUSY");
  }
  if(usbWifiPerfRequested){
    usbWifiPerfRequested=false;if(state!=STATE_IDLE||!bothButtonsUp()){Serial.println("PIKA_WIFI_PERF_BUSY");return;}
    pikaHomeButtonsEnable(false);state=STATE_ASKING;resetActivity();PikaBusyLight busy;
    const bool reused=WiFi.status()==WL_CONNECTED;const uint32_t started=millis();const bool ok=connectConfiguredWifi(8,false);
    Serial.printf("PIKA_WIFI_PERF connected=%d reused=%d elapsed_ms=%lu rssi=%d\n",ok,reused,(unsigned long)(millis()-started),ok?WiFi.RSSI():0);
    stopWifi();returnToReady();return;
  }
  if(usbAskRenewRequested){usbAskRenewRequested=false;if(state==STATE_RECORDING||state==STATE_ASKING){Serial.println("PIKA_ASK_RENEW_BUSY");return;}Serial.println(pikaAskRenew()?"PIKA_ASK_RENEW_DONE":"PIKA_ASK_RENEW_FAILED");resetActivity();}
  if(usbFeedbackTestRequested){
    usbFeedbackTestRequested=false;
    if(state==STATE_RECORDING||state==STATE_ASKING||!bothButtonsUp())return;
    if(state==STATE_TRANSFER)leaveCompanion();
    pikaHomeButtonsReset();state=STATE_ASKING;resetActivity();PikaBusyLight busy;
    if(connectConfiguredWifi(8,false)){
      const bool ran=pikaRunAskStage([](void*){showAskActionProgress("SYNCING","CALENDAR",0,1);pikaGoogleResetSession();pikaGoogleRequestVerify();pikaGoogleSyncBatch();showAskActionProgress("SYNCING","CALENDAR",1,1);},nullptr);
      const bool speech=ran&&playBrainSpeech("Проверка голосового ответа. Записи не изменялись.",NOTES_DIR "/.feedback-test.wav",0);
      Serial.printf("[FeedbackTest] speech=%d calendar=%s stackFree=%u\n",speech,pikaGoogleStatus().c_str(),uxTaskGetStackHighWaterMark(nullptr));
    }
    Serial.println("PIKA_FEEDBACK_TEST_DONE");Serial.flush();returnToReady();return;
  }
  if(usbAskTestQuestion.length()){
    if(state==STATE_RECORDING||state==STATE_ASKING||!bothButtonsUp()){Serial.println("PIKA_ASK_TEST_BUSY");usbAskTestQuestion="";return;}
    if(state==STATE_TRANSFER)leaveCompanion();
    const String question=usbAskTestQuestion;usbAskTestQuestion="";pikaHomeButtonsEnable(false);pikaPhoneForeground(true);state=STATE_ASKING;resetActivity();
    PikaBusyLight busy;showAskProgress("TESTING", "ASK");
    if(connectConfiguredWifi(8,false)){
      loadIndex();pikaSetVoiceMutationContext(true);pikaAskSetDryRun(!question.startsWith("EXECUTE: "));String answer;std::vector<int> sources;
      bool handled=false;
      if(question=="REPLAY_LAST_RECORDING"){
        String replay;const bool decoded=transcribeQuestion(NOTES_DIR "/.ask.wav",replay);
        Serial.printf("[AskTest] replay decoded=%d textBytes=%u\n",decoded,unsigned(replay.length()));
        if(decoded)handled=pikaUnifiedAsk(replay,answer,sources,false);
      }else if(question.startsWith("EXECUTE: ")){handled=pikaUnifiedAsk(question.substring(9),answer,sources,true);if(handled)pikaRememberAskTurn(question.substring(9),answer);}
      else handled=question.startsWith("DIALOGUE: ")?pikaUnifiedAsk(question.substring(10),answer,sources,false):applyAskAssistant(question,answer,sources);
      pikaAskSetDryRun(false);pikaSetVoiceMutationContext(false);
      Serial.printf("[AskTest] handled=%d answer=%s\n",handled,answer.c_str());
    }else Serial.println("[AskTest] no WiFi");
    Serial.println("PIKA_ASK_TEST_DONE");Serial.flush();returnToReady();return;
  }
  if(usbCalendarRequested){
    usbCalendarRequested=false;
    if(state==STATE_RECORDING||state==STATE_ASKING||!bothButtonsUp()){Serial.println("PIKA_CALENDAR_BUSY");return;}
    if(state==STATE_TRANSFER)leaveCompanion();
    pikaSetClockMode(false);armExtraScreen();state=STATE_CALENDAR;resetActivity();showMonthCalendar();
    Serial.println("PIKA_CALENDAR_OPEN");Serial.flush();return;
  }
  if(usbWeatherRequested){
    usbWeatherRequested=false;
    if(state==STATE_RECORDING||state==STATE_ASKING||!bothButtonsUp()){Serial.println("PIKA_WEATHER_BUSY");return;}
    if(state==STATE_TRANSFER)leaveCompanion();
    pikaSetClockMode(false);clearLatchedPresses();
    openWeatherScreen();Serial.println("PIKA_WEATHER_OPEN");Serial.flush();return;
  }
  if(usbSleepRequested){
    usbSleepRequested=false;
    if(state!=STATE_IDLE&&state!=STATE_TRANSFER&&state!=STATE_CLOCK&&state!=STATE_CALENDAR&&state!=STATE_WEATHER&&state!=STATE_MAIL){Serial.println("PIKA_SLEEP_BUSY");return;}
    if(digitalRead(BTN_REC)==LOW||digitalRead(BTN_PWR)==LOW){Serial.println("PIKA_SLEEP_BUTTON_HELD");return;}
    if(state==STATE_TRANSFER)leaveCompanion();
    else if(state!=STATE_IDLE){pikaSetClockMode(false);returnToReady();}
    pikaSetClockMode(false);
    Serial.println("PIKA_SLEEP_START");Serial.flush();
    enterUltraSleep();return;
  }

  // Companion must also be serviced on a device that still needs setup.
  if (state == STATE_TRANSFER) {
    handleCompanion();
    delay(5);
    return;
  }

  // A freshly flashed device waits transparently for the local USB setup.
  // Do not start Wi-Fi Companion automatically: opening WebSerial resets the
  // ESP32 and the browser must still be able to deliver the setup command.
  if(setupRequired&&pikaHasOpenAiKey()){setupRequired=false;returnToReady();}
  if (setupRequired) {
    if (bothButtonsDown()) {
      if (!setupCompanionHoldStartedAt) setupCompanionHoldStartedAt = millis();
      else if (millis() - setupCompanionHoldStartedAt >= COMPANION_HOLD_MS) {
        setupCompanionHoldStartedAt = 0;
        enterCompanion();
      }
    } else {
      setupCompanionHoldStartedAt = 0;
    }
    delay(5);
    return;
  }

  static uint32_t nextReminderCheck=0;
  if((state==STATE_IDLE||state==STATE_AGENDA||state==STATE_CLOCK||state==STATE_CALENDAR||state==STATE_WEATHER)&&!(state==STATE_IDLE&&(pikaHomeButtonsForegroundGrace()||pikaHomeButtonsBusy()))&&static_cast<int32_t>(millis()-nextReminderCheck)>=0){nextReminderCheck=millis()+1000;handleEventReminder();}
  if(state==STATE_CALENDAR||state==STATE_WEATHER){handleExtraScreen();delay(8);return;}
  if(state==STATE_MAIL){handleMailScreen();delay(8);return;}
  if(state==STATE_CLOCK){handleClockMode();delay(8);return;}
  if (state == STATE_AGENDA) {
    handleAgenda();
    delay(8);
    return;
  }

  if (state == STATE_IDLE) {
    if(state==STATE_IDLE&&pikaHomeButtonsBusy()){resetActivity();delay(2);return;}
    if (state == STATE_IDLE&&!pikaHomeButtonsForegroundGrace()) traceHomeWork("wake-sync",[](){handleWakeGoogleSync();});
    if(state==STATE_IDLE&&pikaHomeButtonsBusy()){resetActivity();delay(2);return;}
    if (state == STATE_IDLE&&!pikaHomeButtonsForegroundGrace()) traceHomeWork("pending-memory",[](){handleBackgroundBrain();});
    if (state == STATE_IDLE &&
        static_cast<int32_t>(millis() - nextHomeClockCheckAt) >= 0) {
      nextHomeClockCheckAt = millis() + 60000UL;
      const String day = localDayKey();
      if (day.length() && day != displayedHomeDay) renderReadyHome();
    }
    if (state == STATE_IDLE &&
        idleInputArmed && bothButtonsUp() &&
        (!lastUpperTapAt || millis() - lastUpperTapAt > ASK_DOUBLE_TAP_MS) &&
        (!lastLowerTapAt || millis() - lastLowerTapAt > ASK_DOUBLE_TAP_MS)) {
      const uint32_t idleFor = millis() - lastActivityMs;
      if (idleFor >= pikaSleepIdleMs()) {
        // Pending derived work stays queued. Only an active data-transfer
        // lease may postpone the configured inactivity deadline.
        enterUltraSleep();
      }
    }
  }
  delay(8);
}
