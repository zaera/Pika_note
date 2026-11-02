#pragma once
#include <Arduino.h>
#include <WebServer.h>
bool pikaRestoreBusy();
bool pikaRestoreRecover(String& error);
void pikaBackupRoutes(WebServer& server,void (*restored)());

bool pikaPhoneRestoreCommand(const String& path,const String& body,String& result,int& code);
