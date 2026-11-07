#include "error_log.h"
#include "home_buttons.h"
#include "phone_inventory.h"
#include "phone_sync.h"
#include "sleep.h"
#include "phone_link.h"
#include "data_files.h"
#include "manager_store.h"
#include "notes.h"
#include "../../globals.h"
#include "../../config.h"
#include <SD_MMC.h>
#include <atomic>
#include <mutex>
#include <set>
#include <mbedtls/sha256.h>
#include <esp_heap_caps.h>
namespace {
const char* path="/.pika-phone-sync.json";
const char* intentPath="/.pika-sync-dirty";
constexpr int schema=2;
class PsramAllocator:public ArduinoJson::Allocator{public:void* allocate(size_t n)override{return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}void deallocate(void* p)override{heap_caps_free(p);}void* reallocate(void* p,size_t n)override{return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}};
// Hash JSON as a stream: never allocate another full library-sized String.
class JsonHash:public Print{mbedtls_sha256_context ctx;public:bool ok=true;JsonHash(){mbedtls_sha256_init(&ctx);ok=mbedtls_sha256_starts(&ctx,0)==0;}~JsonHash(){mbedtls_sha256_free(&ctx);}size_t write(uint8_t b)override{return write(&b,1);}size_t write(const uint8_t* b,size_t n)override{ok=ok&&mbedtls_sha256_update(&ctx,b,n)==0;return ok?n:0;}String finish(){uint8_t bytes[32];if(!ok||mbedtls_sha256_finish(&ctx,bytes))return "";char hex[65];for(int i=0;i<32;i++)sprintf(hex+2*i,"%02x",bytes[i]);hex[64]=0;return hex;}};
String digest(JsonVariantConst value){JsonHash hash;serializeJson(value,hash);return hash.finish();}
PsramAllocator allocator;JsonDocument catalog(&allocator);String lastFailure;uint32_t lastFailureLog=0;bool loaded=false,fullScanNeeded=true;std::atomic<bool> dirty{true},intentArmed{false};
std::mutex touchedMutex;std::set<String> touched;
std::atomic<uint32_t> leaseAt{0},leaseStarted{0};
bool fail(const char* stage){if(String(stage)!="input_pending"&&String(stage)!=lastFailure)pikaError("sync",stage,"Library synchronization failed");dirty=true;lastFailure=stage;if(!lastFailureLog||millis()-lastFailureLog>1000){lastFailureLog=millis();Serial.printf("[PhoneSync] %s heap=%u psram=%u stack=%u\n",stage,ESP.getFreeHeap(),ESP.getFreePsram(),uxTaskGetStackHighWaterMark(nullptr));}return false;}
bool normalFile(const String& name){return pikaPhoneNormalFile(name);}
bool armIntent(){if(intentArmed)return true;if(SD_MMC.exists(intentPath)){intentArmed=true;return true;}File f=SD_MMC.open(intentPath,FILE_WRITE);if(!f)return false;const char text[]="PIKA-SYNC-INTENT-2\n";bool ok=f.write((const uint8_t*)text,sizeof(text)-1)==sizeof(text)-1;f.flush();ok=ok&&f.size()==sizeof(text)-1;f.close();if(ok)intentArmed=true;return ok;}
int noteNumber(const String& name){if(!name.startsWith("note_"))return 0;int dot=name.indexOf('.');if(dot<6)return 0;for(int i=5;i<dot;i++)if(!isdigit(name[i]))return 0;String ext=name.substring(dot+1);if(ext!="txt"&&ext!="meta"&&ext!="brain.json"&&ext!="archived"&&ext!="manual"&&ext!="wav")return 0;return name.substring(5,dot).toInt();}
void sourceJson(JsonObject out,const BrainTaskRecord& t){out["id"]=t.id;out["noteNum"]=t.noteNum;out["text"]=t.text;out["createdUtc"]=t.createdUtc;out["dueUtc"]=t.dueUtc;out["endUtc"]=t.endUtc;out["priority"]=t.priority;out["type"]=t.type;out["datePrecision"]=t.datePrecision;}
BrainTaskRecord sourceTask(JsonObjectConst o){BrainTaskRecord t;for(String* field:{&t.id,&t.text,&t.createdUtc,&t.dueUtc,&t.endUtc,&t.updatedUtc})field->reserve(512);t.id=o["id"]|"";t.noteNum=o["noteNum"]|-1;t.text=o["text"]|"";t.createdUtc=o["createdUtc"]|"";t.dueUtc=o["dueUtc"]|"";t.endUtc=o["endUtc"]|"";t.priority=o["priority"]|1;t.type=o["type"]|"todo";t.datePrecision=o["datePrecision"]|"none";return t;}
void putRecord(JsonObject rows,const String& kind,JsonObjectConst record,uint32_t generation,bool& changed){const String id=record["id"]|"",key=kind+":"+id,hash=digest(record);JsonObject row=rows[key];if(row.isNull()||(row["deleted"]|false)||String(row["hash"]|"")!=hash){row=rows[key].to<JsonObject>();row["kind"]=kind;row["id"]=id;row["hash"]=hash;row["version"]=generation;row["record"].set(record);row.remove("deleted");changed=true;}}
void tombstone(JsonObject row,uint32_t generation,bool& changed){if(!row.isNull()&&!(row["deleted"]|false)){row.remove("record");row.remove("hash");row["deleted"]=true;row["version"]=generation;changed=true;}}
void memoryCounts(JsonDocument& meta,JsonObjectConst rows){meta["memoryDays"].to<JsonObject>();meta["archivedMemoryDays"].to<JsonObject>();unsigned count=0,active=0;for(JsonPairConst p:rows){JsonObjectConst row=p.value();if(String(row["kind"]|"")!="memories"||(row["deleted"]|false))continue;count++;JsonObjectConst m=row["record"];bool archived=m["archived"]|false;if(!archived)active++;String date=m["date"]|"";if(date.length()>=10){JsonObject days=meta[archived?"archivedMemoryDays":"memoryDays"];String day=date.substring(0,10);days[day]=(days[day]|0)+1;}}meta["totalMemories"]=count;meta["activeMemories"]=active;}
}
bool pikaPhoneFileWillChange(const char* value){if(!value||strncmp(value,"/notes",6))return true;String name=strncmp(value,"/notes/",7)?String():String(value+7);if(name.startsWith(".undo-"))return true;if(!armIntent())return false;if(name.length()){std::lock_guard<std::mutex> lock(touchedMutex);touched.insert(name);}dirty=true;return true;}
void pikaPhoneSyncInvalidate(){dirty=true;}
void pikaPhoneSyncForceScan(){fullScanNeeded=true;dirty=true;armIntent();}
void pikaPhoneFileTouched(const char* value){if(!value||strncmp(value,"/notes/",7))return;std::lock_guard<std::mutex> lock(touchedMutex);touched.insert(String(value+7));dirty=true;}
uint32_t pikaPhoneSyncGeneration(){return catalog["generation"]|0UL;}
bool pikaPhoneSyncTick(){
 const uint32_t started=millis();if(pikaHomeButtonsBusy())return fail("input_pending");
 if(!loaded){
  bool valid=pikaDataReadJson(path,catalog)&&catalog["schema"]==schema&&catalog["meta"].is<JsonObject>()&&catalog["records"].is<JsonObject>()&&catalog["sources"].is<JsonObject>()&&catalog["files"].is<JsonObject>()&&!String(catalog["epoch"]|"").isEmpty();
  if(valid){String expected=catalog["checksum"]|"";catalog.remove("checksum");valid=expected.length()==64&&digest(catalog.as<JsonVariantConst>())==expected;catalog["checksum"]=expected;}
  fullScanNeeded=!valid||SD_MMC.exists(intentPath);
  if(valid&&!fullScanNeeded){String indexHash;if(!pikaDataHash(INDEX_FILE,indexHash))return fail("index_read_failed");if(indexHash!=String(catalog["indexHash"]|"")||noteIndex.size()!=unsigned(catalog["meta"]["totalMemories"]|0))fullScanNeeded=true;}
  if(!valid){catalog.clear();catalog["epoch"]=String(esp_random(),HEX)+String(esp_random(),HEX);catalog["generation"]=0;catalog["records"].to<JsonObject>();catalog["files"].to<JsonObject>();catalog["sources"].to<JsonObject>();}
  loaded=true;dirty=true;Serial.printf("PIKA_CATALOG_LOAD valid=%d recovery=%d generation=%lu\n",valid,fullScanNeeded,(unsigned long)pikaPhoneSyncGeneration());
 }
 if(!dirty.exchange(false))return true;
 const bool full=fullScanNeeded;
 if(full)loadIndex(true);
 String indexHash;if(!pikaDataHash(INDEX_FILE,indexHash))return fail("index_read_failed");
 std::lock_guard<std::mutex> lock(touchedMutex);
 // This marker precedes all canonical file commits. Only clear it after the
 // new checksummed catalog is durably replaced; a crash forces reconciliation.
 if(!armIntent())return fail("intent_write_failed");
 JsonDocument next(&allocator);if(!next.set(catalog))return fail("catalog_copy_failed");next.remove("checksum");
 const uint32_t generation=pikaPhoneSyncGeneration()+1;bool changed=false;JsonObject rows=next["records"];JsonObject sources=full?next["sources"].to<JsonObject>():next["sources"].as<JsonObject>();
 std::set<int> notes;for(const auto& name:touched){int n=noteNumber(name);if(n>0)notes.insert(n);}
 // Membership is a compact in-memory CSV index, never a directory traversal.
 for(const auto& note:noteIndex)if(full||rows["memories:m"+String(note.num)].isNull()||(rows["memories:m"+String(note.num)]["deleted"]|false))notes.insert(note.num);
 for(JsonPair pair:rows){JsonObject row=pair.value();if(String(row["kind"]|"")=="memories"&&!(row["deleted"]|false)){String id=row["id"]|"";int n=id.substring(1).toInt();if(!managerNoteExists(n))notes.insert(n);}}
 ManagerReadSnapshot readScope;
 for(int n:notes){if(pikaHomeButtonsBusy())return fail("input_pending");const String key="memories:m"+String(n),sourceKey=String(n);
  if(!managerNoteExists(n)){tombstone(rows[key],generation,changed);sources.remove(sourceKey);continue;}
  JsonDocument memory(&allocator);if(!pikaPhoneBuildMemoryRecord(memory.to<JsonObject>(),n)||memory.overflowed())return fail("memory_read_failed");
  putRecord(rows,"memories",memory.as<JsonObjectConst>(),generation,changed);
  auto raw=brainTaskSourcesForNote(n);JsonArray saved=sources[sourceKey].to<JsonArray>();for(const auto& task:raw)sourceJson(saved.add<JsonObject>(),task);
 }
 // No SD reads per task: sources come from the persisted catalog. Read just
 // the small completion and manager overlays once for the entire transaction.
 std::vector<BrainTaskRecord> tasks;for(JsonPair pair:sources)for(JsonObjectConst item:pair.value().as<JsonArrayConst>())tasks.push_back(sourceTask(item));
 if(!brainApplyTaskOverlays(tasks))return fail("task_overlay_failed");
 // Short task strings otherwise consume scarce internal RAM a second time.
 // Existing large strings are already external; reserve moves short strings
 // to PSRAM before SD opens, JSON commits and other radio tasks need RAM.
 for(auto& task:tasks)for(String* field:{&task.id,&task.text,&task.createdUtc,&task.dueUtc,&task.endUtc,&task.updatedUtc})if(field->length()&&!field->reserve(512))return fail("task_allocation_failed");
 std::set<String> taskKeys;
 for(const auto& task:tasks){JsonDocument record(&allocator);pikaPhoneBuildTaskRecord(record.to<JsonObject>(),task);if(record.overflowed())return fail("task_allocation_failed");String kind=task.type=="event"?"events":"tasks";taskKeys.insert(kind+":"+task.id);putRecord(rows,kind,record.as<JsonObjectConst>(),generation,changed);}
 for(JsonPair pair:rows){JsonObject row=pair.value();String kind=row["kind"]|"";if((kind=="tasks"||kind=="events")&&!taskKeys.count(String(pair.key().c_str())))tombstone(row,generation,changed);}
 JsonDocument meta(&allocator);pikaPhoneBuildMeta(meta);memoryCounts(meta,rows);JsonDocument stable(&allocator);if(meta.overflowed()||!stable.set(meta))return fail("metadata_allocation_failed");stable["settings"].remove("googleStatus");stable["settings"].remove("mailStatus");for(JsonObject n:stable["settings"]["networks"].as<JsonArray>())n.remove("connected");
 String hash=digest(stable.as<JsonVariantConst>());if(String(next["metaHash"]|"")!=hash){next["meta"].set(meta);next["metaHash"]=hash;next["metaVersion"]=generation;changed=true;}
 std::vector<PikaDataFile> files;String error;
 if(full){if(!pikaDataList("/notes",files,error,false,true))return fail("file_list_failed");}
 else for(const auto& name:touched){if(!normalFile(name))continue;File file=SD_MMC.open("/notes/"+name);if(!file){if(SD_MMC.exists("/notes/"+name))return fail("file_stat_failed");continue;}if(!file.isDirectory()){PikaDataFile item;item.name=name;item.size=file.size();item.modified=file.getLastWrite();files.push_back(std::move(item));}file.close();}
 changed=pikaUpdatePhoneInventory(next["files"].as<JsonObject>(),files,touched,full,generation)||changed;
 next["schema"]=schema;if(String(next["indexHash"]|"")!=indexHash){next["indexHash"]=indexHash;changed=true;}if(full||!notes.empty())changed=true;
 if(changed){next["generation"]=generation;if(next.overflowed())return fail("catalog_allocation_failed");String checksum=digest(next.as<JsonVariantConst>());if(checksum.length()!=64)return fail("checksum_failed");next["checksum"]=checksum;if(pikaHomeButtonsBusy())return fail("input_pending");if(!pikaDataWriteJson(path,next,true))return fail("catalog_commit_failed");if(!catalog.set(next))return fail("catalog_publish_failed");}
 // Publish caches only after a complete durable commit. A failed transaction
 // retains the previous generation; retries cannot erase untouched records.
 const size_t taskCount=tasks.size();brainPublishTaskCache(std::move(tasks));touched.clear();fullScanNeeded=false;lastFailure="";
 if(!dirty.load()){if(!SD_MMC.remove(intentPath)&&SD_MMC.exists(intentPath))return fail("intent_clear_failed");intentArmed=false;}
 Serial.printf("PIKA_CATALOG_UPDATE full=%d notes=%u task_sources=%u files=%u changed=%d ms=%lu\n",full,unsigned(notes.size()),unsigned(taskCount),unsigned(files.size()),changed,(unsigned long)(millis()-started));return true;
}
bool pikaPhoneCatalogSnapshot(JsonDocument& out){if(!pikaPhoneSyncTick())return false;out.set(catalog["meta"]);for(const char* kind:{"memories","tasks","events"})out[kind].to<JsonArray>();for(JsonPairConst pair:catalog["records"].as<JsonObjectConst>()){JsonObjectConst row=pair.value();if(row["deleted"]|false)continue;String kind=row["kind"]|"";if(kind=="memories"||kind=="tasks"||kind=="events")out[kind].as<JsonArray>().add(row["record"]);}out["revision"]=managerRevision();return !out.overflowed();}
void pikaPhoneSyncStatus(JsonDocument& out){if(!pikaPhoneSyncTick()){out["error"]=lastFailure.length()?lastFailure:String("Cannot read durable change journal");return;}out["protocol"]=4;out["epoch"]=catalog["epoch"];out["generation"]=catalog["generation"];out["revision"]=managerRevision();}
void pikaPhoneSyncChanges(JsonDocument& out,uint32_t since,const String& epoch,bool full){
 if(!pikaPhoneSyncTick()){out["error"]=lastFailure.length()?lastFailure:String("Cannot read durable change journal");return;}
 full=full||epoch!=String(catalog["epoch"]|"")||since>pikaPhoneSyncGeneration();out["protocol"]=4;out["epoch"]=catalog["epoch"];out["generation"]=catalog["generation"];out["revision"]=managerRevision();out["full"]=full;
 if(full||uint32_t(catalog["metaVersion"]|0UL)>since)out["meta"].set(catalog["meta"]);
 JsonArray records=out["records"].to<JsonArray>();for(JsonPairConst pair:catalog["records"].as<JsonObjectConst>()){JsonObjectConst row=pair.value();if(full||uint32_t(row["version"]|0UL)>since){if(full&&(row["deleted"]|false))continue;records.add(row);}}
 JsonArray files=out["files"].to<JsonArray>();for(JsonPairConst pair:catalog["files"].as<JsonObjectConst>()){JsonObjectConst file=pair.value();if(full||uint32_t(file["version"]|0UL)>since){if(full&&(file["deleted"]|false))continue;JsonObject item=files.add<JsonObject>();item.set(file);item["stamp"]=String(catalog["epoch"]|"")+":"+String(file["version"]|0UL);}}
}
void pikaPhoneSyncRecord(JsonDocument& ack,const String& kind,const String& id){if(!pikaPhoneSyncTick())return;const String key=kind+":"+id;JsonObjectConst row=catalog["records"][key];if(!row.isNull())ack["change"].set(row);ack["revision"]=managerRevision();}
void pikaPhoneSyncLease(bool active){if(!active){leaseAt=leaseStarted=0;return;}leaseStarted=millis();leaseAt=leaseStarted.load();}
void pikaPhoneSyncProgress(){if(leaseStarted)leaseAt=millis();}
bool pikaPhoneSyncActive(){return pikaPhoneOnline()&&leaseStarted&&millis()-leaseAt<15000UL;}

int pikaPhoneCachedMemoryCount(){return loaded&&!dirty.load()&&!fullScanNeeded?int(catalog["meta"]["activeMemories"]|0):-1;}

void pikaPhoneCatalogPrintStatus(){Serial.printf("PIKA_CATALOG_STATUS loaded=%d dirty=%d full=%d generation=%lu records=%u files=%u active=%d\n",loaded,dirty.load(),fullScanNeeded,(unsigned long)pikaPhoneSyncGeneration(),unsigned(catalog["records"].size()),unsigned(catalog["files"].size()),pikaPhoneCachedMemoryCount());}
void pikaPhoneCatalogBenchmark(){if(!pikaPhoneSyncTick())return;uint32_t at=millis();pikaPhoneSyncInvalidate();bool meta=pikaPhoneSyncTick();Serial.printf("PIKA_CATALOG_BENCH kind=metadata ok=%d ms=%lu\n",meta,(unsigned long)(millis()-at));if(!noteIndex.empty()){char path[80];snprintf(path,sizeof(path),"/notes/note_%03d.txt",noteIndex.back().num);at=millis();pikaPhoneFileTouched(path);bool note=pikaPhoneSyncTick();Serial.printf("PIKA_CATALOG_BENCH kind=one-note ok=%d ms=%lu\n",note,(unsigned long)(millis()-at));}}
