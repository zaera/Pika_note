#pragma once
#include <Arduino.h>
constexpr int PIKA_GESTURES=8;
inline const char* pikaDefaultAction(int slot){const char* defaults[]={"tasks","daily","record","none","events","weekly","ask","clock"};return slot>=0&&slot<PIKA_GESTURES?defaults[slot]:"none";}
inline bool pikaValidAction(int slot,const String& action){
 if(slot<0||slot>=PIKA_GESTURES)return false;
 if(action=="record"||action=="ask")return slot==2||slot==6;
 for(const char* value:{"none","tasks","events","daily","weekly","clock","calendar","weather","mail","companion"})if(action==value)return true;
 return false;
}
