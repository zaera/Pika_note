#pragma once
#include <Arduino.h>
struct PikaDeferredResponse {int code=-1;String body;};
bool pikaDeferredHttpStart(int owner,const String& url,const String& method,const String& body,const String& authorization,bool weatherCa=false);
bool pikaDeferredHttpPoll(int owner,PikaDeferredResponse& result);
bool pikaDeferredHttpBusy();

void pikaDeferredHttpCancel(int owner);
void pikaDeferredHttpReap();
