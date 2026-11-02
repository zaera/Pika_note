from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'driver').mkdir()
 (p/'Arduino.h').write_text('''#pragma once
inline constexpr int HIGH=1,LOW=0,OUTPUT=1;
inline int ledLevel=HIGH;
inline void pinMode(int,int){} inline void digitalWrite(int,int value){ledLevel=value;}
''')
 (p/'Preferences.h').write_text('''#pragma once
struct Preferences{void begin(const char*,bool){} bool getBool(const char*,bool){return true;} unsigned putBool(const char*,bool){return 1;}};
''')
 (p/'driver/gpio.h').write_text('''#pragma once
using gpio_num_t=int;inline constexpr int GPIO_NUM_3=3;
inline void gpio_set_level(int,int){} inline void gpio_hold_dis(int){} inline void gpio_hold_en(int){}
''')
 (p/'esp_timer.h').write_text('''#pragma once
#include <cstdint>
struct esp_timer_create_args_t{void(*callback)(void*)=nullptr;const char* name=nullptr;};
struct Timer{void(*callback)(void*)=nullptr;bool running=false;};using esp_timer_handle_t=Timer*;
inline Timer timer;inline constexpr int ESP_OK=0;
inline int esp_timer_create(esp_timer_create_args_t* args,Timer** out){timer.callback=args->callback;*out=&timer;return 0;}
inline int esp_timer_stop(Timer* t){t->running=false;return 0;}
inline int esp_timer_start_periodic(Timer* t,uint64_t){t->running=true;return 0;}
inline void tick(){if(timer.running)timer.callback(nullptr);}
''')
 (p/'test.cpp').write_text('''#include "activity_led.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <cassert>
#include <iostream>
int main(){
 pikaActivityLedBegin();assert(ledLevel==HIGH);
 pikaActivityLedWake();assert(ledLevel==LOW);tick();assert(ledLevel==HIGH);tick();assert(ledLevel==LOW);tick();assert(ledLevel==HIGH);tick();assert(ledLevel==LOW&&!timer.running);
 pikaActivityLedHomeReady();assert(ledLevel==HIGH);timer.callback(nullptr);assert(ledLevel==HIGH);
 {PikaBusyLight first;assert(ledLevel==LOW);{PikaBusyLight second;assert(ledLevel==LOW);}assert(ledLevel==LOW);pikaActivityLedHomeReady();assert(ledLevel==LOW);}assert(ledLevel==HIGH);
 pikaActivityLedConnection(true);assert(ledLevel==LOW);pikaActivityLedConnection(false);assert(ledLevel==HIGH);
 pikaActivityLedWake();pikaActivityLedHomeReady();tick();assert(ledLevel==HIGH);
 pikaActivityLedSleep();pikaActivityLedBusy(true);assert(ledLevel==HIGH);
 std::cout<<"Production LED: wake phases, ready handoff, late callback, nested work and sleep: PASS\\n";
}
''')
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),str(root/'pika_note/src/app/activity_led.cpp'),'-std=c++17','-I',str(p),'-I',str(root/'pika_note/src/app'),'-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)
