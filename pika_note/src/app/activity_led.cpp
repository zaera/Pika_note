#include "activity_led.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <atomic>
#include <Preferences.h>
#include <driver/gpio.h>
namespace {
constexpr gpio_num_t kActivityLed=GPIO_NUM_3; // Green die of L2; active LOW. Red die belongs to charger STAT.
Preferences preferences;std::atomic<bool> enabled{false},companion{false},ready{false},sleeping{false},wakePulse{false},wakeActive{false},traffic{false};std::atomic<unsigned> connections{0},recordings{0},operations{0};std::atomic<unsigned> wakePhase{0};esp_timer_handle_t wakeTimer=nullptr,trafficTimer=nullptr;std::atomic<int64_t> trafficUntil{0};
void update(){if(ready)digitalWrite(kActivityLed,enabled&&!sleeping&&(traffic||recordings||(wakePulse&&wakeActive))?LOW:HIGH);}
void finishTraffic(void*){if(traffic&&esp_timer_get_time()>=trafficUntil){traffic=false;update();}}
void finishWake(void*){if(!wakeActive)return;unsigned phase=++wakePhase;wakePulse=phase>=4||phase%2==0;if(phase>=4&&wakeTimer)esp_timer_stop(wakeTimer);update();}
}
void pikaActivityLedBegin(){gpio_set_level(kActivityLed,1);gpio_hold_dis(kActivityLed);pinMode(kActivityLed,OUTPUT);digitalWrite(kActivityLed,HIGH);preferences.begin("pikaLed",false);enabled=preferences.getBool("enabled",false);ready=true;sleeping=false;esp_timer_create_args_t args={};args.callback=finishWake;args.name="wake-led";esp_timer_create(&args,&wakeTimer);esp_timer_create_args_t trafficArgs={};trafficArgs.callback=finishTraffic;trafficArgs.name="ble-led";esp_timer_create(&trafficArgs,&trafficTimer);esp_timer_start_periodic(trafficTimer,50000);update();}
bool pikaActivityLedEnabled(){return enabled;}
bool pikaActivityLedSave(bool value){if(!preferences.putBool("enabled",value))return false;enabled=value;update();return true;}
void pikaActivityLedConnection(bool active){if(active)++connections;else if(connections)--connections;update();}
void pikaActivityLedCompanion(bool active){companion=active;update();}
void pikaActivityLedSleep(){if(trafficTimer)esp_timer_stop(trafficTimer);if(wakeTimer)esp_timer_stop(wakeTimer);wakeActive=false;wakePulse=false;sleeping=true;update();gpio_hold_en(kActivityLed);}

void pikaActivityLedRecording(bool active){if(active)++recordings;else if(recordings)--recordings;update();}
void pikaActivityLedWake(){
 if(!ready||!enabled||sleeping||!wakeTimer)return;
 esp_timer_stop(wakeTimer);wakePhase=0;wakeActive=true;wakePulse=true;update();
 // Two short blinks, then stay on until the Home input task is armed.
 esp_timer_start_periodic(wakeTimer,75000);
}
void pikaActivityLedHomeReady(){wakeActive=false;if(wakeTimer)esp_timer_stop(wakeTimer);wakePulse=false;update();}

void pikaActivityLedBusy(bool active){if(active)++operations;else if(operations)--operations;update();}

void pikaActivityLedTraffic(bool active){if(active)trafficUntil=esp_timer_get_time()+180000;if(traffic.exchange(active)!=active)update();}
