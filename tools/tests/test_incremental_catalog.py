"""Fault-injection tests against the production incremental catalog implementation."""
from pathlib import Path
import runpy, subprocess, sys, tempfile
root=Path(__file__).resolve().parents[2]
base=runpy.run_path(str(root/'tools/tests/test_manager_store.py'))
production=(root/'pika_note/src/app/phone_sync.cpp').read_text(encoding='utf-8')
production='\n'.join(line for line in production.splitlines() if not line.startswith('#include'))
arduino=base['arduino'].replace('String(unsigned long v)', 'String(unsigned int v):text(std::to_string(v)){}String(unsigned long v)')
arduino=arduino.replace('size_t length()const{return text.size();}', 'size_t length()const{return text.size();}bool isEmpty()const{return text.empty();}')
arduino=arduino.replace(' String(){}', ' String(unsigned int v,int):text(std::to_string(v)){} String(){}')
stubs=r'''#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 0
#define ARDUINOJSON_ENABLE_PROGMEM 0
#include "phone_sync.h"
#include "phone_inventory.h"
#include "storage_commit.h"
#include <map>
#include <memory>
#include <set>
#include <mutex>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#define HEX 16
#define INDEX_FILE "/notes/index.csv"
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
void* heap_caps_malloc(size_t n,int){return malloc(n);}void heap_caps_free(void*p){free(p);}void* heap_caps_realloc(void*p,size_t n,int){return realloc(p,n);}
class Print{public:virtual ~Print(){}virtual size_t write(uint8_t)=0;virtual size_t write(const uint8_t*,size_t)=0;};
struct mbedtls_sha256_context{uint64_t hash;};void mbedtls_sha256_init(mbedtls_sha256_context*){}void mbedtls_sha256_free(mbedtls_sha256_context*){}int mbedtls_sha256_starts(mbedtls_sha256_context*c,int){c->hash=1469598103934665603ULL;return 0;}int mbedtls_sha256_update(mbedtls_sha256_context*c,const uint8_t*b,size_t n){for(size_t i=0;i<n;i++)c->hash=(c->hash^b[i])*1099511628211ULL;return 0;}int mbedtls_sha256_finish(mbedtls_sha256_context*c,uint8_t*out){for(int i=0;i<32;i++)out[i]=uint8_t(c->hash>>((i%8)*8));return 0;}
struct{template<class... A>void printf(const char*,A...){}}Serial;
struct{unsigned getFreeHeap(){return 1000000;}unsigned getFreePsram(){return 8000000;}}ESP;
uint32_t millis(){static uint32_t n=1;return ++n;}unsigned uxTaskGetStackHighWaterMark(void*){return 10000;}uint32_t esp_random(){static uint32_t n=0;return ++n;}
std::map<std::string,std::shared_ptr<std::string>> disk;std::string failWrite,failRenameTo,failRemove;bool inputBusy=false,memoryFail=false,overlayFail=false;int scans=0,memoryReads=0,sourceReads=0,published=0;uint32_t revision=0;
struct File{std::shared_ptr<std::string> data;explicit operator bool()const{return bool(data);}bool isDirectory(){return false;}uint32_t getLastWrite(){return 7;}size_t size(){return data?data->size():0;}size_t write(const uint8_t*b,size_t n){if(!data)return 0;data->append((const char*)b,n);return n;}void flush(){}void close(){}};
#define FILE_WRITE 1
struct FS{File open(const String&p,int mode=0){std::string k=p.c_str();if(mode){if(k==failWrite)return {};disk[k]=std::make_shared<std::string>();}return {disk.count(k)?disk[k]:nullptr};}bool exists(const String&p){return disk.count(p.c_str());}bool remove(const String&p){if(p.c_str()==failRemove)return false;return disk.erase(p.c_str())>0;}bool rename(const String&a,const String&b){if(b.c_str()==failRenameTo)return false;if(!disk.count(a.c_str()))return false;disk[b.c_str()]=disk[a.c_str()];disk.erase(a.c_str());return true;}}SD_MMC;
void seed(const char*p,const char*text){disk[p]=std::make_shared<std::string>(text);}
bool pikaDataReadJson(const String&p,JsonDocument&out,size_t){recoverStoredFile(SD_MMC,p);auto f=SD_MMC.open(p);return f&&!deserializeJson(out,*f.data);}
bool pikaDataWriteJson(const String&p,const JsonDocument&doc,bool){String temp=p+".tmp";auto f=SD_MMC.open(temp,FILE_WRITE);if(!f)return false;serializeJson(doc,*f.data);return commitStoredFile(SD_MMC,p,temp);}
bool pikaDataHash(const String&p,String&out,uint32_t*){auto f=SD_MMC.open(p);if(!f)return false;out=String(f.data->c_str());return true;}
bool pikaDataList(const String&,std::vector<PikaDataFile>&out,String&,bool,bool){scans++;for(auto&p:disk)if(p.first.rfind("/notes/",0)==0){PikaDataFile f;f.name=p.first.substr(7).c_str();f.size=p.second->size();f.modified=7;out.push_back(f);}return true;}
struct NoteEntry{int num;};std::vector<NoteEntry> noteIndex;void loadIndex(bool){}bool managerNoteExists(int n){for(auto e:noteIndex)if(e.num==n)return true;return false;}
uint32_t managerRevision(){return revision;}struct ManagerReadSnapshot{};
std::map<int,std::string> memoryText;std::map<int,std::vector<BrainTaskRecord>> rawSources;std::map<std::string,bool> completions;
bool brainApplyTaskOverlays(std::vector<BrainTaskRecord>& tasks){if(overlayFail)return false;for(auto&t:tasks)if(completions.count(t.id.c_str()))t.done=completions[t.id.c_str()];return true;}
std::vector<BrainTaskRecord> brainTaskSourcesForNote(int n){sourceReads++;return rawSources[n];}
void brainPublishTaskCache(const std::vector<BrainTaskRecord>&){published++;}
void brainPublishTaskCache(std::vector<BrainTaskRecord>&&){published++;}
bool pikaPhoneBuildMemoryRecord(JsonObject o,int n){memoryReads++;if(memoryFail||!managerNoteExists(n))return false;o["id"]="m"+String(n);o["text"]=memoryText[n];o["date"]="2026-11-02";o["archived"]=false;return true;}
void pikaPhoneBuildTaskRecord(JsonObject o,const BrainTaskRecord&t){o["id"]=t.id;o["text"]=t.text;o["done"]=t.done;}
void pikaPhoneBuildMeta(JsonDocument&o){o["revision"]=revision;o["settings"]["voice"]="cedar";}
bool pikaHomeButtonsBusy(){return inputBusy;}void pikaError(const char*,const char*,const String&,int=0,uint32_t=0){}bool pikaPhoneOnline(){return true;}void resetActivity(){}
'''
main=r'''
void restart(){catalog.clear();loaded=false;fullScanNeeded=true;dirty=true;intentArmed=false;touched.clear();}
void resetReads(){memoryReads=sourceReads=scans=0;}
int main(){noteIndex={{1},{2}};memoryText[1]="One";memoryText[2]="Two";seed(INDEX_FILE,"1,Inbox,1\\n2,Inbox,1\\n");seed("/notes/note_001.txt","One");seed("/notes/note_002.txt","Two");BrainTaskRecord t;t.id="task-1";t.text="Keep me";t.noteNum=1;rawSources[1]={t};
 assert(pikaPhoneSyncTick());assert(scans==1&&memoryReads==2&&sourceReads==2);assert(!SD_MMC.exists(intentPath));String untouched=digest(catalog["records"]["memories:m2"]);uint32_t first=pikaPhoneSyncGeneration();
 restart();resetReads();assert(pikaPhoneSyncTick());assert(scans==0&&memoryReads==0&&sourceReads==0&&pikaPhoneSyncGeneration()==first);
 assert(pikaPhoneFileWillChange("/notes/note_001.txt"));memoryText[1]="Changed";seed("/notes/note_001.txt","Changed");pikaPhoneFileTouched("/notes/note_001.txt");revision++;resetReads();assert(pikaPhoneSyncTick());assert(scans==0&&memoryReads==1&&sourceReads==1);assert(digest(catalog["records"]["memories:m2"])==untouched);
 auto beforeReadFailure=pikaPhoneSyncGeneration();pikaPhoneFileTouched("/notes/note_001.txt");memoryFail=true;assert(!pikaPhoneSyncTick()&&pikaPhoneSyncGeneration()==beforeReadFailure);memoryFail=false;assert(pikaPhoneSyncTick());overlayFail=true;pikaPhoneSyncInvalidate();beforeReadFailure=pikaPhoneSyncGeneration();assert(!pikaPhoneSyncTick()&&pikaPhoneSyncGeneration()==beforeReadFailure);overlayFail=false;assert(pikaPhoneSyncTick());
 completions["task-1"]=true;revision++;pikaPhoneFileTouched("/notes/manager.json");resetReads();assert(pikaPhoneSyncTick());assert(scans==0&&memoryReads==0&&sourceReads==0);assert(catalog["records"]["tasks:task-1"]["record"]["done"]==true);
 assert(pikaPhoneFileWillChange("/notes/note_002.txt"));memoryText[2]="Crash committed source";seed("/notes/note_002.txt","Crash committed source");restart();resetReads();assert(pikaPhoneSyncTick());assert(scans==1&&catalog["records"]["memories:m2"]["record"]["text"]=="Crash committed source");
 auto previous=pikaPhoneSyncGeneration();assert(pikaPhoneFileWillChange("/notes/note_001.txt"));memoryText[1]="Retry after catalog failure";seed("/notes/note_001.txt","Retry after catalog failure");pikaPhoneFileTouched("/notes/note_001.txt");failWrite=std::string(path)+".tmp";int beforePublish=published;assert(!pikaPhoneSyncTick());assert(pikaPhoneSyncGeneration()==previous&&published==beforePublish&&SD_MMC.exists(intentPath));failWrite="";restart();assert(pikaPhoneSyncTick());assert(catalog["records"]["memories:m1"]["record"]["text"]=="Retry after catalog failure");
 assert(pikaPhoneFileWillChange("/notes/note_001.txt"));memoryText[1]="Rename recovery";pikaPhoneFileTouched("/notes/note_001.txt");failRenameTo=path;previous=pikaPhoneSyncGeneration();assert(!pikaPhoneSyncTick()&&pikaPhoneSyncGeneration()==previous&&SD_MMC.exists(String(path)+".bak"));failRenameTo="";restart();assert(pikaPhoneSyncTick()&&catalog["records"]["memories:m1"]["record"]["text"]=="Rename recovery");memoryText[1]="Retry after catalog failure";pikaPhoneFileTouched("/notes/note_001.txt");assert(pikaPhoneSyncTick());
 assert(pikaPhoneFileWillChange("/notes/manager.json"));revision++;pikaPhoneFileTouched("/notes/manager.json");failRemove=intentPath;assert(!pikaPhoneSyncTick());failRemove="";restart();assert(pikaPhoneSyncTick());assert(!SD_MMC.exists(intentPath)&&catalog["records"].size()==3);
 std::string valid=*disk[path];auto pos=disk[path]->find("Retry after catalog failure");assert(pos!=std::string::npos);disk[path]->replace(pos,5,"BROKE");restart();resetReads();assert(pikaPhoneSyncTick()&&scans==1);assert(catalog["records"]["memories:m1"]["record"]["text"]=="Retry after catalog failure");
 assert(pikaPhoneFileWillChange("/notes/note_002.txt"));noteIndex.pop_back();SD_MMC.remove("/notes/note_002.txt");pikaPhoneFileTouched("/notes/note_002.txt");seed(INDEX_FILE,"1,Inbox,1\\n");resetReads();assert(pikaPhoneSyncTick());assert(scans==0&&catalog["records"]["memories:m2"]["deleted"]==true&&!catalog["records"]["memories:m1"]["deleted"]);
 failWrite=intentPath;seed("/notes/guarded.txt","before");seed("/notes/guarded.txt.tmp","after");assert(!commitStoredFile(SD_MMC,String("/notes/guarded.txt"),String("/notes/guarded.txt.tmp")));assert(*disk["/notes/guarded.txt"]=="before");failWrite="";
 JsonDocument snapshot;assert(pikaPhoneCatalogSnapshot(snapshot)&&snapshot["memories"].size()==1&&snapshot["tasks"].size()==1);assert(snapshot["tasks"][0]["done"]==true);
 puts("PASS: production catalog: cold reuse/no scans, targeted note patch, RAM task overlays, unchanged rows retained, interrupted source/catalog/marker recovery, checksum rejection, deletion tombstones, failed intent blocks source commit");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'Arduino.h').write_text(arduino);(p/'test.cpp').write_text(stubs+production+main);exe=p/'catalog.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-DPIKA_PHONE_MODE=1','-I',str(p),'-I',str(root/'pika_note/src/app'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
