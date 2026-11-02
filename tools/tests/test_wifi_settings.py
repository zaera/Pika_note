from pathlib import Path
root=Path(__file__).resolve().parents[2];out=Path('work/manager-host').resolve()
prefix=r'''
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 0
#define ARDUINOJSON_ENABLE_PROGMEM 0
#include "Arduino.h"
#include <ArduinoJson.h>
#include "settings.h"
#include "settings_pair.h"
#include <map>
#include <cassert>
struct Preferences {
 std::map<std::string,String> values;
 bool begin(const char*,bool){return true;}
 bool isKey(const char* key){return values.count(key);}
 String getString(const char* key,const char* fallback=""){return isKey(key)?values[key]:String(fallback);}
 size_t putString(const char* key,const String& value){values[key]=value;return value.length();}
 bool remove(const char* key){return values.erase(key)>0;}
 bool getBool(const char* key,bool fallback){return isKey(key)?values[key]=="1":fallback;}
 size_t putBool(const char* key,bool value){values[key]=value?"1":"0";return 1;}
 int getInt(const char* key,int fallback){return isKey(key)?values[key].toInt():fallback;}
 size_t putInt(const char* key,int value){values[key]=String(value);return 4;}
};
struct SerialStub{void println(const char*){}} Serial;
template<class T>T constrain(T v,T low,T high){return v<low?low:v>high?high:v;}
void resumeBlockedCloudNotes(){}
'''
source=(root/'pika_note/src/app/settings.cpp').read_text(encoding='utf-8');source=source[source.index('static Preferences'):]
main=r'''
int main(){
 assert(!pikaOpenWifiFallback());
 assert(pikaSaveWifiProfile("test-network","old-pass-123"));
 assert(pikaUpdateWifiProfile("test-network","test-network",""));assert(pikaWifiPassword()=="old-pass-123");
 assert(pikaUpdateWifiProfile("test-network","renamed",""));assert(pikaWifiSsid()=="renamed"&&pikaWifiPassword()=="old-pass-123");
 for(int i=1;i<5;++i)assert(pikaUpdateWifiProfile("","backup-"+String(i),"backup-pass"));
 assert(pikaWifiProfileCount()==5);assert(!pikaUpdateWifiProfile("","sixth","new-pass-123"));
 assert(!pikaUpdateWifiProfile("renamed","backup-1",""));assert(pikaWifiPassword()=="old-pass-123");
 assert(pikaRemoveWifiProfile("backup-2"));assert(pikaWifiProfileCount()==4);
 assert(pikaSaveSettings("","","test-api-key-12345","cedar"));
 assert(pikaSaveSettings("","","","coral"));assert(pikaOpenAiKey()=="test-api-key-12345");assert(pikaSpeechVoice()=="coral");
 const String service=R"({"type":"service_account","client_email":"test@demo.iam.gserviceaccount.com","private_key":"-----BEGIN PRIVATE KEY-----\nFAKE TEST DATA"})";
 assert(pikaSaveGoogleSettings("calendar@group.calendar.google.com",service,true));
 assert(pikaSaveGoogleSettings("calendar@group.calendar.google.com","",false));assert(pikaGoogleServiceJson()==service);
 assert(!pikaSaveGoogleSettings("primary","",true));assert(pikaGoogleServiceJson()==service);
 assert(pikaSetOpenWifiFallback(true)&&pikaOpenWifiFallback());
 assert(pikaClearGoogleSettings());assert(!pikaGoogleServiceJson().length());
 printf("Production settings: SSID, unchanged password, rename, five-profile limit, blank API/Google key preservation: PASS\n");
}
'''
(out/'settings.cpp').write_text(prefix+source+main,encoding='utf-8')
print('Host settings test generated')
