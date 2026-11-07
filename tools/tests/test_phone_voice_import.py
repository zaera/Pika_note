from pathlib import Path
import runpy,subprocess,sys,tempfile,base64,struct
root=Path(__file__).resolve().parents[2];base=runpy.run_path(str(root/'tools/tests/test_manager_store.py'))
arduino=base['arduino'].replace('const char* c_str()const','auto begin()const{return text.begin();}auto end()const{return text.end();}const char* c_str()const').replace('String(unsigned long v)','String(unsigned int v):text(std::to_string(v)){}String(unsigned long v)')
production=(root/'pika_note/src/app/phone_voice_import.cpp').read_text(encoding='utf-8');production='\n'.join(line for line in production.splitlines() if not line.startswith('#include'))
prefix=r'''
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 0
#define ARDUINOJSON_ENABLE_PROGMEM 0
#include "Arduino.h"
#include <ArduinoJson.h>
#include <map>
#include <memory>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
void* heap_caps_malloc(size_t n,int){return malloc(n);}
#include <vector>
#include <cassert>
#include <cstdio>
std::map<std::string,std::shared_ptr<std::string>> files;
struct File{std::shared_ptr<std::string> data;size_t pos=0;explicit operator bool()const{return bool(data);}size_t size(){return data?data->size():0;}size_t read(uint8_t* out,size_t count){if(!data)return 0;count=std::min(count,data->size()-pos);memcpy(out,data->data()+pos,count);pos+=count;return count;}size_t write(const uint8_t* in,size_t count){if(!data)return 0;data->append((const char*)in,count);return count;}void flush(){}void close(){}};
#define FILE_APPEND 2
bool renameFailed=false;
struct FS{File open(const String& path,int mode=0){auto key=std::string(path.c_str());if(mode&&!files.count(key))files[key]=std::make_shared<std::string>();return {files.count(key)?files[key]:nullptr,0};}bool exists(const String& path){return files.count(path.c_str());}bool remove(const String& path){return files.erase(path.c_str())>0;}bool rename(const String& from,const String& to){if(renameFailed||!exists(from)||exists(to))return false;files[to.c_str()]=files[from.c_str()];files.erase(from.c_str());return true;}} SD_MMC;
int touched=0,changed=0;bool archived=false,pendingHash=false;std::string expectedWav;
bool managerNoteExists(int n){return n>=1&&n<=3;}bool managerArchived(int){return archived;}bool managerTextOnly(int n){char p[80];snprintf(p,sizeof(p),"/notes/note_%03d.manual",n);return files.count(p);}
bool pikaPhoneFileWillChange(const char*){return true;}void managerTouch(){touched++;}void pikaPhoneChanged(){changed++;}void pikaPhoneFileTouched(const char*){}void pikaPhoneHashCancel(){}
int pikaPhoneHashRequest(const String& name,String& sha){if(pendingHash){pendingHash=false;return 202;}auto i=files.find(std::string("/notes/")+name.c_str());if(i==files.end())return 404;sha=*i->second==expectedWav?std::string(64,'a'):std::string(64,'b');return 200;}
bool pikaDataReadJson(const String& path,JsonDocument& doc,size_t){auto i=files.find(path.c_str());return i!=files.end()&&!deserializeJson(doc,i->second->c_str());}
bool pikaDataWriteJson(const String& path,const JsonDocument& doc){String text;serializeJson(doc,text);files[path.c_str()]=std::make_shared<std::string>(text.c_str());return true;}
int mbedtls_base64_decode(unsigned char* out,size_t cap,size_t* count,const unsigned char* data,size_t n){const std::string chars="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";unsigned value=0;int bits=-8;*count=0;for(size_t i=0;i<n;i++){if(data[i]=='=')break;auto p=chars.find(data[i]);if(p==std::string::npos)return -1;value=(value<<6)|unsigned(p);bits+=6;if(bits>=0){if(*count>=cap)return -1;out[(*count)++]=(value>>bits)&255;bits-=8;}}return 0;}
'''
wav=b'RIFF'+struct.pack('<I',64036)+b'WAVEfmt '+struct.pack('<IHHIIHH',16,1,1,16000,32000,2,16)+b'data'+struct.pack('<I',64000)+bytes(64000)
main=r'''
int main(){const char* token="12345678-1234-1234-1234-123456789abc";JsonDocument req,out;req["id"]="m1";req["token"]=token;req["sha256"]=std::string(64,'a');req["size"]=64044;req["action"]="begin";files["/notes/note_001.manual"]=std::make_shared<std::string>("manual");assert(pikaPhoneVoiceImport(req.as<JsonVariantConst>(),out)==200&&out["offset"]==0);
'''
main+='expectedWav=std::string("'+''.join('\\x%02x'%b for b in wav[:44])+'",44)+std::string(64000,0);\n'
for i,at in enumerate(range(0,len(wav),32768)):
 chunk=base64.b64encode(wav[at:at+32768]).decode();expected=min(at+32768,len(wav))
 main+=f'req["action"]="chunk";req["offset"]={at};req["data"]="{chunk}";out.clear();assert(pikaPhoneVoiceImport(req.as<JsonVariantConst>(),out)==200&&out["offset"]=={expected});\n'
 if i==0:main+='out.clear();assert(pikaPhoneVoiceImport(req.as<JsonVariantConst>(),out)==200&&out["offset"]==32768);req["action"]="begin";out.clear();assert(pikaPhoneVoiceImport(req.as<JsonVariantConst>(),out)==200&&out["offset"]==32768);req["action"]="chunk";req["offset"]=40000;assert(pikaPhoneVoiceImport(req.as<JsonVariantConst>(),out)==409);\n'
main+=r'''
req["action"]="commit";pendingHash=true;out.clear();assert(pikaPhoneVoiceImport(req.as<JsonVariantConst>(),out)==202);out.clear();assert(pikaPhoneVoiceImport(req.as<JsonVariantConst>(),out)==200&&out["done"]==true);assert(files.count("/notes/note_001.wav")&&!files.count("/notes/note_001.manual")&&touched==1);assert(*files["/notes/note_001.wav"]==expectedWav);
req["action"]="begin";out.clear();assert(pikaPhoneVoiceImport(req.as<JsonVariantConst>(),out)==200&&out["done"]==true&&touched==1);req["sha256"]=std::string(64,'b');assert(pikaPhoneVoiceImport(req.as<JsonVariantConst>(),out)==409&&*files["/notes/note_001.wav"]==expectedWav);req["sha256"]=std::string(64,'a');archived=true;assert(pikaPhoneVoiceImport(req.as<JsonVariantConst>(),out)==410);archived=false;
req["id"]="m2";files["/notes/note_002.manual"]=std::make_shared<std::string>("manual");assert(pikaPhoneVoiceImport(req.as<JsonVariantConst>(),out)==200);req["action"]="commit";assert(pikaPhoneVoiceImport(req.as<JsonVariantConst>(),out)==409&&!files.count("/notes/note_002.wav"));files["/notes/.phone-voice-2.wav.tmp"]=std::make_shared<std::string>(expectedWav);(*files["/notes/.phone-voice-2.wav.tmp"])[50]=99;assert(pikaPhoneVoiceImport(req.as<JsonVariantConst>(),out)==409&&!files.count("/notes/note_002.wav"));files["/notes/.phone-voice-2.wav.tmp"]=std::make_shared<std::string>(expectedWav);renameFailed=true;assert(pikaPhoneVoiceImport(req.as<JsonVariantConst>(),out)==500&&files.count("/notes/.phone-voice-2.wav.tmp")&&files.count("/notes/note_002.manual"));renameFailed=false;assert(pikaPhoneVoiceImport(req.as<JsonVariantConst>(),out)==200);
req["id"]="m3";req["size"]=100;assert(pikaPhoneVoiceImport(req.as<JsonVariantConst>(),out)==400);req["size"]=64044;req["token"]="../../outside";assert(pikaPhoneVoiceImport(req.as<JsonVariantConst>(),out)==400);req["token"]=token;req["id"]="m../";assert(pikaPhoneVoiceImport(req.as<JsonVariantConst>(),out)==400);
puts("Production phone WAV import: resume, duplicate chunks/commit, gap rejection, deferred SHA check, archive/existing WAV protection, rename failure and path validation: PASS");}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'Arduino.h').write_text(arduino,encoding='utf-8');(p/'test.cpp').write_text(prefix+production+main,encoding='utf-8');exe=p/'voice-import.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-I',str(p),'-I',str(root/'.pio/libdeps/pika_note_2/ArduinoJson/src'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
