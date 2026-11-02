#include "phone_sync.h"
#include "undo_store.h"
#include "../../globals.h"
#include "data_files.h"
#include "manager_store.h"
#include "notes.h"
#include "brain.h"
#include "storage_commit.h"
#include <SD_MMC.h>
#include <algorithm>
namespace {
const String live="/notes/.undo-live",stage="/notes/.undo-stage",old="/notes/.undo-old";
bool active=false;JsonDocument journal;
void traceUndo(const char* phase){
#if defined(ARDUINO)
 Serial.printf("[Undo] phase=%s heap=%u stack=%u at=%lu\n",phase,ESP.getFreeHeap(),uxTaskGetStackHighWaterMark(nullptr),(unsigned long)millis());
#endif
}
String manifest(const String& folder){return folder+"/manifest.json";}
String slot(const String& folder,size_t n){return folder+"/before-"+String((unsigned long)n);}
void addPath(std::vector<String>& paths,const String& path){if(std::find(paths.begin(),paths.end(),path)==paths.end())paths.push_back(path);}
bool validPath(const String& path){return path.startsWith("/notes/")&&pikaDataRelativePath(path.substring(7));}
void invalidate(){brainInvalidateMemoryCache();for(const char* name:{"/notes/brain.search.v3","/notes/brain.vectors.v2"})for(const char* suffix:{"",".tmp",".bak"}){SD_MMC.remove(String(name)+suffix);pikaPhoneFileTouched(name);}managerInvalidateReadSnapshot();loadIndex();}
bool promote(){
 if(!pikaDataRemoveTree(old))return false;
 if(SD_MMC.exists(live)&&!SD_MMC.rename(live,old))return false;
 if(!SD_MMC.rename(stage,live)){if(!SD_MMC.exists(live)&&SD_MMC.exists(old))SD_MMC.rename(old,live);return false;}
 return pikaDataRemoveTree(old);
}
bool restoreFiles(const String& folder,JsonDocument& doc,String& error){
 auto rows=doc["files"].as<JsonArray>();if(rows.size()>2048){error="Invalid undo journal";return false;}
 // Verify every before-image before writing any live file.
 size_t n=0;for(auto row:rows){const String path=row["path"]|"";if(!validPath(path)){error="Invalid undo path";return false;}
  if(row["before"]|false){String hash;if(!pikaDataHash(slot(folder,n),hash)||hash!=String(row["beforeSha"]|"")){error="Undo snapshot is damaged";return false;}}++n;
 }
 if(!pikaPhoneFileWillChange("/notes")){error="Could not save index recovery intent";return false;}
 n=0;for(auto row:rows){const String path=row["path"]|"";pikaPhoneFileTouched(path.c_str());
  if(row["before"]|false){const String temp=path+".tmp";if(!pikaDataCopy(slot(folder,n),temp)||!commitStoredFile(SD_MMC,path,temp)){error="Undo interrupted; it will resume on restart";return false;}}
  else if(SD_MMC.exists(path)&&!SD_MMC.remove(path)){error="Undo could not remove a created file";return false;}
  SD_MMC.remove(path+".tmp");SD_MMC.remove(path+".bak");++n;
 }
 invalidate();
 // Forget removes WAV permanently. Keep recovered text-only memories recoverable.
 for(const auto& note:noteIndex){char wav[64],txt[64],manual[64];snprintf(wav,sizeof(wav),"/notes/note_%03d.wav",note.num);snprintf(txt,sizeof(txt),"/notes/note_%03d.txt",note.num);snprintf(manual,sizeof(manual),"/notes/note_%03d.manual",note.num);
  if(!SD_MMC.exists(wav)&&SD_MMC.exists(txt)&&!SD_MMC.exists(manual)){File marker=SD_MMC.open(manual,FILE_WRITE);if(!marker||marker.print("text-only\n")!=10){if(marker)marker.close();error="Could not preserve restored text-only memory";return false;}marker.flush();marker.close();pikaPhoneFileTouched(manual);}
 }
 return true;
}
}
bool pikaUndoActive(){return active;}
bool pikaUndoBegin(const std::vector<JsonDocument>& requests,const String& label,String& error){
 if(active)return true;
 traceUndo("recover");if(!pikaUndoRecover(error))return false;
 traceUndo("prepare-stage");
 if(!pikaDataRemoveTree(stage)||!SD_MMC.mkdir(stage)){error="Could not prepare undo storage";return false;}
 journal.clear();journal["state"]="collecting";journal["label"]=label;journal["beforeRevision"]=managerRevision();
 std::vector<String> paths;for(const char* path:{"/notes/manager.json","/notes/index.csv","/notes/tasks.state"})addPath(paths,path);
 for(const auto& request:requests)if(String(request["kind"]|"")=="memories"){
  const String id=request["id"]|"";const int note=id.length()?id.substring(1).toInt():nextNoteNumber();if(note<=0){error="Invalid undo target";return false;}
  for(const char* ext:{"txt","meta","queue","manual","archived","moments","brain.json","brain.vec"}){char path[80];snprintf(path,sizeof(path),"/notes/note_%03d.%s",note,ext);addPath(paths,path);}
 }
 auto rows=journal["files"].to<JsonArray>();size_t index=0;
 for(const String& path:paths){traceUndo("snapshot-file");auto row=rows.add<JsonObject>();row["path"]=path;const bool present=SD_MMC.exists(path);row["before"]=present;
  if(present){String hash;if(!pikaDataHash(path,hash)||!pikaDataCopy(path,slot(stage,index))){pikaDataRemoveTree(stage);error="Could not save the undo snapshot; nothing changed";return false;}row["beforeSha"]=hash;}++index;
 }
 traceUndo("write-manifest");
 if(!pikaDataWriteJson(manifest(stage),journal)){pikaDataRemoveTree(stage);error="Could not save undo journal";return false;}active=true;traceUndo("prepared");return true;
}
bool pikaUndoFinish(String& error){
 if(!active)return true;bool changed=false;auto rows=journal["files"].as<JsonArray>();
 for(auto row:rows){const String path=row["path"]|"";const bool present=SD_MMC.exists(path);row["after"]=present;String hash;if(present&&!pikaDataHash(path,hash)){error="Could not finalize undo journal";active=false;return false;}row["afterSha"]=hash;
  changed|=present!=(row["before"]|false)||(present&&hash!=String(row["beforeSha"]|""));
 }
 if(!changed){active=false;journal.clear();return pikaDataRemoveTree(stage);}
 journal["state"]="ready";journal["afterRevision"]=managerRevision();
 if(!pikaDataWriteJson(manifest(stage),journal)){active=false;error="Could not finalize undo journal";return false;}
 active=false;journal.clear();if(!promote()){error="Could not commit undo history";return false;}return true;
}
void pikaUndoAbort(){active=false;journal.clear();}
bool pikaUndoClear(){active=false;journal.clear();return pikaDataRemoveTree(stage)&&pikaDataRemoveTree(live)&&pikaDataRemoveTree(old);}
void pikaUndoStatus(JsonObject out){JsonDocument doc;const bool ready=pikaDataReadJson(manifest(live),doc)&&String(doc["state"]|"")=="ready";
 out["available"]=ready&&managerRevision()==(doc["afterRevision"]|0UL);out["label"]=ready?String(doc["label"]|"Last change"):String();out["wavRecoverable"]=false;
}
bool pikaUndoApply(const String& operationKey,String& error){
 if(active){error="Wait until the current operation finishes";return false;}
 JsonDocument doc;if(!pikaDataReadJson(manifest(live),doc)){error="No change to undo";return false;}
 const String state=doc["state"]|"";
 if(state=="used"&&operationKey.length()&&operationKey==String(doc["undoKey"]|""))return true;
 if(state!="ready"){error="No change to undo";return false;}
 const uint32_t before=managerRevision();if(before!=(doc["afterRevision"]|0UL)){error="Data changed after this operation; undo would overwrite newer changes";return false;}
 for(auto row:doc["files"].as<JsonArrayConst>()){
  const String path=row["path"]|"";const bool present=SD_MMC.exists(path);String hash;
  if(!validPath(path)||present!=(row["after"]|false)||(present&&(!pikaDataHash(path,hash)||hash!=String(row["afterSha"]|"")))){error="Data changed after this operation; nothing was undone";return false;}
 }
 doc["state"]="undoing";doc["undoKey"]=operationKey;doc["revisionFloor"]=before;
 if(!pikaDataWriteJson(manifest(live),doc)){error="Could not prepare undo";return false;}
 if(!restoreFiles(live,doc,error)||!managerBumpRevision(before)){if(!error.length())error="Could not finalize restored data";return false;}
 doc["state"]="used";return pikaDataWriteJson(manifest(live),doc);
}
bool pikaUndoRecover(String& error){
 if(active)return true;
 JsonDocument pending;if(pikaDataReadJson(manifest(stage),pending)){
  const String state=pending["state"]|"";
  if(state=="ready"){if(!promote()){error="Could not finish committing undo history";return false;}}
  else if(state=="collecting"){
   const uint32_t floor=managerRevision();if(!restoreFiles(stage,pending,error)||!managerBumpRevision(floor)||!pikaDataRemoveTree(stage))return false;
  }else if(!pikaDataRemoveTree(stage))return false;
 }else if(SD_MMC.exists(stage)&&!pikaDataRemoveTree(stage))return false;
 if(!SD_MMC.exists(live)&&SD_MMC.exists(old)&&!SD_MMC.rename(old,live)){error="Could not recover undo history";return false;}
 JsonDocument doc;if(pikaDataReadJson(manifest(live),doc)&&String(doc["state"]|"")=="undoing"){
  const uint32_t floor=doc["revisionFloor"]|0UL;if(!restoreFiles(live,doc,error)||!managerBumpRevision(floor))return false;
  doc["state"]="used";if(!pikaDataWriteJson(manifest(live),doc))return false;
 }
 return true;
}

bool pikaUndoAlreadyApplied(const String& key){if(!key.length())return false;JsonDocument doc;return pikaDataReadJson(manifest(live),doc)&&String(doc["state"]|"")=="used"&&String(doc["undoKey"]|"")==key;}
