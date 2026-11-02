#include "device_extras.h"
#include "activity_led.h"
#include "home_buttons.h"
#include <Arduino.h>
#include <atomic>
#include "../../config.h"
namespace {
std::atomic<bool> screenEnabled{false};std::atomic<unsigned> screenGeneration{0},screenPresses{0};
std::atomic<bool> enabled{false},busy{false},inputReady{false};
std::atomic<unsigned> generation{0};
std::atomic<uint32_t> dispatchDelay{0},readyAt{0};
struct Event{int slot;unsigned generation;uint32_t at;};
std::atomic<uint32_t> firstPressAt{0},lastQueuedAt{0},lastTakenAt{0};
QueueHandle_t queue=nullptr;
struct Button{int pin;bool raw=false,down=false,held=false;uint32_t rawAt=0,pressed=0,released=0;int count=0;};
void emit(int slot,unsigned epoch){lastQueuedAt=millis();Serial.printf("PIKA_HOME_QUEUED slot=%d at=%lu\n",slot,(unsigned long)millis());Event e{slot,epoch,millis()};xQueueSend(queue,&e,0);}
void run(void*){
 unsigned screenEpoch=~0u;Button screenButtons[2]={{BTN_REC},{BTN_PWR}};
 unsigned epoch=~0u;Button buttons[2]={{BTN_REC},{BTN_PWR}};bool armed=false,chord=false,sent=false;uint32_t upAt=0,chordAt=0;
 for(;;){const uint32_t now=millis();const unsigned current=generation.load();
  const unsigned simpleGeneration=screenGeneration.load();
  if(simpleGeneration!=screenEpoch){screenEpoch=simpleGeneration;screenButtons[0]=Button{BTN_REC};screenButtons[1]=Button{BTN_PWR};}
  if(screenEnabled.load())for(int i=0;i<2;++i){auto& b=screenButtons[i];const bool raw=digitalRead(b.pin)==LOW;if(raw!=b.raw){b.raw=raw;b.rawAt=now;}if(b.raw!=b.down&&now-b.rawAt>=HOME_TAP_DEBOUNCE_MS){b.down=b.raw;if(b.down){screenPresses.fetch_or(1u<<i);Serial.printf("PIKA_SCREEN_QUEUED key=%s at=%lu\n",i?"lower":"upper",(unsigned long)now);}}}

  if(!enabled.load()||current!=epoch){epoch=current;buttons[0]=Button{BTN_REC};buttons[1]=Button{BTN_PWR};armed=false;chord=false;sent=false;upAt=0;busy=false;inputReady=false;}
  if(enabled.load()){
   const bool upper=digitalRead(BTN_REC)==LOW,lower=digitalRead(BTN_PWR)==LOW;
   if((upper||lower)&&!firstPressAt.load())firstPressAt=now;
   if(!armed){if(upper||lower)upAt=0;else if(!upAt)upAt=now;else if(now-upAt>=HOME_TAP_DEBOUNCE_MS){armed=true;readyAt=now;inputReady=true;Serial.printf("PIKA_HOME_ARMED at=%lu\n",(unsigned long)now);pikaActivityLedHomeReady();pikaVibrationWakeReady();}}
   else if(upper&&lower){if(!chord){chord=true;chordAt=now;sent=false;for(auto& b:buttons)b.count=0;}busy=true;if(!sent&&now-chordAt>=COMPANION_HOLD_MS){emit(8,epoch);sent=true;}}
   else if(chord){busy=true;if(!upper&&!lower){chord=false;armed=false;inputReady=false;upAt=now;for(auto& b:buttons)b=Button{b.pin};}}
   else {bool pending=false;for(int i=0;i<2;++i){auto& b=buttons[i];const bool raw=i==0?upper:lower;
    if(raw!=b.raw){b.raw=raw;b.rawAt=now;}
    if(b.raw!=b.down&&now-b.rawAt>=HOME_TAP_DEBOUNCE_MS){b.down=b.raw;if(b.down){b.pressed=now;b.held=false;}else{b.released=now;if(!b.held){++b.count;if(b.count==3){emit(i*4+3,epoch);b.count=0;}}}}
    if(b.down&&!b.held&&now-b.pressed>=REC_HOLD_MS){b.held=true;b.count=0;emit(i*4+2,epoch);}
    if(!b.down&&!b.raw&&b.count&&now-b.released>=HOME_TAP_GAP_MS){emit(i*4+(b.count==2?1:0),epoch);b.count=0;}
    pending|=b.raw||b.down||b.count;
   }busy=pending;}
  }vTaskDelay(pdMS_TO_TICKS(1));
 }
}
}
bool pikaHomeButtonsBegin(){queue=xQueueCreate(16,sizeof(Event));return queue&&xTaskCreate(run,"home-buttons",3072,nullptr,3,nullptr)==pdPASS;}
void pikaHomeButtonsEnable(bool value){if(value)pikaScreenButtonsEnable(false);if(enabled.exchange(value)!=value){inputReady=false;generation.fetch_add(1);}}
bool pikaHomeButtonsBusy(){return (screenEnabled.load()&&(screenPresses.load()||digitalRead(BTN_REC)==LOW||digitalRead(BTN_PWR)==LOW))||(enabled.load()&&(!inputReady.load()||digitalRead(BTN_REC)==LOW||digitalRead(BTN_PWR)==LOW))||busy.load()||(queue&&uxQueueMessagesWaiting(queue)>0);}
bool pikaHomeButtonsTake(int& slot){Event e;while(queue&&xQueueReceive(queue,&e,0)==pdTRUE){if(enabled.load()&&e.generation==generation.load()){if((e.slot==2||e.slot==6)&&digitalRead(e.slot==2?BTN_REC:BTN_PWR)!=LOW){Serial.printf("PIKA_HOME_DROPPED slot=%d age_ms=%lu\n",e.slot,(unsigned long)(millis()-e.at));continue;}lastTakenAt=millis();dispatchDelay=millis()-e.at;slot=e.slot;return true;}}return false;}

void pikaHomeButtonsReset(){pikaScreenButtonsEnable(false);enabled=false;generation.fetch_add(1);if(queue)xQueueReset(queue);busy=false;inputReady=false;}

uint32_t pikaHomeButtonsDispatchDelay(){return dispatchDelay.load();}

void pikaScreenButtonsEnable(bool value){if(screenEnabled.exchange(value)!=value){screenPresses=0;screenGeneration.fetch_add(1);}}
unsigned pikaScreenButtonsPending(){return screenEnabled.load()?screenPresses.load():0;}
unsigned pikaScreenButtonsTake(){return screenEnabled.load()?screenPresses.exchange(0):0;}

bool pikaHomeButtonsForegroundGrace(){return enabled.load()&&inputReady.load()&&millis()-readyAt.load()<3500UL;}

void pikaHomeButtonsPrintTrace(){Serial.printf("PIKA_HOME_TRACE now=%lu ready=%lu press=%lu queued=%lu taken=%lu enabled=%d armed=%d raw=%d/%d\n",(unsigned long)millis(),(unsigned long)readyAt.load(),(unsigned long)firstPressAt.load(),(unsigned long)lastQueuedAt.load(),(unsigned long)lastTakenAt.load(),int(enabled.load()),int(inputReady.load()),digitalRead(BTN_REC),digitalRead(BTN_PWR));}
