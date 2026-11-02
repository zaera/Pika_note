#include "phone_sync.h"
#include "buffered_json_file.h"
#include "manager_store.h"
#include "undo_store.h"
#include "data_backup.h"
#include "google_calendar.h"
#include "notes.h"
#include "brain.h"
#include "storage_commit.h"
#include "utf8_text.h"
#include "../../globals.h"
#include "../../config.h"
#include <SD_MMC.h>
#include <algorithm>
#include <time.h>

namespace {
const char* kStore = NOTES_DIR "/manager.json";
unsigned readSnapshotDepth=0;bool readSnapshotReady=false;PIKA_JSON_DOCUMENT(readSnapshot);
void cacheReadStore(const JsonDocument& doc){
 if(!readSnapshotDepth)return;
 // A failed deep copy must never publish a partial snapshot as valid.
 readSnapshotReady=readSnapshot.set(doc)&&readSnapshot["records"].is<JsonArray>()&&readSnapshot["revision"].is<uint32_t>();
 if(!readSnapshotReady)readSnapshot.clear();
}
bool readStore(JsonDocument& doc) {
  if(readSnapshotDepth&&readSnapshotReady){
   const bool copied=doc.set(readSnapshot)&&doc["records"].is<JsonArray>()&&doc["revision"].is<uint32_t>();
#if defined(ARDUINO)
   if(!copied)Serial.printf("[ManagerRead] snapshot_copy_failed heap=%u\n",ESP.getFreeHeap());
#endif
   return copied;
  }
  recoverStoredFile(SD_MMC, String(kStore));
  for(int attempt=0;attempt<3;++attempt){
    File file=SD_MMC.open(kStore);
    if(!file){
      // An existing store that cannot be opened is an I/O failure, not an empty account.
      if(SD_MMC.exists(kStore))continue;
      if(attempt<2)continue;
      doc.clear();doc["revision"]=0;doc["records"].to<JsonArray>();cacheReadStore(doc);return true;
    }
    doc.clear();const auto error=pikaReadJsonBuffered(doc,file);file.close();
    if(!error&&doc["records"].is<JsonArray>()){cacheReadStore(doc);return true;}
  }
  return false;
}

bool writeStore(JsonDocument& doc) {
  if(doc.overflowed()||!doc["records"].is<JsonArray>())return false;
#if defined(ARDUINO)
  Serial.printf("[ManagerWrite] begin heap=%u revision=%lu\n",ESP.getFreeHeap(),(unsigned long)(doc["revision"]|0UL));
#endif
  invalidateBrainTaskCache();
  readSnapshotReady=false;readSnapshot.clear();
  doc["revision"] = (doc["revision"] | 0UL) + 1;
  const String temp = String(kStore) + ".tmp";
  SD_MMC.remove(temp);
  File file = SD_MMC.open(temp, FILE_WRITE);
  if (!file) return false;
  const size_t expected = measureJson(doc), written = pikaWriteJsonBuffered(doc, file);
  file.flush();
  const bool complete = written == expected && file.size() == expected;
  file.close();
#if defined(ARDUINO)
  Serial.printf("[ManagerWrite] bytes=%u expected=%u complete=%d\n",unsigned(written),unsigned(expected),complete);
#endif
  const bool committed=complete&&commitStoredFile(SD_MMC,String(kStore),temp);
#if defined(ARDUINO)
  Serial.printf("[ManagerWrite] committed=%d\n",committed);
#endif
  return committed;
}
bool finishMutation(JsonDocument& doc,const String& key,const String& id){
  if(key.length()){
    JsonObject receipts=doc["receipts"].is<JsonObject>()?doc["receipts"].as<JsonObject>():doc["receipts"].to<JsonObject>();
    while(receipts.size()>=128){String oldest=receipts.begin()->key().c_str();receipts.remove(oldest);}
    receipts[key]=id;
  }return writeStore(doc);
}
String notePath(int note, const char* ext) {
  char path[80];snprintf(path, sizeof(path), "%s/note_%03d.%s", NOTES_DIR, note, ext);return path;
}
JsonObject findRecord(JsonDocument& doc, const String& id) {
  for (JsonObject record : doc["records"].as<JsonArray>())
    if (String(record["id"] | "") == id) return record;
  return JsonObject();
}
bool writeText(const String& path, const String& text) {
  const String temp = path + ".tmp";
  SD_MMC.remove(temp);
  File file=SD_MMC.open(temp,FILE_WRITE);
  if(!file)return false;
  const size_t count=file.print(text);file.flush();
  const bool ok=count==text.length()&&file.size()==text.length();file.close();
  return ok&&commitStoredFile(SD_MMC,path,temp);
}
}

void managerBeginReadSnapshot(){++readSnapshotDepth;}
void managerEndReadSnapshot(){if(readSnapshotDepth&&!--readSnapshotDepth){readSnapshotReady=false;readSnapshot.clear();readSnapshot.shrinkToFit();}}

bool managerNoteExists(int note) {
  return std::any_of(noteIndex.begin(),noteIndex.end(),[note](const NoteEntry& n){return n.num==note;});
}
bool managerArchived(int note) { return SD_MMC.exists(notePath(note,"archived")); }
bool managerTextOnly(int note) { return SD_MMC.exists(notePath(note,"manual")); }
String managerReadText(int note,size_t limit) {
  File file=SD_MMC.open(notePath(note,"txt"));if(!file)return "";
  const size_t count=std::min(static_cast<size_t>(file.size()),limit+4);
  String text;if(!text.reserve(count+1)){file.close();return "";}
  char buffer[512];
  while(file.available()&&text.length()<count){const size_t got=file.readBytes(buffer,std::min(sizeof(buffer),count-text.length()));if(!got)break;text.concat(buffer,got);}
  file.close();const size_t end=pikaUtf8Prefix(text.c_str(),limit);if(end<text.length())text.remove(end);return text;
}
String managerLocalToUtc(const String& local,bool dayOnly,bool* valid) {
  if(valid)*valid=false;
  if(!local.length()){if(valid)*valid=true;return "";}
  int y,m,d,h=0,min=0;
  if((dayOnly&&local.length()!=10)||(!dayOnly&&local.length()!=16))return "";
  if(sscanf(local.c_str(),dayOnly?"%d-%d-%d":"%d-%d-%dT%d:%d",&y,&m,&d,&h,&min)!=(dayOnly?3:5))return "";
  if(y<2020||y>2099||m<1||m>12||d<1||d>31||h<0||h>23||min<0||min>59)return "";
  setenv("TZ",LOCAL_TZ_RULE,1);tzset();
  struct tm value={};value.tm_year=y-1900;value.tm_mon=m-1;value.tm_mday=d;value.tm_hour=h;value.tm_min=min;value.tm_isdst=-1;
  time_t epoch=mktime(&value);
  if(value.tm_year!=y-1900||value.tm_mon!=m-1||value.tm_mday!=d||value.tm_hour!=h||value.tm_min!=min)return "";
  struct tm utc={};gmtime_r(&epoch,&utc);char out[25];strftime(out,sizeof(out),"%Y-%m-%dT%H:%M:%SZ",&utc);
  if(valid)*valid=true;return out;
}
String managerUtcToLocalInput(const String& utc,bool dayOnly) {
  if(!utc.length())return "";
  String local=utcToLocalDeviceLabel(utc);if(local=="time not set")return "";
  if(dayOnly)return local.substring(0,10);local.setCharAt(10,'T');return local;
}

bool managerMergeTasks(std::vector<BrainTaskRecord>& tasks) {
  PIKA_JSON_DOCUMENT(doc);if(!readStore(doc)){
#if defined(ARDUINO)
 Serial.printf("[TaskRead] manager overlay failed heap=%u psram=%u\n",ESP.getFreeHeap(),ESP.getFreePsram());
#endif
 return false;
 }
  for(JsonObject record:doc["records"].as<JsonArray>()){
    const String id=record["id"]|"";
    auto found=std::find_if(tasks.begin(),tasks.end(),[&](const BrainTaskRecord& task){return task.id==id;});
    if(record["deleted"]|false){if(found!=tasks.end())tasks.erase(found);continue;}
    if(found==tasks.end()){
      // Overrides never resurrect an AI task that was removed by re-analysis.
      if(!(record["manual"]|false))continue;
      BrainTaskRecord created;created.id=id;created.noteNum=record["noteNum"]|-1;created.createdUtc=record["createdUtc"]|"";
      tasks.push_back(created);found=tasks.end()-1;
    }
    found->text=record["text"]|found->text;
    found->dueUtc=record["dueUtc"]|found->dueUtc;
    found->endUtc=record["endUtc"]|found->endUtc;
    found->type=record["type"]|found->type;
    found->datePrecision=record["datePrecision"]|found->datePrecision;
    found->priority=record["priority"]|found->priority;
    found->updatedUtc=record["updatedUtc"]|found->updatedUtc;
    found->archived=record["archived"]|false;
    if(record["done"].is<bool>())found->done=record["done"].as<bool>();
  }
  return true;
}
int managerSetTaskDone(const String& id,bool done) {
  PIKA_JSON_DOCUMENT(doc);if(!readStore(doc))return -1;
  JsonObject record=findRecord(doc,id);
  if(record.isNull())return 0;
  if(record["deleted"]|false)return -1;
  record["done"]=done;if(!done)record["archived"]=false;record["updatedUtc"]=currentUtcIso();
  return writeStore(doc)?1:-1;
}
uint32_t managerRevision(){PIKA_JSON_DOCUMENT(doc);return readStore(doc)?(doc["revision"]|0UL):0;}
void managerTouch(){PIKA_JSON_DOCUMENT(doc);if(readStore(doc))writeStore(doc);}

// Purge uses a frozen snapshot; active descendants of removed notes are detached
// into full manual records before their source files can disappear.
static bool purgeManagerArchive(JsonDocument& doc,const String& key,String& id,String& error){
 const String today=managerUtcToLocalInput(currentUtcIso(),true);
 if(today.length()!=10){error="Set the device clock before clearing Archive";return false;}
 std::vector<int> notes;for(const auto& n:noteIndex)if(managerArchived(n.num))notes.push_back(n.num);
 const auto tasks=loadBrainTasks();std::vector<String> removed,events;
 for(const auto& task:tasks){
  const bool event=task.type=="event";
  const String last=managerUtcToLocalInput(task.endUtc.length()?task.endUtc:task.dueUtc,true);
  const bool archived=task.done||task.archived||(event&&last.length()==10&&last<today);
  const bool losesSource=std::find(notes.begin(),notes.end(),task.noteNum)!=notes.end();
  if(!archived&&!losesSource)continue;
  JsonObject record=findRecord(doc,task.id);if(record.isNull())record=doc["records"].add<JsonObject>();
  if(archived){record.clear();record["id"]=task.id;record["deleted"]=true;removed.push_back(task.id);if(event)events.push_back(task.id);}
  else {record["id"]=task.id;record["manual"]=true;record["noteNum"]=-1;record["text"]=task.text;record["type"]=task.type;record["dueUtc"]=task.dueUtc;record["endUtc"]=task.endUtc;record["datePrecision"]=task.datePrecision;record["priority"]=task.priority;record["done"]=false;record["archived"]=false;record["createdUtc"]=task.createdUtc;record["updatedUtc"]=task.updatedUtc;}
 }
 // Keep a durable cleanup journal so interrupted file removal can be resumed.
 for(JsonVariant value:doc["archivePurge"]["notes"].as<JsonArray>()){int n=value.as<int>();if(std::find(notes.begin(),notes.end(),n)==notes.end())notes.push_back(n);}
 for(JsonVariant value:doc["archivePurge"]["records"].as<JsonArray>()){String v=value.as<String>();if(std::find(removed.begin(),removed.end(),v)==removed.end())removed.push_back(v);}
 for(JsonVariant value:doc["archivePurge"]["events"].as<JsonArray>()){String v=value.as<String>();if(std::find(events.begin(),events.end(),v)==events.end())events.push_back(v);}
 auto journal=doc["archivePurge"].to<JsonObject>();auto jn=journal["notes"].to<JsonArray>();for(int n:notes)jn.add(n);auto jr=journal["records"].to<JsonArray>();for(const String& v:removed)jr.add(v);auto je=journal["events"].to<JsonArray>();for(const String& v:events)je.add(v);
 // Do not turn a local archive purge into remote Google Calendar deletion.
 if(!pikaGoogleForgetLocalEvents(events)){error="Could not preserve Google Calendar links; nothing deleted";return false;}
 if(!writeStore(doc)){error="Could not save archive cleanup plan";return false;}
 auto failed=[&](){error="Archive cleanup incomplete. Some archived data was removed; retry to finish.";return false;};
 // Strip deleted AI task bodies as well as manager overrides. Tombstones keep
 // re-analysis and repeated imports from resurrecting the same record IDs.
 for(const auto& note:noteIndex){
  if(std::find(notes.begin(),notes.end(),note.num)!=notes.end())continue;
  const String path=notePath(note.num,"brain.json");recoverStoredFile(SD_MMC,path);File file=SD_MMC.open(path);if(!file)continue;
  JsonDocument brain;const auto parse=pikaReadJsonBuffered(brain,file);file.close();if(parse)return failed();
  auto array=brain["tasks"].as<JsonArray>();bool changed=false;
  for(int i=int(array.size())-1;i>=0;--i){String taskId=array[i]["id"]|"";
   if(!taskId.length()){String title=array[i]["title"]|"";uint32_t hash=2166136261UL;for(size_t c=0;c<title.length();++c){hash^=uint8_t(title[c]);hash*=16777619UL;}char legacy[32];snprintf(legacy,sizeof(legacy),"%d-%08lx",note.num,(unsigned long)hash);taskId=legacy;}
   if(std::find(removed.begin(),removed.end(),taskId)!=removed.end()){array.remove(i);changed=true;}
  }
  if(changed){String json;serializeJson(brain,json);if(!writeText(path,json))return failed();removeBrainSearchIndexEntry(note.num);removeBrainVectorIndexEntry(note.num);}
 }
 // Legacy completion state contains normalized task text; remove those rows too.
 const String statePath=NOTES_DIR "/tasks.state";recoverStoredFile(SD_MMC,statePath);File state=SD_MMC.open(statePath);
 if(state){String kept,line;while(state.available()){const char c=char(state.read());if(c!='\n'){line+=c;continue;}int end=line.indexOf('|');String taskId=end<0?line:line.substring(0,end);if(std::find(removed.begin(),removed.end(),taskId)==removed.end())kept+=line+"\n";line="";}state.close();if(line.length()){int end=line.indexOf('|');String taskId=end<0?line:line.substring(0,end);if(std::find(removed.begin(),removed.end(),taskId)==removed.end())kept+=line+"\n";}if(!writeText(statePath,kept))return failed();}
 for(int note:notes)if(!deleteNote(note))return failed();
 doc.remove("archivePurge");id="archive";if(!finishMutation(doc,key,id))return failed();error="";return true;
}

static bool managerMutationImpl(JsonVariantConst request,String& id,String& error) {
  error="Invalid request";
  const String kind=request["kind"]|"",action=request["action"]|"save";
  id=request["id"]|"";
  const bool purge=kind=="archive"&&action=="purge";
  if(!purge&&((kind!="tasks"&&kind!="events"&&kind!="memories")||(action!="save"&&action!="delete"&&action!="toggle"&&action!="remember"&&action!="archive"))){error="Invalid record type/action: "+kind+"/"+action;return false;}
  PIKA_JSON_DOCUMENT(doc);if(!readStore(doc)){error="Manager storage is unreadable";return false;}
  const String requestKey=request["clientRequest"]|"";
  if(requestKey.length()>64){error="Invalid operation ID";return false;}
  if(requestKey.length()&&doc["receipts"][requestKey].is<const char*>()){id=doc["receipts"][requestKey].as<String>();return true;}
  if(request["revision"].is<uint32_t>()&&request["revision"].as<uint32_t>()!=(doc["revision"]|0UL)){error="Data changed in another tab. Reload before saving. [commit "+String((unsigned long)request["revision"].as<uint32_t>())+"/"+String((unsigned long)(doc["revision"]|0UL))+"]";return false;}
  if(purge){if(!(request["confirmed"]|false)){error="Confirm permanent archive deletion";return false;}return purgeManagerArchive(doc,requestKey,id,error);}
  if(kind=="memories"){
    const int note=id.startsWith("m")?id.substring(1).toInt():-1;
    if(id.length()&&!managerNoteExists(note)){error="Memory not found";return false;}
    if(action=="delete"){
      // Commit archive marker before removing audio. Text, metadata and links remain.
      if(!writeText(notePath(note,"archived"),"archived\n")){error="Could not archive memory";return false;}
      const String wav=notePath(note,"wav");if(!pikaPhoneFileWillChange(wav.c_str())){error="Cannot save recovery intent";return false;}pikaPhoneFileTouched(wav.c_str());
      if(SD_MMC.exists(wav)&&!SD_MMC.remove(wav)){error="Could not remove recording";return false;}
      return finishMutation(doc,requestKey,id);
    }
    if(action=="remember"){
      const String marker=notePath(note,"archived");if(!pikaPhoneFileWillChange(marker.c_str())){error="Cannot save recovery intent";return false;}pikaPhoneFileTouched(marker.c_str());
      // Preserve orphan recovery for a remembered memory whose audio was removed.
      if(!SD_MMC.exists(notePath(note,"wav"))&&!writeText(notePath(note,"manual"),"text-only\n"))return false;
      if(SD_MMC.exists(marker)&&!SD_MMC.remove(marker)){error="Could not restore memory";return false;}
      return finishMutation(doc,requestKey,id);
    }
    if(action!="save")return false;
    String text=request["record"]["text"]|"";text.trim();
    if(!text.length()||text.length()>32000){error="Transcript must contain 1 to 32000 UTF-8 bytes";return false;}
    const bool creating=!id.length();
    const int target=creating?nextNoteNumber():note;
    if(target<=0){error="No memory number available";return false;}
    String captured=currentUtcIso();
    if(creating&&request["record"]["date"].is<const char*>()){
      bool valid=false;captured=managerLocalToUtc(request["record"]["date"].as<String>(),false,&valid);
      if(!valid||!captured.length()){error="Invalid capture date";return false;}
    }
    if(creating){
      if(!captured.length()){error="Set the device clock before creating a memory";return false;}
      if(!writeText(notePath(target,"manual"),"text-only\n")||
         !writeText(notePath(target,"meta"),"created_utc="+captured+"\ntag=Inbox\nsynced=1\n"))return false;
    }
    if(!writeText(notePath(target,"txt"),text)){error="Could not save transcript";return false;}
    if(creating?!addToIndex(target,"Inbox",true):!updateIndexHasText(target)){error="Could not save catalog";return false;}
    // Keep the raw text immediately searchable. Re-analysis is queued, never
    // attempt transcription for a text-only note.
    removeBrainSearchIndexEntry(target);
    if(!writeNoteQueueState(target,"queued","text edited; re-analysis pending"))return false;
    id="m"+String(target);return finishMutation(doc,requestKey,id);
  }
  if(action=="remember"){error="Remember is only available for memories";return false;}
  const std::vector<BrainTaskRecord> tasks=loadBrainTasks();
  auto existing=std::find_if(tasks.begin(),tasks.end(),[&](const BrainTaskRecord& t){return t.id==id;});
  const bool creating=!id.length();
  if(!creating&&existing==tasks.end()){error="Task or event not found";return false;}
  if(action=="archive"){
    if(kind!="events"||existing==tasks.end()||existing->type!="event"){error="Only events can be archived this way";return false;}
    JsonObject record=findRecord(doc,id);if(record.isNull())record=doc["records"].add<JsonObject>();
    record["id"]=id;record["archived"]=request["record"]["archived"]|true;if(!(record["archived"]|true))record["done"]=false;record["updatedUtc"]=currentUtcIso();
    return finishMutation(doc,requestKey,id);
  }
  if(action=="toggle"){
    // The live task and optimistic revision were already validated above.
    // Persist one manager overlay, including for memory-derived tasks. Calling
    // setBrainTaskDone here recursively read/wrote manager.json, then reloaded
    // it for a second commit while the original JSON document was still live.
    if(existing->type=="event"||kind!="tasks"){error="Use Archive for events";return false;}
    JsonObject record=findRecord(doc,id);if(record.isNull())record=doc["records"].add<JsonObject>();
    const bool done=request["record"]["done"]|false;
    record["id"]=id;record["done"]=done;if(!done)record["archived"]=false;
    record["updatedUtc"]=currentUtcIso();
    if(doc.overflowed()){error="Not enough memory to save task status";return false;}
    if(!finishMutation(doc,requestKey,id)){error="Could not commit task status";return false;}
    error="";return true;
  }
  if(action=="delete"){
    JsonObject record=findRecord(doc,id);if(record.isNull())record=doc["records"].add<JsonObject>();
    record["id"]=id;record["deleted"]=true;return finishMutation(doc,requestKey,id);
  }
  String text=request["record"]["text"]|"";text.trim();
  if(!text.length()||text.length()>1500){error="Title must contain 1 to 1500 UTF-8 bytes";return false;}
  const bool event=kind=="events",allDay=event&&(request["record"]["allDay"]|false);
  const String local=request["record"]["date"]|"";
  bool valid=false;const String due=managerLocalToUtc(local,allDay,&valid);
  if(!valid||(event&&!due.length())){error="Invalid date (including daylight-saving gaps)";return false;}
  String end;
  if(event){end=managerLocalToUtc(request["record"]["end"]|"",allDay,&valid);if(!valid||!end.length()||end<due||(!allDay&&end==due)){error="End must be after start";return false;}}
  const String priority=request["record"]["priority"]|"normal";
  const int rank=priority=="urgent"?3:priority=="high"?2:priority=="normal"?1:priority=="low"?0:-1;
  if(rank<0){error="Invalid priority";return false;}
  if(creating){char generated[40];snprintf(generated,sizeof(generated),"web-%08lx-%08lx",(unsigned long)esp_random(),(unsigned long)millis());id=generated;}
  JsonObject record=findRecord(doc,id);if(record.isNull())record=doc["records"].add<JsonObject>();
  record["id"]=id;record["manual"]=creating||(record["manual"]|false);record["deleted"]=false;
  if(event&&(creating||existing->dueUtc!=due||existing->endUtc!=end||(request["restoreEvent"]|false))){record["archived"]=false;record["done"]=false;}
  if(!event&&(request["restoreTask"]|false)){record["done"]=false;record["archived"]=false;}
  record["text"]=text;record["dueUtc"]=due;record["endUtc"]=end;record["datePrecision"]=allDay?"day":due.length()?"time":"none";
  record["type"]=event?"event":"todo";record["priority"]=rank;record["updatedUtc"]=currentUtcIso();
  if(creating){
    record["createdUtc"]=currentUtcIso();record["done"]=false;
    String source=request["record"]["note"]|"";const int num=source.startsWith("m")?source.substring(1).toInt():-1;
    record["noteNum"]=managerNoteExists(num)?num:-1;
  }
  error="Could not commit changes";return finishMutation(doc,requestKey,id);
}

bool managerMutation(JsonVariantConst request,String& id,String& error){
 if(pikaRestoreBusy()){error="Restore in progress; wait or cancel it first";return false;}
 const String key=request["clientRequest"]|"";bool receipt=false;
 // Do not retain a full extra copy of manager.json throughout the commit.
 // Large JSON copies can exhaust the fragmented internal heap on the device.
 {PIKA_JSON_DOCUMENT(state);if(!readStore(state)){error="Manager storage is unreadable";return false;}
  receipt=key.length()&&state["receipts"][key].is<const char*>();
  if(!receipt&&request["revision"].is<uint32_t>()&&request["revision"].as<uint32_t>()!=(state["revision"]|0UL)){error="Data changed in another tab. Reload before saving.";return false;}
 }
 if(receipt)return managerMutationImpl(request,id,error);
 if(String(request["kind"]|"")=="archive"&&String(request["action"]|"")=="purge"){
  if(!(request["confirmed"]|false)){error="Confirm permanent archive deletion";return false;}
  if(!pikaUndoClear()){error="Could not discard undo history before permanent deletion";return false;}
  return managerMutationImpl(request,id,error);
 }
 if(pikaUndoActive())return managerMutationImpl(request,id,error);
 std::vector<JsonDocument> requests;JsonDocument entry;entry.set(request);requests.push_back(std::move(entry));
 const String label=String(request["action"]|"save")+" "+String(request["kind"]|"record");
 if(!pikaUndoBegin(requests,label,error))return false;
 const bool ok=managerMutationImpl(request,id,error);String undoError;
 if(!pikaUndoFinish(undoError)){error=undoError.length()?undoError:String("Could not finalize undo history");return false;}return ok;
}
void managerInvalidateReadSnapshot(){readSnapshotReady=false;readSnapshot.clear();}
bool managerBumpRevision(uint32_t floor){managerInvalidateReadSnapshot();PIKA_JSON_DOCUMENT(doc);if(!readStore(doc))return false;if((doc["revision"]|0UL)<floor)doc["revision"]=floor;return writeStore(doc);}

String managerGoogleBaseline(const String& id){
  PIKA_JSON_DOCUMENT(doc);if(!readStore(doc))return "";
  JsonObject record=findRecord(doc,id);return record["googleBaseline"]|"";
}
int managerImportGoogleEvent(const BrainTaskRecord& task,const String& baseline,String& error){
  PIKA_JSON_DOCUMENT(doc);if(!readStore(doc)){error="Manager storage unreadable";return -1;}
  // Keep tombstones too: a manually deleted event must not reappear on import.
  if(!findRecord(doc,task.id).isNull())return 0;
  const auto existing=loadBrainTasks();
  for(const auto& value:existing)if(value.id==task.id)return 0;
  JsonObject record=doc["records"].add<JsonObject>();
  record["id"]=task.id;record["manual"]=true;record["deleted"]=false;record["noteNum"]=-1;
  record["type"]="event";record["text"]=task.text;record["dueUtc"]=task.dueUtc;record["endUtc"]=task.endUtc;
  record["datePrecision"]=task.datePrecision;record["priority"]=task.priority;record["done"]=false;
  record["createdUtc"]=currentUtcIso();record["updatedUtc"]=currentUtcIso();record["googleBaseline"]=baseline;
  if(!writeStore(doc)){error="Could not save imported event";return -1;}return 1;
}

bool managerKnownRecord(const String& id){PIKA_JSON_DOCUMENT(doc);return readStore(doc)&&!findRecord(doc,id).isNull();}

bool managerPhoneReceipts(JsonDocument& out){PIKA_JSON_DOCUMENT(doc);if(!readStore(doc))return false;out["receipts"].set(doc["receipts"]);return true;}
