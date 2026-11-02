#pragma once
#include "data_files.h"
#include "phone_sync.h"
#include "phone_link.h"
#include <SD_MMC.h>
// Commit audio before publishing metadata; never expose a partial request.
inline bool pikaOfflineAskPending(){File dir=SD_MMC.open("/notes");if(!dir)return false;File file=dir.openNextFile();while(file){String name=file.name();const bool pending=name.indexOf("askq_")>=0&&name.endsWith(".json");file.close();if(pending){dir.close();return true;}file=dir.openNextFile();}dir.close();return false;}
inline bool pikaQueueOfflineAsk(const char* source,const String& captured){
 JsonDocument counter;pikaDataReadJson("/notes/.ask-outbox-counter.json",counter,4096);uint32_t seq=uint32_t(counter["sequence"]|0UL)+1;counter["sequence"]=seq;
 if(!pikaDataWriteJson("/notes/.ask-outbox-counter.json",counter,true))return false;
 char stem[64];snprintf(stem,sizeof(stem),"/notes/askq_%010lu_%08lx",(unsigned long)seq,(unsigned long)esp_random());String wav=String(stem)+".wav",meta=String(stem)+".json";
 if(!pikaDataCopy(source,wav))return false;pikaPhoneFileTouched(wav.c_str());JsonDocument item;item["sequence"]=seq;item["capturedUtc"]=captured;item["audio"]=wav.substring(7);item["version"]=1;
 if(!pikaDataWriteJson(meta,item,true)){SD_MMC.remove(wav);pikaPhoneFileTouched(wav.c_str());return false;}pikaPhoneChanged();return true;
}
inline bool pikaAckOfflineAsk(const String& name){
 if(!name.startsWith("askq_")||!name.endsWith(".json")||name.length()!=29)return false;
 for(size_t i=5;i<name.length()-5;i++)if(!isxdigit(name[i])&&name[i]!='_')return false;
 const String meta="/notes/"+name,wav=meta.substring(0,meta.length()-5)+".wav";
 if(SD_MMC.exists(meta)&&!SD_MMC.remove(meta))return false;
 if(SD_MMC.exists(wav)&&!SD_MMC.remove(wav))return false;
 pikaPhoneFileTouched(meta.c_str());pikaPhoneFileTouched(wav.c_str());pikaPhoneChanged();return true;
}
