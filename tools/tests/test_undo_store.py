from pathlib import Path
import runpy, subprocess, tempfile, sys
root=Path(__file__).resolve().parents[2]
b=runpy.run_path(str(root/'tools/tests/test_manager_store.py'))
prefix='#define PIKA_REAL_UNDO\n'+b['prefix']
prefix=prefix.replace('#include <map>','#include <map>\n#include <set>\n#include "data_files.h"')
a=prefix.index('struct FS {');z=prefix.index('} SD_MMC;',a)+len('} SD_MMC;')
prefix=prefix[:a]+r'''
std::set<std::string> dirs={"/notes"};
struct FS {
 File open(const String& path,int mode=0){const std::string key=path.c_str();if(mode)files[key]=std::make_shared<std::string>();return {files.count(key)?files[key]:nullptr,0};}
 bool exists(const String& path){return files.count(path.c_str())||dirs.count(path.c_str());}
 bool mkdir(const String& path){return dirs.insert(path.c_str()).second;}
 bool remove(const String& path){return files.erase(path.c_str())>0;}
 bool rename(const String& a,const String& b){
  std::string from=a.c_str(),to=b.c_str();
  if(files.count(from)){files[to]=files[from];files.erase(from);return true;}
  if(!dirs.count(from)||exists(b))return false;
  std::vector<std::pair<std::string,std::shared_ptr<std::string>>> moved;
  for(auto it=files.begin();it!=files.end();)if(it->first.rfind(from+"/",0)==0){moved.push_back({to+it->first.substr(from.size()),it->second});it=files.erase(it);}else ++it;
  for(auto& item:moved)files[item.first]=item.second;
  std::vector<std::string> ds;for(auto it=dirs.begin();it!=dirs.end();)if(*it==from||it->rfind(from+"/",0)==0){ds.push_back(to+it->substr(from.size()));it=dirs.erase(it);}else ++it;
  for(auto& d:ds)dirs.insert(d);return true;
 }
} SD_MMC;
''' +prefix[z:]
helpers=r'''
void brainInvalidateMemoryCache(){}
void loadIndex(){noteIndex.clear();for(auto& x:files){const auto& p=x.first;if(p.rfind("/notes/note_",0)==0&&p.size()==19&&p.substr(15)==".txt")noteIndex.push_back({atoi(p.substr(12,3).c_str()),true});}}
bool pikaDataParents(const String&){return true;}
bool pikaDataHash(const String& path,String& hash,uint32_t* size){auto i=files.find(path.c_str());if(i==files.end())return false;hash=String(std::to_string(std::hash<std::string>{}(*i->second)));if(size)*size=i->second->size();return true;}
bool pikaDataCopy(const String& from,const String& to){auto i=files.find(from.c_str());if(i==files.end())return false;files[to.c_str()]=std::make_shared<std::string>(*i->second);return true;}
bool pikaDataRemoveTree(const String& path){std::string p=path.c_str();for(auto i=files.begin();i!=files.end();)if(i->first==p||i->first.rfind(p+"/",0)==0)i=files.erase(i);else ++i;for(auto i=dirs.begin();i!=dirs.end();)if(*i==p||i->rfind(p+"/",0)==0)i=dirs.erase(i);else ++i;return true;}
bool pikaDataReadJson(const String& path,JsonDocument& doc,size_t maximum){recoverStoredFile(SD_MMC,path);File f=SD_MMC.open(path);return f&&f.size()<=maximum&&!deserializeJson(doc,f);}
bool pikaDataWriteJson(const String& path,const JsonDocument& doc,bool){File f=SD_MMC.open(path+".tmp",FILE_WRITE);serializeJson(doc,f);return commitStoredFile(SD_MMC,path,path+".tmp");}
'''
raw=(root/'pika_note/src/app/data_files.cpp').read_text();pathfunc=raw[raw.index('bool pikaDataRelativePath'):raw.index('bool pikaDataHash')]
u=(root/'pika_note/src/app/undo_store.cpp').read_text();u=u[u.index('namespace {'):]
main=r'''
void put(const char* p,const char* v){auto f=SD_MMC.open(p,FILE_WRITE);f.print(v);}
bool mutate(const char* json,String& id,String& error){JsonDocument r;assert(!deserializeJson(r,json));return managerMutation(r.as<JsonVariantConst>(),id,error);}
void reset(){pikaUndoClear();files.clear();dirs={"/notes"};noteIndex.clear();base.clear();managerInvalidateReadSnapshot();}
int main(){
 String id,error;
 reset();put("/notes/index.csv","");
 assert(mutate(R"({"kind":"tasks","action":"save","record":{"text":"First task","priority":"normal"}})",id,error));
 assert(loadBrainTasks().size()==1);auto rev=managerRevision();
 assert(pikaUndoApply("u1",error));assert(loadBrainTasks().empty()&&managerRevision()>rev);
 rev=managerRevision();assert(pikaUndoApply("u1",error)&&managerRevision()==rev);assert(!pikaUndoApply("u2",error));
 // One Ask batch is one undo, including multiple created records.
 reset();put("/notes/index.csv","");std::vector<JsonDocument> batch(2);
 assert(!deserializeJson(batch[0],R"({"kind":"tasks","record":{"text":"A","priority":"normal"}})"));
 assert(!deserializeJson(batch[1],R"({"kind":"tasks","record":{"text":"B","priority":"normal"}})"));
 assert(pikaUndoBegin(batch,"Ask batch",error));for(auto& r:batch)assert(managerMutation(r.as<JsonVariantConst>(),id,error));assert(pikaUndoFinish(error));assert(loadBrainTasks().size()==2);
 assert(pikaUndoApply("batch",error));assert(loadBrainTasks().empty());
 // A rejected edit retains the previous successful undo.
 assert(mutate(R"({"kind":"tasks","record":{"text":"Keep undo","priority":"normal"}})",id,error));
 assert(!mutate(R"({"kind":"tasks","record":{"text":"","priority":"normal"}})",id,error));assert(pikaUndoApply("retained",error));assert(loadBrainTasks().empty());
 // Protect later data changes, including writes without revision changes.
 assert(mutate(R"({"kind":"tasks","record":{"text":"Protected","priority":"normal"}})",id,error));put("/notes/tasks.state","newer write");assert(!pikaUndoApply("conflict",error));assert(loadBrainTasks().size()==1);
 // Transcript editing and Forget undo; removed WAV never reappears.
 reset();put("/notes/index.csv","1|date|1\n");put("/notes/note_001.txt","original transcript");put("/notes/note_001.wav","audio");loadIndex();
 assert(mutate(R"({"kind":"memories","id":"m1","record":{"text":"new transcript"}})",id,error));assert(pikaUndoApply("text",error));assert(managerReadText(1)=="original transcript");assert(SD_MMC.exists("/notes/note_001.wav"));
 assert(mutate(R"({"kind":"memories","action":"delete","id":"m1"})",id,error));assert(managerArchived(1)&&!SD_MMC.exists("/notes/note_001.wav"));
 assert(pikaUndoApply("forget",error));assert(!managerArchived(1)&&managerReadText(1)=="original transcript");assert(!SD_MMC.exists("/notes/note_001.wav")&&SD_MMC.exists("/notes/note_001.manual"));
 // Interrupted batch is rolled back on boot.
 reset();put("/notes/index.csv","");assert(pikaUndoBegin(batch,"Interrupted",error));assert(managerMutation(batch[0].as<JsonVariantConst>(),id,error));assert(loadBrainTasks().size()==1);pikaUndoAbort();assert(pikaUndoRecover(error));assert(loadBrainTasks().empty());
 // Power loss after undo intent: replay all before images and finalize.
 assert(mutate(R"({"kind":"tasks","record":{"text":"Replay","priority":"normal"}})",id,error));JsonDocument j;assert(pikaDataReadJson("/notes/.undo-live/manifest.json",j));j["state"]="undoing";j["revisionFloor"]=managerRevision();assert(pikaDataWriteJson("/notes/.undo-live/manifest.json",j));assert(pikaUndoRecover(error));assert(loadBrainTasks().empty());
 assert(!pikaDataRelativePath("../manager.json")&&!pikaDataRelativePath("x/../../notes")&&!pikaDataRelativePath(".undo-live/manifest.json")&&!pikaDataRelativePath("a\\b"));assert(pikaDataRelativePath("note_001.brain.json"));
 puts("Real undo transaction: single/batch, retry, conflicts, transcript, Forget without WAV, interrupted batch and undo replay: PASS");
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'Arduino.h').write_text(b['arduino']);(p/'WString.h').write_text('#include "Arduino.h"');(p/'test.cpp').write_text(prefix+pathfunc+helpers+b['source']+u+main,encoding='utf-8');exe=p/'undo.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-I',str(p),'-I',str(root/'pika_note/src/app'),'-I',str(root/'.pio/libdeps/pika_note_2/ArduinoJson/src'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
