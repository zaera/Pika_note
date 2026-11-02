#pragma once
#include "data_files.h"
#include <set>
inline bool pikaPhoneNormalFile(const String& name){return !name.startsWith(".undo-")&&!name.endsWith(".tmp")&&!name.endsWith(".bak")&&(!name.endsWith(".wav")||(name.startsWith("note_")||name.startsWith("askq_")));}
inline bool pikaUpdatePhoneInventory(JsonObject inventory,const std::vector<PikaDataFile>& files,const std::set<String>& touched,bool fullScan,uint32_t generation){
 bool changed=false;std::set<String> current;
 for(const auto& file:files){if(!pikaPhoneNormalFile(file.name))continue;current.insert(file.name);String stamp=String(file.modified)+":"+String(file.size);JsonObject old=inventory[file.name];
  if(old.isNull()||(old["deleted"]|false)||String(old["stat"]|"")!=stamp||touched.count(file.name)){
   JsonObject item=inventory[file.name].to<JsonObject>();item["name"]=file.name;item["size"]=file.size;item["stat"]=stamp;item["version"]=generation;changed=true;
  }
 }
 for(JsonPair pair:inventory){JsonObject file=pair.value().as<JsonObject>();if((fullScan||touched.count(String(pair.key().c_str())))&&!current.count(String(pair.key().c_str()))&&!(file["deleted"]|false)){file["deleted"]=true;file["version"]=generation;changed=true;}}
 return changed;
}
