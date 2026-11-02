from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
s=(root/'pika_note/src/app/ask_stage.cpp').read_text(encoding='utf-8')
stubs=r'''
#pragma once
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <cstdlib>
constexpr int pdTRUE=1,pdPASS=1,portMAX_DELAY=-1;
struct Queue{size_t itemSize;std::mutex mutex;std::condition_variable cv;std::deque<std::vector<char>> items;};using QueueHandle_t=Queue*;
struct Semaphore{std::mutex mutex;std::condition_variable cv;bool ready=false;};using SemaphoreHandle_t=Semaphore*;
inline bool failTask=false,failSemaphore=false;inline unsigned tasks=0;
inline QueueHandle_t xQueueCreate(int,size_t n){auto* q=new Queue;q->itemSize=n;return q;}
inline void vQueueDelete(QueueHandle_t q){delete q;}
inline int xQueueSend(QueueHandle_t q,const void* item,int){std::lock_guard<std::mutex> lock(q->mutex);std::vector<char> bytes(q->itemSize);memcpy(bytes.data(),item,bytes.size());q->items.push_back(bytes);q->cv.notify_one();return 1;}
inline int xQueueReceive(QueueHandle_t q,void* item,int){std::unique_lock<std::mutex> lock(q->mutex);q->cv.wait(lock,[&]{return !q->items.empty();});memcpy(item,q->items.front().data(),q->itemSize);q->items.pop_front();return 1;}
inline int xTaskCreate(void(*f)(void*),const char*,unsigned size,void*,int,void*){assert(size==32768);if(failTask)return 0;++tasks;std::thread([=]{f(nullptr);}).detach();return 1;}
inline SemaphoreHandle_t xSemaphoreCreateBinary(){return failSemaphore?nullptr:new Semaphore;}
inline void vSemaphoreDelete(SemaphoreHandle_t s){delete s;}
inline int xSemaphoreTake(SemaphoreHandle_t s,int){std::unique_lock<std::mutex> lock(s->mutex);s->cv.wait(lock,[&]{return s->ready;});return 1;}
inline void xSemaphoreGive(SemaphoreHandle_t s){std::lock_guard<std::mutex> lock(s->mutex);s->ready=true;s->cv.notify_one();}
inline unsigned uxTaskGetStackHighWaterMark(void*){return 16000;}
inline int pdMS_TO_TICKS(int n){return n;}inline void delay(int){}
struct {template<class... T>void printf(const char*,T...){} } Serial;
'''
main=r'''
int main(){
 const auto caller=std::this_thread::get_id();int count=0;
 auto callback=[](void* c){++*static_cast<int*>(c);};
 failTask=true;assert(!pikaRunAskStage(callback,&count));assert(count==0);failTask=false;
 assert(pikaAskStagesBegin());assert(tasks==1);failTask=true; // Warm worker needs no task allocation after heap fragmentation.
 failSemaphore=true;assert(!pikaRunAskStage(callback,&count));assert(count==0);failSemaphore=false;
 struct Context{std::thread::id caller;int count=0;} ctx{caller};
 for(int i=0;i<20;++i){assert(pikaRunAskStage([](void* p){auto& c=*static_cast<Context*>(p);assert(std::this_thread::get_id()!=c.caller);++c.count;},&ctx));assert(ctx.count==i+1);}
 assert(tasks==1);puts("Production Ask stage queue: dedicated reusable worker, sequential completion, task/semaphore failures leave work untouched: PASS");fflush(stdout);std::_Exit(0);
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'Arduino.h').write_text(stubs,encoding='utf-8');(p/'freertos').mkdir()
 for header in ['FreeRTOS.h','queue.h','semphr.h']:(p/'freertos'/header).write_text('#include "Arduino.h"')
 (p/'test.cpp').write_text(s+main,encoding='utf-8');exe=p/'ask-worker.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-I',str(p),'-I',str(root/'pika_note/src/app'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True,timeout=15)

setup=(root/"pika_note/pika_note.ino").read_text(encoding="utf-8").split("void setup() {",1)[1]
assert setup.index("pikaAskStagesBegin();")<setup.index("pikaPhoneBegin();")<setup.index("audio_bsp_init();")
