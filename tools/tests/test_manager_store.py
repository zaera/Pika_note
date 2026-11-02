from pathlib import Path
root=Path(__file__).resolve().parents[2];out=root/'tools/tests/generated';out.mkdir(parents=True,exist_ok=True)
arduino=r'''
#pragma once
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cctype>
#include <algorithm>
class String {
 std::string text;
 public:
 String(){} String(const char* value):text(value?value:""){} String(const std::string& v):text(v){}
 String(int v):text(std::to_string(v)){} String(unsigned long v):text(std::to_string(v)){}
 const char* c_str()const{return text.c_str();} size_t length()const{return text.size();}
 bool reserve(size_t n){text.reserve(n);return true;}
 bool concat(const char* s,size_t n){text.append(s,n);return true;}
 bool concat(const char* s){text.append(s);return true;} bool concat(char c){text+=c;return true;}
 void remove(size_t n){text.erase(n);} char operator[](size_t n)const{return text[n];}
 void setCharAt(size_t n,char c){text[n]=c;}
 String substring(size_t from,size_t to=std::string::npos)const{if(from>=text.size())return String();return text.substr(from,to==std::string::npos?to:to-from);}
 int indexOf(char c)const{auto p=text.find(c);return p==std::string::npos?-1:(int)p;}
 bool startsWith(const char* s)const{return text.rfind(s,0)==0;}
 bool endsWith(const char* s)const{return text.size()>=strlen(s)&&text.compare(text.size()-strlen(s),strlen(s),s)==0;}
 int toInt()const{return atoi(text.c_str());}
 void trim(){const auto a=text.find_first_not_of(" \r\n\t"),b=text.find_last_not_of(" \r\n\t");text=a==std::string::npos?"":text.substr(a,b-a+1);}
 String& operator+=(char c){text+=c;return *this;}
 String& operator+=(const String& v){text+=v.text;return *this;}
 friend String operator+(const String& a,const String& b){return a.text+b.text;}
 friend bool operator==(const String& a,const String& b){return a.text==b.text;}
 friend bool operator!=(const String& a,const String& b){return !(a==b);}
 friend bool operator<(const String& a,const String& b){return a.text<b.text;}
};
'''
(out/'Arduino.h').write_text(arduino);(out/'WString.h').write_text('#include "Arduino.h"')
prefix=r'''
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 0
#define ARDUINOJSON_ENABLE_PROGMEM 0
#include "manager_store.h"
#include "buffered_json_file.h"
#include "storage_commit.h"
#include "utf8_text.h"
#include <map>
#include <memory>
#include <vector>
#include <cassert>
#include <time.h>
#define NOTES_DIR "/notes"
#define LOCAL_TZ_RULE "UTC0"
int setenv(const char*,const char* value,int){return _putenv_s("TZ",value);}

struct tm* localtime_r(const time_t* value,struct tm* result){*result=*localtime(value);return result;}
struct tm* gmtime_r(const time_t* value,struct tm* result){*result=*gmtime(value);return result;}
void invalidateBrainTaskCache(){}
void pikaPhoneFileTouched(const char*){}bool pikaPhoneFileWillChange(const char*){return true;}
struct NoteEntry{int num;bool hasText;};std::vector<NoteEntry> noteIndex;
std::map<std::string,std::shared_ptr<std::string>> files;
struct File {
 std::shared_ptr<std::string> data;size_t pos=0;
 explicit operator bool()const{return bool(data);}
 bool available(){return data&&pos<data->size();}
 int read(){return available()?(unsigned char)(*data)[pos++]:-1;}
 size_t readBytes(char* buffer,size_t n){if(!data)return 0;n=std::min(n,data->size()-pos);memcpy(buffer,data->data()+pos,n);pos+=n;return n;}
 size_t write(const uint8_t* buffer,size_t n){if(!data)return 0;data->append((const char*)buffer,n);return n;}
 size_t write(uint8_t c){return write(&c,1);}
 size_t print(const String& s){return write((const uint8_t*)s.c_str(),s.length());}
 size_t size(){return data?data->size():0;}void flush(){}void close(){}
};
#define FILE_WRITE 1
struct FS {
 File open(const String& path,int mode=0){const std::string key=path.c_str();if(mode)files[key]=std::make_shared<std::string>();return {files.count(key)?files[key]:nullptr,0};}
 bool exists(const String& path){return files.count(path.c_str());}
 bool remove(const String& path){return files.erase(path.c_str())>0;}
 bool rename(const String& a,const String& b){if(!files.count(a.c_str()))return false;files[b.c_str()]=files[a.c_str()];files.erase(a.c_str());return true;}
} SD_MMC;
uint32_t esp_random(){static uint32_t id=1;return ++id;}uint32_t millis(){return 12345;}
String currentUtcIso(){return "2026-10-20T12:00:00Z";}
String utcToLocalDeviceLabel(const String& value){String out=value.substring(0,16);if(out.length()>10)out.setCharAt(10,' ');return out;}
int nextNoteNumber(){int top=0;for(const auto& n:noteIndex)top=std::max(top,n.num);return top+1;}
bool addToIndex(int num,const char*,bool hasText){noteIndex.push_back({num,hasText});return true;}
bool updateIndexHasText(int num){for(auto& n:noteIndex)if(n.num==num)n.hasText=true;return true;}
void removeBrainSearchIndexEntry(int){}
void removeBrainVectorIndexEntry(int){}
bool pikaGoogleForgetLocalEvents(const std::vector<String>&){return true;}
bool writeNoteQueueState(int,const char*,const char*){return true;}
bool deleteNote(int num){noteIndex.erase(std::remove_if(noteIndex.begin(),noteIndex.end(),[num](const NoteEntry& n){return n.num==num;}),noteIndex.end());return true;}
#include "undo_store.h"
bool pikaRestoreBusy(){return false;}
#ifndef PIKA_REAL_UNDO
bool undoRecoverNeedsBump=false;bool pikaUndoRecover(String&){if(undoRecoverNeedsBump){undoRecoverNeedsBump=false;return managerBumpRevision(managerRevision());}return true;}
bool pikaUndoActive(){return false;}
bool pikaUndoBegin(const std::vector<JsonDocument>&,const String&,String&){return true;}
bool pikaUndoFinish(String&){return true;}
bool pikaUndoClear(){return true;}
bool pikaUndoApply(const String&,String&){return false;}
#endif
std::vector<BrainTaskRecord> base;
std::vector<BrainTaskRecord> loadBrainTasks(){auto tasks=base;managerMergeTasks(tasks);return tasks;}
bool setBrainTaskDone(const String& id,bool done){return managerSetTaskDone(id,done)>0;}
'''
source=(root/'pika_note/src/app/manager_store.cpp').read_text(encoding='utf-8');source=source[source.index('namespace {'):]
main=r'''
bool mutate(const char* json,String& id,String& error){JsonDocument request;assert(!deserializeJson(request,json));return managerMutation(request.as<JsonVariantConst>(),id,error);}
int main(){
 String id,error;
 BrainTaskRecord old;old.id="old-1";old.text="Original";old.done=true;old.noteNum=7;base.push_back(old);
 assert(mutate(R"({"kind":"tasks","action":"save","id":"old-1","record":{"text":"Edited","date":"2026-10-21T18:00","priority":"high"}})",id,error));
 auto tasks=loadBrainTasks();assert(tasks[0].text=="Edited"&&tasks[0].done&&tasks[0].noteNum==7&&tasks[0].priority==2);
 assert(mutate(R"({"kind":"events","record":{"text":"Meeting","date":"2026-10-21T12:00","end":"2026-10-21T13:00","priority":"urgent","allDay":false}})",id,error));
 const String created=id;tasks=loadBrainTasks();assert(tasks.size()==2&&tasks[1].type=="event"&&tasks[1].priority==3);
 const auto before=managerRevision();assert(!mutate(R"({"kind":"events","record":{"text":"Bad date","date":"2026-02-31T12:00","end":"2026-02-31T13:00","priority":"normal"}})",id,error));assert(managerRevision()==before);
 assert(managerSetTaskDone(created,true)==1);tasks=loadBrainTasks();assert(tasks[1].done);
 JsonDocument remove;remove["kind"]="events";remove["action"]="delete";remove["id"]=created;assert(managerMutation(remove.as<JsonVariantConst>(),id,error));assert(loadBrainTasks().size()==1);
 assert(mutate(R"({"kind":"memories","record":{"text":"Manual text","date":"2026-10-20T12:00"}})",id,error));
 assert(id=="m1"&&managerNoteExists(1)&&managerTextOnly(1));assert(managerReadText(1)=="Manual text");assert(!SD_MMC.exists("/notes/note_001.wav"));assert(noteIndex[0].hasText);
 File wav=SD_MMC.open("/notes/note_001.wav",FILE_WRITE);wav.print("unchanged audio");
 assert(mutate(R"({"kind":"memories","id":"m1","record":{"text":"Corrected text"}})",id,error));assert(managerReadText(1)=="Corrected text");assert(*files["/notes/note_001.wav"]=="unchanged audio");
 assert(mutate(R"({"kind":"memories","action":"delete","id":"m1"})",id,error));
 assert(managerNoteExists(1)&&managerArchived(1)&&managerReadText(1)=="Corrected text");
 assert(!SD_MMC.exists("/notes/note_001.wav"));
 assert(mutate(R"({"kind":"memories","action":"remember","id":"m1"})",id,error));
 assert(!managerArchived(1)&&managerTextOnly(1)&&managerReadText(1)=="Corrected text");
 assert(!SD_MMC.exists("/notes/note_001.wav"));
 assert(!mutate(R"({"kind":"tasks","id":"old-1","revision":0,"record":{"text":"Stale","priority":"normal"}})",id,error));assert(error.startsWith("Data changed"));
 BrainTaskRecord incoming;incoming.id="gcal-test-remote1";incoming.type="event";incoming.text="Imported event";incoming.dueUtc="2026-10-21T09:00:00Z";incoming.endUtc="2026-10-21T10:00:00Z";incoming.datePrecision="time";
 assert(managerImportGoogleEvent(incoming,"original-hash",error)==1);
 assert(managerGoogleBaseline(incoming.id)=="original-hash");
 incoming.text="Remote change must not overwrite local";
 assert(managerImportGoogleEvent(incoming,"different-hash",error)==0);
 tasks=loadBrainTasks();auto imported=std::find_if(tasks.begin(),tasks.end(),[&](const BrainTaskRecord& t){return t.id==incoming.id;});assert(imported!=tasks.end()&&imported->text=="Imported event");
 JsonDocument archive;archive["kind"]="events";archive["action"]="archive";archive["id"]=incoming.id;archive["record"]["archived"]=true;
 assert(managerMutation(archive.as<JsonVariantConst>(),id,error));tasks=loadBrainTasks();auto archived=std::find_if(tasks.begin(),tasks.end(),[&](const BrainTaskRecord& t){return t.id==incoming.id;});assert(archived!=tasks.end()&&archived->archived&&archived->dueUtc==incoming.dueUtc&&archived->text=="Imported event");
 archive["record"]["archived"]=false;assert(managerMutation(archive.as<JsonVariantConst>(),id,error));tasks=loadBrainTasks();assert(std::none_of(tasks.begin(),tasks.end(),[](const BrainTaskRecord& t){return t.archived;}));
 JsonDocument tombstone;tombstone["kind"]="events";tombstone["action"]="delete";tombstone["id"]=incoming.id;
 assert(managerMutation(tombstone.as<JsonVariantConst>(),id,error));assert(managerKnownRecord(incoming.id));
 assert(managerImportGoogleEvent(incoming,"third-hash",error)==0);
 tasks=loadBrainTasks();assert(std::none_of(tasks.begin(),tasks.end(),[&](const BrainTaskRecord& t){return t.id==incoming.id;}));
 printf("Production manager storage: original IDs/status, manual event CRUD, dates, text-only memory, WAV preservation and stale edits: PASS\n");
}
'''
(out/'manager.cpp').write_text(prefix+source+main,encoding='utf-8')
print('Host storage test generated')
