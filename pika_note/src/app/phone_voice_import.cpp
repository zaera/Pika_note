#include "phone_voice_import.h"
#include "phone_hash.h"
#include "phone_sync.h"
#include "phone_link.h"
#include "manager_store.h"
#include "data_files.h"
#include <SD_MMC.h>
#include <mbedtls/base64.h>
namespace {
int error(JsonDocument& out,int code,const char* message){out["error"]=message;return code;}
bool hexDigest(const String& value){if(value.length()!=64)return false;for(char c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
bool uuid(const String& value){if(value.length()!=36)return false;for(size_t i=0;i<36;i++){char c=value[i];if(i==8||i==13||i==18||i==23){if(c!='-')return false;}else if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;}return true;}
bool validWav(const String& path,uint32_t expected){File f=SD_MMC.open(path);uint8_t b[44];bool read=f&&f.size()==expected&&f.read(b,44)==44;if(f)f.close();if(!read)return false;auto u32=[&](int i){return uint32_t(b[i])|(uint32_t(b[i+1])<<8)|(uint32_t(b[i+2])<<16)|(uint32_t(b[i+3])<<24);};return !memcmp(b,"RIFF",4)&&!memcmp(b+8,"WAVEfmt ",8)&&!memcmp(b+36,"data",4)&&u32(16)==16&&b[20]==1&&b[21]==0&&b[22]==1&&b[23]==0&&u32(24)==16000&&u32(28)==32000&&b[32]==2&&b[33]==0&&b[34]==16&&b[35]==0&&u32(4)==expected-8&&u32(40)==expected-44&&((expected-44)%2==0);}
void completed(int note,const String& file,const String& meta,const String& part,JsonDocument& out){char marker[64];snprintf(marker,sizeof(marker),"/notes/note_%03d.manual",note);bool changed=SD_MMC.exists(marker);if(changed)SD_MMC.remove(marker);pikaPhoneFileTouched(marker);pikaPhoneFileTouched(file.c_str());SD_MMC.remove(meta);SD_MMC.remove(part);if(changed){managerTouch();pikaPhoneFileTouched(file.c_str());pikaPhoneChanged();}out["done"]=true;out["id"]="m"+String(note);}
}
int pikaPhoneVoiceImport(JsonVariantConst req,JsonDocument& out){
 const String id=req["id"]|"",token=req["token"]|"",sha=req["sha256"]|"",action=req["action"]|"";const uint32_t size=req["size"]|0UL;
 if(id.length()<2||id[0]!='m'||id.length()>9)return error(out,400,"Invalid memory ID");for(size_t i=1;i<id.length();i++)if(id[i]<'0'||id[i]>'9')return error(out,400,"Invalid memory ID");int note=id.substring(1).toInt();if(note<=0||!managerNoteExists(note)||managerArchived(note))return error(out,410,"Memory unavailable or archived");
 if(!uuid(token)||!hexDigest(sha)||size<16044||size>25000000UL)return error(out,400,"Invalid recording metadata");
 char name[64];snprintf(name,sizeof(name),"note_%03d.wav",note);const String file=String("/notes/")+name;const String stem=String("/notes/.phone-voice-")+String(note);const String part=stem+".wav.tmp",meta=stem+".json.tmp";
 if(SD_MMC.exists(file)){String current;int status=pikaPhoneHashRequest(name,current);if(status==202){out["pending"]=true;return 202;}if(status!=200||current!=sha)return error(out,409,"Existing recording is protected");completed(note,file,meta,part,out);return 200;}
 if(!managerTextOnly(note))return error(out,409,"Memory is not a phone text memory");
 JsonDocument saved;bool matching=pikaDataReadJson(meta,saved,4096)&&String(saved["token"]|"")==token&&String(saved["sha256"]|"")==sha&&(saved["size"]|0UL)==size;
 if(action=="begin"){
  if(!matching){pikaPhoneHashCancel();SD_MMC.remove(part);saved.clear();saved["token"]=token;saved["sha256"]=sha;saved["size"]=size;if(!pikaDataWriteJson(meta,saved))return error(out,500,"Cannot save upload intent");}
  File f=SD_MMC.open(part,FILE_APPEND);if(!f)return error(out,500,"Cannot create recording stage");const uint32_t offset=f.size();f.close();if(offset>size)return error(out,409,"Recording stage is larger than expected");out["offset"]=offset;out["done"]=false;return 200;
 }
 if(!matching)return error(out,409,"Start recording upload first");
 if(action=="chunk"){
  const uint32_t offset=req["offset"]|0UL;String encoded=req["data"]|"";if(!encoded.length()||encoded.length()>5464)return error(out,400,"Invalid recording chunk");uint8_t bytes[4096];size_t count=0;if(mbedtls_base64_decode(bytes,sizeof(bytes),&count,(const unsigned char*)encoded.c_str(),encoded.length())||!count)return error(out,400,"Invalid recording chunk encoding");
  File f=SD_MMC.open(part,FILE_APPEND);if(!f)return error(out,500,"Recording stage unavailable");uint32_t current=f.size();if(offset==current){if(count>size-current){f.close();return error(out,400,"Recording exceeds declared size");}pikaPhoneHashCancel();size_t written=f.write(bytes,count);f.flush();current=f.size();f.close();if(written!=count)return error(out,500,"Recording write failed");}else{f.close();if(offset>current)return error(out,409,"Recording chunk has a gap");}out["offset"]=current;return 200;
 }
 if(action=="commit"){
  File f=SD_MMC.open(part);bool complete=f&&f.size()==size;if(f)f.close();if(!complete)return error(out,409,"Recording is incomplete");String digest;String relative=part.substring(7);int status=pikaPhoneHashRequest(relative,digest);if(status==202){out["pending"]=true;return 202;}if(status!=200||digest!=sha||!validWav(part,size))return error(out,409,"Recording checksum or WAV header mismatch");pikaPhoneHashCancel();if(!pikaPhoneFileWillChange(file.c_str()))return error(out,500,"Cannot save recovery intent");if(!SD_MMC.rename(part,file))return error(out,500,"Cannot commit recording");completed(note,file,meta,part,out);return 200;
 }
 return error(out,400,"Unknown recording upload action");
}
