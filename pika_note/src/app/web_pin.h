#pragma once
#include <WebServer.h>
void pikaLocalConfigBegin();
bool pikaWebAccess(WebServer& server);

bool pikaWebSessionValid(WebServer& server);

bool pikaWebChangePin(const String& current,const String& next,String& error);

bool pikaWebPinProof(const String& nonce,const String& proof);
