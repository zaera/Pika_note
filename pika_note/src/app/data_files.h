#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
struct PikaDataFile {String name,sha256;uint32_t size=0,modified=0;};
bool pikaDataRelativePath(const String& name);
bool pikaDataHash(const String& path,String& hash,uint32_t* size=nullptr);
bool pikaDataCopy(const String& from,const String& to);
bool pikaDataParents(const String& path);
bool pikaDataRemoveTree(const String& path);
bool pikaDataReadJson(const String& path,JsonDocument& doc,size_t maximum=1048576);
bool pikaDataWriteJson(const String& path,const JsonDocument& doc,bool cooperative=false);
bool pikaDataList(const String& root,std::vector<PikaDataFile>& files,String& error,bool hashes=true,bool cooperative=false);
