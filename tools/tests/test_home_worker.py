from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
source=(root/'pika_note/src/app/home_buttons.cpp').read_text(encoding='utf-8').replace('#include "../../config.h"','')
stubs=r'''
#pragma once
#include <cstdint>
#include <deque>
#include <vector>
#include <cstring>
#include <functional>
#include <cassert>
struct {template<class... T>void printf(const char*,T...){} } Serial;
constexpr int LOW=0,HIGH=1,BTN_REC=0,BTN_PWR=18,pdTRUE=1,pdPASS=1;
constexpr uint32_t HOME_TAP_DEBOUNCE_MS=18,HOME_TAP_GAP_MS=480,REC_HOLD_MS=320,COMPANION_HOLD_MS=3000;
struct Queue{size_t size;std::deque<std::vector<uint8_t>> entries;};using QueueHandle_t=Queue*;
inline QueueHandle_t xQueueCreate(int,size_t size){return new Queue{size,{}};}
inline int xQueueSend(QueueHandle_t q,const void* item,int){std::vector<uint8_t> bytes(q->size);memcpy(bytes.data(),item,q->size);q->entries.push_back(bytes);return 1;}
inline int xQueueReceive(QueueHandle_t q,void* item,int){if(q->entries.empty())return 0;memcpy(item,q->entries.front().data(),q->size);q->entries.pop_front();return 1;}
inline unsigned uxQueueMessagesWaiting(QueueHandle_t q){return q->entries.size();}
inline void xQueueReset(QueueHandle_t q){q->entries.clear();}
inline void(*worker)(void*)=nullptr;
inline int xTaskCreate(void(*fn)(void*),const char*,int,void*,int,void*){worker=fn;return pdPASS;}
inline uint32_t now=0,stopAt=1800;inline std::function<void(uint32_t)> hook;
struct Stop{};
struct Pulse{int pin;uint32_t from,to;};inline std::vector<Pulse> pulses;
inline uint32_t millis(){return now;}
inline int digitalRead(int pin){for(const auto& p:pulses)if(p.pin==pin&&now>=p.from&&now<p.to)return LOW;return HIGH;}
inline int pdMS_TO_TICKS(int value){return value;}
inline void vTaskDelay(uint32_t ms){now+=ms;if(hook)hook(now);if(now>=stopAt)throw Stop{};}
'''
main=r'''
#include <iostream>
void pikaActivityLedHomeReady(){}
void simulate(std::vector<Pulse> input,uint32_t resetAt,uint32_t enableAt,int expected){
 pikaHomeButtonsReset();assert(pikaHomeButtonsBegin());pulses=input;now=0;stopAt=1800;pikaHomeButtonsEnable(true);
 hook=[=](uint32_t t){if(t==resetAt)pikaHomeButtonsReset();if(t==enableAt)pikaHomeButtonsEnable(true);};
 try{worker(nullptr);}catch(const Stop&){}
 int slot=-1;assert(pikaHomeButtonsTake(slot)&&slot==expected);assert(!pikaHomeButtonsTake(slot));
}
int main(){
 // A pending single from the previous screen must not combine with a new triple.
 simulate({{0,50,100},{0,300,350},{0,450,500},{0,600,650}},200,210,3);
 // An already queued single is discarded before new double-click recognition.
 simulate({{0,50,100},{0,800,850},{0,1000,1050}},700,710,1);
 // Holding a button across Home entry cannot become a new hold/action.
 simulate({{18,50,800},{18,900,950}},400,410,4);
 // Lower triples have the same clean boundary.
 simulate({{18,50,100},{18,300,350},{18,450,500},{18,600,650}},200,210,7);
 // A delayed old hold must never start an empty recording after release.
 pikaHomeButtonsReset();assert(pikaHomeButtonsBegin());pulses={{18,50,900}};now=0;stopAt=1200;hook=nullptr;pikaHomeButtonsEnable(true);
 try{worker(nullptr);}catch(const Stop&){}int slot=-1;assert(!pikaHomeButtonsTake(slot));
 // A live hold is delivered while the key is still down, on the first attempt.
 pikaHomeButtonsReset();assert(pikaHomeButtonsBegin());pulses={{18,50,1000}};now=0;stopAt=500;pikaHomeButtonsEnable(true);
 try{worker(nullptr);}catch(const Stop&){}assert(pikaHomeButtonsTake(slot)&&slot==6);assert(pikaHomeButtonsDispatchDelay()<200);
 assert(pikaHomeButtonsForegroundGrace());now=4000;assert(!pikaHomeButtonsForegroundGrace());pikaHomeButtonsReset();assert(!pikaHomeButtonsForegroundGrace());pikaHomeButtonsEnable(true);pulses={};stopAt=4300;try{worker(nullptr);}catch(const Stop&){}assert(pikaHomeButtonsForegroundGrace());
 // Reader-screen presses remain pending after release while the owner loop is busy.
 for(int pin:{BTN_REC,BTN_PWR}){pikaHomeButtonsReset();assert(pikaHomeButtonsBegin());pulses={{pin,50,100}};now=0;stopAt=500;hook=nullptr;pikaScreenButtonsEnable(true);try{worker(nullptr);}catch(const Stop&){}const unsigned mask=pin==BTN_REC?1:2;assert(pikaScreenButtonsPending()==mask);assert(pikaHomeButtonsBusy());assert(pikaScreenButtonsTake()==mask);assert(pikaScreenButtonsTake()==0);pikaHomeButtonsEnable(true);assert(pikaScreenButtonsPending()==0);}
 pikaHomeButtonsReset();assert(pikaHomeButtonsBegin());pulses={{BTN_PWR,50,55}};now=0;stopAt=200;pikaScreenButtonsEnable(true);try{worker(nullptr);}catch(const Stop&){}assert(pikaScreenButtonsTake()==0);pikaScreenButtonsEnable(false);
 std::cout<<"Production Home worker: partial/queued clicks and held buttons discarded; fresh upper/lower triple and double recognized; short screen presses retained across blocked owner work; bounce rejected: PASS\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'Arduino.h').write_text(stubs,encoding='utf-8');(p/'test.cpp').write_text(source+main,encoding='utf-8');exe=p/'home-worker.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-I',str(p),'-I',str(root/'pika_note/src/app'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
