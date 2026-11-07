#include "device_owner.h"
#include "offline_ask.h"
#include "home_buttons.h"
#include "mail.h"
#include "usb_backup.h"
#pragma once
#include "activity_led.h"
#include "../../build_info.generated.h"
#include "manager_store.h"
#include "google_calendar.h"
#include "manager_assets.h"
#include "device_extras.h"
#include "weather.h"
#include "button_actions.h"

static uint32_t managerLastRequest=0;
static void managerDeviceJson(JsonObject device){device["serial"]=pikaDeviceSerial();
  static BatteryReading battery;static uint32_t sampled=0;static bool ready=false;static float sampledFactor=0;
  // f_getfree can scan the entire FAT. Preserve telemetry across deep sleep;
  // refresh it at a cold boot, never on a Home sync/status request after wake.
  static RTC_DATA_ATTR uint32_t storageMagic=0;
  static RTC_DATA_ATTR uint64_t total=0,used=0;
  if(storageMagic!=0x53444348||!total||used>total){total=SD_MMC.totalBytes();used=SD_MMC.usedBytes();if(total)storageMagic=0x53444348;}
  if(!ready||millis()-sampled>=30000UL||sampledFactor!=pikaBatteryCalibration()){battery=readBattery();sampled=millis();sampledFactor=pikaBatteryCalibration();ready=true;}
  device["batteryValid"]=battery.valid;
  if(battery.valid){device["batteryPercent"]=battery.percent;device["batteryMillivolts"]=battery.millivolts;}
  device["storageTotal"]=total;device["storageUsed"]=used;device["storageFree"]=total>=used?total-used:0;
  device["memories"]=noteIndex.size();
}
static void handleManagerState(){
  JsonDocument doc;doc["restoreActive"]=pikaRestoreBusy();pikaUndoStatus(doc["undo"].to<JsonObject>());doc["revision"]=managerRevision();doc["googleStatus"]=pikaGoogleStatus();managerDeviceJson(doc["device"].to<JsonObject>());
  String out;serializeJson(doc,out);transferServer.sendHeader("Cache-Control","no-store");transferServer.send(200,"application/json",out);
}
static String managerPriorityName(int rank){return rank>=3?"urgent":rank==2?"high":rank<=0?"low":"normal";}
static void managerJsonError(int code,const String& message){JsonDocument doc;doc["error"]=message;String out;serializeJson(doc,out);transferServer.sendHeader("Cache-Control","no-store");transferServer.send(code,"application/json; charset=utf-8",out);}
static String managerDefaultEnd(const String& local){
  bool valid=false;String utc=managerLocalToUtc(local,false,&valid);if(!valid||!utc.length())return "";
  struct tm time={};sscanf(utc.c_str(),"%d-%d-%dT%d:%d",&time.tm_year,&time.tm_mon,&time.tm_mday,&time.tm_hour,&time.tm_min);time.tm_year-=1900;time.tm_mon--;time_t epoch=utcTmToEpoch(time)+3600;struct tm result={};gmtime_r(&epoch,&result);char out[25];strftime(out,sizeof(out),"%Y-%m-%dT%H:%M:%SZ",&result);return managerUtcToLocalInput(out);
}
static void managerSettingsJson(JsonObject settings){
  pikaWeatherSettingsJson(settings);
  pikaMailSettingsJson(settings);
  settings["sleepMinutes"]=pikaSleepMinutes();settings["batteryCalibration"]=pikaBatteryCalibration();
  settings["activityLed"]=pikaActivityLedEnabled();
  settings["buildCommit"]=PIKA_GIT_COMMIT;settings["firmwareVersion"]=FIRMWARE_VERSION;
  settings["vibrationEnabled"]=pikaVibrationEnabled();{auto values=settings["vibrationPatterns"].to<JsonArray>();for(int i=0;i<6;i++)values.add(pikaVibrationPattern(i));}
  settings["notify"]=pikaNotifyEnabled();{auto values=settings["notifyMelodies"].to<JsonArray>();for(int i=0;i<7;i++)values.add(pikaNotifyMelody(i));}settings["alertMelody"]=pikaAlertMelody();settings["volume"]=pikaPlaybackVolume();
  JsonArray buttons=settings["buttonActions"].to<JsonArray>();for(int i=0;i<PIKA_GESTURES;i++)buttons.add(pikaButtonAction(i));
  settings["voiceRecordActions"]=pikaVoiceRecordActions();
  settings["strictCapture"]=pikaStrictCapture();settings["alerts"]=pikaAlertEnabled();settings["alertLead"]=pikaAlertLeadMinutes();settings["alertDuration"]=pikaAlertDurationSeconds();
  settings["openWifi"]=pikaOpenWifiFallback();settings["apiConfigured"]=pikaHasOpenAiKey();settings["voice"]=pikaSpeechVoice();
  settings["googleConfigured"]=pikaGoogleServiceJson().length()>0;settings["googleEnabled"]=pikaGoogleSyncEnabled();settings["googleStatus"]=pikaGoogleStatus();settings["calendarId"]=pikaGoogleCalendarId();
  JsonArray networks=settings["networks"].to<JsonArray>();
  for(int i=0;i<pikaWifiProfileCount();i++){const auto profile=pikaWifiProfile(i);JsonObject n=networks.add<JsonObject>();n["ssid"]=profile.ssid;n["passwordSaved"]=profile.password.length()>0;n["connected"]=WiFi.status()==WL_CONNECTED&&WiFi.SSID()==profile.ssid;}
}
static void managerTaskJson(JsonObject item,const BrainTaskRecord& task){
    item["id"]=task.id;item["text"]=task.text;item["date"]=managerUtcToLocalInput(task.dueUtc,task.datePrecision=="day"&&task.type=="event");
    item["end"]=task.endUtc.length()?managerUtcToLocalInput(task.endUtc,task.datePrecision=="day"):task.datePrecision=="day"?managerUtcToLocalInput(task.dueUtc,true):managerDefaultEnd(item["date"].as<String>());
    item["priority"]=managerPriorityName(task.priority);item["allDay"]=task.datePrecision=="day"&&task.type=="event";item["done"]=task.done;item["archived"]=task.archived;
    if(task.noteNum>0&&managerNoteExists(task.noteNum))item["note"]="m"+String(task.noteNum);else item["note"]=nullptr;
}
static void managerMemoryJson(JsonObject memory,int num){
 const String captured=managerUtcToLocalInput(noteCreatedUtc(num));const bool archived=managerArchived(num);
    const BrainMemory brain=loadBrainMemory(num);
    memory["id"]="m"+String(num);memory["date"]=captured;memory["archived"]=archived;memory["text"]=managerReadText(num,600);
    if(brain.valid){
      memory["summary"]=brain.summary;memory["language"]=brain.language;memory["kind"]=brain.kind;
      {JsonArray values=memory["topics"].to<JsonArray>();for(const auto& value:brain.topics)values.add(value);}
      {JsonArray values=memory["people"].to<JsonArray>();for(const auto& value:brain.people)values.add(value);}
      {JsonArray values=memory["projects"].to<JsonArray>();for(const auto& value:brain.projects)values.add(value);}
      {JsonArray values=memory["decisions"].to<JsonArray>();for(const auto& value:brain.decisions)values.add(value);}
      {JsonArray values=memory["facts"].to<JsonArray>();for(const auto& value:brain.facts)values.add(value);}
      {JsonArray values=memory["questions"].to<JsonArray>();for(const auto& value:brain.questions)values.add(value);}
    }
    String title=brain.valid?brain.title:memory["text"].as<String>();title.replace("\n"," ");
    if(title.length()>90)title=title.substring(0,pikaUtf8Prefix(title.c_str(),90))+"...";
    memory["title"]=title.length()?title:"Memory #"+String(num);
    char path[64];snprintf(path,sizeof(path),"%s/note_%03d.wav",NOTES_DIR,num);memory["wav"]=SD_MMC.exists(path);
    snprintf(path,sizeof(path),"%s/note_%03d.txt",NOTES_DIR,num);File file=SD_MMC.open(path);memory["truncated"]=file&&file.size()>memory["text"].as<String>().length();if(file)file.close();
}
#include "manager_search.h"

static void handleManagerData(){
 uint32_t snapshotStageAt=millis();auto snapshotTrace=[&](const char* name){uint32_t now=millis();if(now-snapshotStageAt>100)Serial.printf("PIKA_SNAPSHOT_STAGE name=%s ms=%lu\n",name,(unsigned long)(now-snapshotStageAt));snapshotStageAt=now;};
 const bool cooperative=transferServer.arg("yield")=="1";
 auto inputPending=[&](){if(cooperative&&pikaHomeButtonsBusy()){transferServer.send(409,"application/json","{\"error\":\"Pika input pending\"}");return true;}return false;};
 if(inputPending())return;
 ManagerReadSnapshot readSnapshotScope;
  managerLastRequest=millis();loadIndex();
  snapshotTrace("index");PIKA_JSON_DOCUMENT(doc);doc["revision"]=managerRevision();doc["totalMemories"]=noteIndex.size();
  doc["restoreActive"]=pikaRestoreBusy();pikaUndoStatus(doc["undo"].to<JsonObject>());
  if(inputPending())return;
  managerSettingsJson(doc["settings"].to<JsonObject>());
  if(inputPending())return;
  managerDeviceJson(doc["device"].to<JsonObject>());
  JsonArray todos=doc["tasks"].to<JsonArray>(),events=doc["events"].to<JsonArray>();
  snapshotTrace("metadata");const auto taskSnapshot=transferServer.arg("yield")=="1"?loadBrainTasksCooperative():loadBrainTasks();
  if(!brainTaskReadComplete()){managerJsonError(503,"Task storage read failed; keeping the previous library");return;}
  for(const auto& task:taskSnapshot){
    if(inputPending())return;
    JsonObject item=(task.type=="event"?events:todos).add<JsonObject>();
    managerTaskJson(item,task);
  }
  snapshotTrace("tasks");const int offset=max(0L,transferServer.arg("offset").toInt());
  const String requestedDay=transferServer.arg("day");
  JsonArray memories=doc["memories"].to<JsonArray>();JsonObject days=doc["memoryDays"].to<JsonObject>();JsonObject archiveDays=doc["archivedMemoryDays"].to<JsonObject>();
  int visible=0,index=0,activeMemories=0;
  std::vector<int> orderedNotes;
  for(bool archivedPass:{false,true})for(auto it=noteIndex.rbegin();it!=noteIndex.rend();++it){if(inputPending())return;if(managerArchived(it->num)==archivedPass)orderedNotes.push_back(it->num);}
  snapshotTrace("order");for(const int num:orderedNotes){
    if(transferServer.arg("yield")=="1"&&pikaHomeButtonsBusy()){transferServer.send(409,"application/json","{\"error\":\"Pika input pending\"}");return;}
    const String captured=managerUtcToLocalInput(noteCreatedUtc(num));const String day=captured.substring(0,10);
    const bool archived=managerArchived(num);if(!archived)++activeMemories;
    if(day.length()==10){JsonObject counts=archived?archiveDays:days;counts[day]=(counts[day]|0)+1;}
    const String requestedMemory=transferServer.arg("memory");
    const bool include=requestedMemory.length()?requestedMemory=="m"+String(num):requestedDay.length()?day==requestedDay:(index>=offset&&visible<24);
    index++;if(!include)continue;
    if(visible>=100){doc["dayTruncated"]=true;continue;}visible++;
    if(inputPending())return;
    managerMemoryJson(memories.add<JsonObject>(),num);
  }
  snapshotTrace("memories");doc["activeMemories"]=activeMemories;
  doc["nextOffset"]=offset+visible;
  if(doc.overflowed()){managerJsonError(503,"Library snapshot allocation failed; keeping the previous library");return;}
  if(inputPending())return;
  String out;const size_t serialized=serializeJson(doc,out);if(serialized!=measureJson(doc)||out.length()!=serialized){managerJsonError(503,"Library serialization failed; keeping the previous library");return;}transferServer.sendHeader("Cache-Control","no-store");transferServer.send(200,"application/json; charset=utf-8",out);
}
static void handleManagerMutation(){
  managerLastRequest=millis();const String body=transferServer.arg("plain");if(body.length()>45000){managerJsonError(413,"Request is too large");return;}
  JsonDocument request;if(deserializeJson(request,body)){managerJsonError(400,"Invalid JSON");return;}
  loadIndex();const uint32_t before=managerRevision();String id,error;
  if(!managerMutation(request.as<JsonVariantConst>(),id,error)){if(managerRevision()!=before){loadIndex();invalidatePortalCache();}managerJsonError(error.startsWith("Data changed")?409:400,error.length()?error:"Storage error; changes could not be saved");return;}
  invalidatePortalCache();pikaGoogleScheduleSync();
  JsonDocument response;response["id"]=id;response["revision"]=managerRevision();response["saved"]=true;String out;serializeJson(response,out);transferServer.sendHeader("Cache-Control","no-store");transferServer.send(200,"application/json",out);
}
static void handleManagerUndo(){
  if(pikaRestoreBusy()){managerJsonError(409,"Restore in progress");return;}
  JsonDocument request;if(deserializeJson(request,transferServer.arg("plain"))){managerJsonError(400,"Invalid JSON");return;}
  if(!request["revision"].is<uint32_t>()||request["revision"].as<uint32_t>()!=managerRevision()){if(!pikaUndoAlreadyApplied(request["clientRequest"]|"")){managerJsonError(409,"Data changed; reload before undo");return;}}
  String error;if(!pikaUndoApply(request["clientRequest"]|"",error)){managerJsonError(409,error);return;}
  loadIndex();invalidatePortalCache();pikaGoogleScheduleSync();
  JsonDocument doc;doc["saved"]=true;doc["revision"]=managerRevision();String body;serializeJson(doc,body);transferServer.send(200,"application/json",body);
}
static void handleManagerSettings(){
  managerLastRequest=millis();if(pikaRestoreBusy()){managerJsonError(409,"Restore in progress; wait or cancel it first");return;}const String body=transferServer.arg("plain");if(body.length()>12000){managerJsonError(413,"Settings are too large");return;}
  JsonDocument request;if(deserializeJson(request,body)){managerJsonError(400,"Invalid JSON");return;}
  const String action=request["action"]|"";bool ok=false;
  if(action=="factoryReset"){
    if(!(request["confirmed"]|false)||String(request["confirmation"]|"")!="RESET"||!request["revision"].is<uint32_t>()||request["revision"].as<uint32_t>()!=managerRevision()){managerJsonError(409,"Confirm RESET with the latest data revision");return;}
    String error;if(!pikaFactoryRequest(request["newPin"]|"",error)){managerJsonError(400,error);return;}
    transferServer.sendHeader("Cache-Control","no-store");transferServer.send(200,"application/json","{\"saved\":true,\"restarting\":true}");pikaOwnerRestartAt=millis()+1500;return;
  }
  else if(action=="changePin"){
    if(String(request["newPin"]|"")!=String(request["repeatPin"]|"")){managerJsonError(400,"New PINs do not match");return;}
    String error;if(!pikaWebChangePin(request["currentPin"]|"",request["newPin"]|"",error)){managerJsonError(400,error);return;}ok=true;
  }
  else if(action=="offlineAskAck")ok=pikaAckOfflineAsk(request["name"]|"");
  else if(action=="forgetOwner"){String error;if(!(request["confirmed"]|false)||!pikaOwnerForget(request["serial"]|"",request["receipt"]|"",error)){managerJsonError(400,error.length()?error:"Confirm Forget Pika");return;}transferServer.send(200,"application/json","{\"saved\":true,\"resetScheduled\":true}");pikaOwnerRestartAt=millis()+1500;return;}
  else if(action=="askAppend")ok=pikaAskImportTurn(request["id"]|"",request["question"]|"",request["answer"]|"",request["createdUtc"]|"");
  else if(action=="askRenew")ok=pikaAskRenew();
  else if(action=="usbMount")ok=pikaUsbBackupRequest();
  else if(action=="batteryCalibrate"){ok=(request["confirmed"]|false)&&batteryCalibrateFull(request["reset"]|false);}
  else if(action=="sleep"){ok=request["minutes"].is<int>()&&pikaSaveSleepMinutes(request["minutes"].as<int>());if(ok)resetActivity();}
  else if(action=="activityLed")ok=request["enabled"].is<bool>()&&pikaActivityLedSave(request["enabled"].as<bool>());
  else if(action=="mail")ok=pikaMailSave(request.as<JsonObjectConst>());
  else if(action=="mailDisconnect")ok=pikaMailDisconnect();
  else if(action=="weather")ok=pikaWeatherSave(request["locations"].as<JsonArrayConst>());
  else if(action=="voiceActions")ok=request["enabled"].is<bool>()&&pikaSaveVoiceRecordActions(request["enabled"].as<bool>());
  else if(action=="capture")ok=request["strict"].is<bool>()&&pikaSaveStrictCapture(request["strict"].as<bool>());
  else if(action=="vibrationTest"){ok=request["pattern"].is<int>()&&pikaVibrationTest(request["pattern"]);}
  else if(action=="vibration"){auto values=request["patterns"].as<JsonArray>();int selected[6];ok=request["enabled"].is<bool>()&&values.size()==6;for(int i=0;ok&&i<6;i++){ok=values[i].is<int>();selected[i]=values[i]|-1;}if(ok)ok=pikaSaveVibration(request["enabled"],selected);}
  else if(action=="sound"){ok=pikaSaveSoundSettings(request["melody"]|-1,request["volume"]|0);if(ok&&request["notify"].is<bool>())ok=pikaSaveNotify(request["notify"]);if(ok&&!request["notifyMelodies"].isNull()){auto values=request["notifyMelodies"].as<JsonArray>();int selected[7];for(int i=0;i<7;i++)selected[i]=pikaNotifyMelody(i);ok=values.size()==5||values.size()==7;for(int i=0;ok&&i<int(values.size());i++){ok=values[i].is<int>();selected[i]=values[i]|-2;}if(ok)ok=pikaSaveNotifyMelodies(selected);}}
  else if(action=="soundTest"){
    int melody=request["melody"]|-1,volume=request["volume"]|0;
    ok=melody>=-1&&melody<13&&volume>=20&&volume<=100;
    if(ok)pikaQueueSoundTest(melody,volume);
  }
  else if(action=="buttons"){
    String actions[PIKA_GESTURES];JsonArray values=request["actions"].as<JsonArray>();
    if(values.size()==PIKA_GESTURES){for(int i=0;i<PIKA_GESTURES;i++)actions[i]=values[i]|"";ok=pikaSaveButtonActions(actions);}
  }
  else if(action=="alerts")ok=pikaSaveAlertSettings(request["enabled"]|true,request["lead"]|30,request["duration"]|60);
  else if(action=="fallback"||action=="wifi"||action=="wifiRemove"){managerJsonError(400,"This version uses the phone internet over Bluetooth; Wi-Fi settings are not used");return;}
  else if(action=="ai")ok=pikaSaveSettings("","",request["apiKey"]|"",request["voice"]|"cedar");
  else if(action=="google"){
    ok=pikaSaveGoogleSettings(request["calendarId"]|"",request["serviceJson"]|"",request["enabled"]|false);if(ok)pikaGoogleResetSession();
  }
  else if(action=="googleDisconnect"){ok=pikaClearGoogleSettings();pikaGoogleResetSession();}
  else if(action=="googleImport"){ok=pikaGoogleRequestImport(request["from"]|"",request["through"]|"");}
  else if(action=="googleSync"){ok=pikaGoogleSyncEnabled();if(ok){pikaGoogleResetSession();pikaGoogleRequestVerify();}}
  if(!ok){managerJsonError(400,"Could not save settings. Check the entered fields.");return;}
  if(action=="batteryCalibrate"){invalidatePortalCache();JsonDocument doc;doc["saved"]=true;doc["factor"]=pikaBatteryCalibration();managerDeviceJson(doc["device"].to<JsonObject>());String out;serializeJson(doc,out);transferServer.send(200,"application/json",out);return;}
  if(action!="soundTest"&&action!="vibrationTest")invalidatePortalCache();transferServer.sendHeader("Cache-Control","no-store");transferServer.send(200,"application/json","{\"saved\":true}");
}
static void handleManagerApp(){
  managerLastRequest=millis();transferServer.sendHeader("Cache-Control","no-cache");transferServer.sendHeader("Content-Encoding","gzip");
  transferServer.send_P(200,"text/html; charset=utf-8",reinterpret_cast<const char*>(kManagerGzip),sizeof(kManagerGzip));
}
static void handleGoogleHelp(){
  transferServer.send(200,"text/html; charset=utf-8",String("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>Google Calendar setup</title>")+portalCss()+
    "<main class=wrap><a class=btn href='/manager'>Back to Pika</a><h1 style='font-size:48px;margin-top:30px'>Google Calendar</h1><ol style='line-height:1.8'><li>Create a project in Google Cloud Console and enable Google Calendar API.</li><li>Create an IAM service account. No project role is required.</li><li>Open the service account: Keys, Add key, Create new key, JSON.</li><li>Create a separate Pika calendar in Google Calendar.</li><li>Share it with client_email from the JSON key. Grant permission to edit events.</li><li>Copy Calendar ID from Integrate calendar.</li><li>In Pika Setup, upload the JSON key, paste Calendar ID, enable sync and save.</li></ol><p>Keep the JSON key private. It is stored on this device and is never returned by the settings API. Sync is one-way, Pika to Google; tasks and memories stay local.</p></main>");
}
