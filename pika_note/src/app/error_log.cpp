#include "error_log.h"
#include <SD_MMC.h>
#include <esp_system.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <esp_heap_caps.h>
namespace {
constexpr unsigned capacity=200;
struct Row {uint32_t magic,sequence,uptime,heap,duration;int32_t code;int64_t utc;char area[32],stage[64],reason[380];uint32_t checksum;};
static_assert(sizeof(Row)==512,"Fixed diagnostic slot");
StaticQueue_t queueStorage;std::mutex queueMutex;QueueHandle_t queue=nullptr;portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;uint32_t dropped=0,sequence=0;bool loaded=false;
const char* path="/.pika-errors.bin";
uint32_t checksum(const Row& r){uint32_t n=2166136261u;const auto* b=reinterpret_cast<const uint8_t*>(&r);for(size_t i=0;i<offsetof(Row,checksum);i++)n=(n^b[i])*16777619u;return n;}
void text(char* target,size_t size,const String& input){String s=input;for(const char* secret:{"sk-","Bearer ","ya29.","1//","-----BEGIN","access_token","refresh_token","client_secret","private_key","password","newPin"})if(s.indexOf(secret)>=0){s="Sensitive details omitted";break;}s.replace("\n"," ");s.replace("\r"," ");s.toCharArray(target,size);}
struct Index {uint32_t sequence=0;unsigned slot=0;};struct IndexList {std::array<Index,capacity> rows{};unsigned count=0;};
IndexList read(){IndexList list;File f=SD_MMC.open(path,FILE_READ);if(f){for(unsigned i=0;i<capacity;i++){Row r{};if(f.read(reinterpret_cast<uint8_t*>(&r),sizeof(r))!=sizeof(r))break;if(r.magic==0x50455252&&r.checksum==checksum(r)){list.rows[list.count++]={r.sequence,i};sequence=std::max(sequence,r.sequence);}}f.close();}std::sort(list.rows.begin(),list.rows.begin()+list.count,[](const Index& a,const Index& b){return a.sequence>b.sequence;});loaded=true;return list;}

}
void pikaError(const char* area,const char* stage,const String& reason,int code,uint32_t duration){
 if(!queue){std::lock_guard<std::mutex> lock(queueMutex);if(!queue){auto* storage=(uint8_t*)heap_caps_malloc(capacity*sizeof(Row),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(storage)queue=xQueueCreateStatic(capacity,sizeof(Row),storage,&queueStorage);}}if(!queue)return;
 Row r{};r.magic=0x50455252;r.utc=time(nullptr);if(r.utc<1700000000)r.utc=0;r.uptime=millis();r.heap=ESP.getFreeHeap();r.code=code;r.duration=duration;text(r.area,sizeof(r.area),area?area:"device");text(r.stage,sizeof(r.stage),stage?stage:"unknown");text(r.reason,sizeof(r.reason),reason);
 if(xQueueSend(queue,&r,0)!=pdTRUE){Row old;xQueueReceive(queue,&old,0);xQueueSend(queue,&r,0);portENTER_CRITICAL(&mux);++dropped;portEXIT_CRITICAL(&mux);}
}
void pikaErrorService(){static bool pending=false;static Row r{};if(!pending&&(!queue||xQueueReceive(queue,&r,0)!=pdTRUE))return;pending=true;if(!loaded)read();File f=SD_MMC.open(path,SD_MMC.exists(path)?"r+":"w+");if(!f)return;r.sequence=sequence+1;r.checksum=checksum(r);const bool ok=f.seek(((r.sequence-1)%capacity)*sizeof(Row))&&f.write(reinterpret_cast<uint8_t*>(&r),sizeof(r))==sizeof(r);f.flush();f.close();if(ok){sequence=r.sequence;pending=false;}}
void pikaErrorRead(JsonDocument& out,unsigned offset,unsigned limit){for(unsigned i=0;i<=capacity;i++)pikaErrorService();auto rows=read();out["capacity"]=capacity;out["total"]=rows.count;out["pending"]=queue?uxQueueMessagesWaiting(queue):0;out["dropped"]=dropped;auto a=out["errors"].to<JsonArray>();limit=std::min(limit,24u);File f=SD_MMC.open(path,FILE_READ);if(f)for(unsigned i=offset;i<rows.count&&i<offset+limit;i++){Row r{};if(!f.seek(rows.rows[i].slot*sizeof(Row))||f.read(reinterpret_cast<uint8_t*>(&r),sizeof(r))!=sizeof(r)||r.checksum!=checksum(r))continue;r.area[sizeof(r.area)-1]=0;r.stage[sizeof(r.stage)-1]=0;r.reason[sizeof(r.reason)-1]=0;auto o=a.add<JsonObject>();o["id"]=r.sequence;o["utc"]=r.utc;o["uptimeMs"]=r.uptime;o["area"]=r.area;o["stage"]=r.stage;o["reason"]=r.reason;o["code"]=r.code;o["durationMs"]=r.duration;o["freeHeap"]=r.heap;}if(f)f.close();out["next"]=offset+limit<rows.count?int(offset+limit):-1;}
