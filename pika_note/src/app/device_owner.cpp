#include "brand_compat.h"
#include "device_owner.h"
#include "factory_reset.h"
#include "phone_link.h"
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include <nvs.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include <errno.h>
#include <esp_mac.h>
#include <mutex>
#include <atomic>
#include <vector>
uint32_t pikaOwnerRestartAt=0;
namespace{String ownerKey,ownerPeer;std::vector<String> oldPeers;std::atomic<bool> bound{false};bool legacy=false;std::mutex lock;
String mac(const String& key,const String& nonce){uint8_t digest[32];const auto* info=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);mbedtls_md_hmac(info,(const uint8_t*)key.c_str(),key.length(),(const uint8_t*)nonce.c_str(),nonce.length(),digest);char hex[65];for(int n=0;n<32;n++)sprintf(hex+n*2,"%02x",digest[n]);hex[64]=0;return hex;}
bool equal(const String&a,const String&b){if(a.length()!=b.length())return false;uint8_t d=0;for(size_t n=0;n<a.length();n++)d|=a[n]^b[n];return d==0;}
bool hexKey(const String&s){if(s.length()!=64)return false;for(char c:s)if(!isxdigit(c))return false;return true;}
const char* spaces[]={"pika","pikaMail","pikaWeather","pikaExtras","pikaLed","pikaClock"};bool allowed(const String&ns){for(auto v:spaces)if(ns==v)return true;return false;}
}
String pikaDeviceSerial(){uint8_t address[6];esp_read_mac(address,ESP_MAC_WIFI_STA);char serial[16];snprintf(serial,sizeof(serial),"PN-%02X%02X%02X",address[3],address[4],address[5]);return serial;}
void pikaOwnerBegin(){Preferences p;p.begin("pikaOwner",false);if(p.getBool("clearBonds",false)){NimBLEDevice::deleteAllBonds();p.remove("clearBonds");}ownerKey=p.getString("key","");ownerPeer=p.getString("peer","");p.end();bound=ownerKey.length()==64;legacy=ownerKey.isEmpty()&&NimBLEDevice::getNumBonds()>0;oldPeers.clear();for(int n=0;n<NimBLEDevice::getNumBonds();n++)oldPeers.push_back(String(NimBLEDevice::getBondedAddress(n).toString().c_str()));}
bool pikaOwnerBound(){return bound.load();}bool pikaOwnerLegacyBondOnly(){return legacy;}
bool pikaOwnerAuthenticate(const String& nonce,const String& proof,const String& claimKey,const String& peer,bool previouslyBonded){std::lock_guard<std::mutex> guard(lock);if(nonce.length()!=64)return false;
 if(pikaOwnerBound()){
  const bool valid=equal(mac(ownerKey,nonce),proof);Serial.printf("PIKA_OWNER_CHECK proof=%d same_peer=%d bonded=%d\n",valid,equal(peer,ownerPeer),previouslyBonded);if(!valid)return false;
  if(!equal(peer,ownerPeer)){
   // Preview migration: initial pairing cached an unresolved private address.
   // Only an existing bond AND the stored owner's HMAC can repair identity.
   if(!previouslyBonded)return false;Preferences p;if(!p.begin("pikaOwner",false))return false;
   bool saved=p.putString("peer",peer)==peer.length();p.end();if(!saved)return false;ownerPeer=peer;
  }
  return true;
 }
 if(legacy){bool old=false;for(const auto& known:oldPeers)if(equal(known,peer))old=true;if(!old)return false;}if(!hexKey(claimKey)||!equal(mac(claimKey,nonce),proof))return false;Preferences p;if(!p.begin("pikaOwner",false))return false;bool ok=p.putString("peer",peer)==peer.length()&&p.putString("key",claimKey)==64;p.end();if(!ok)return false;ownerPeer=peer;ownerKey=claimKey;bound=true;legacy=false;return true;}
String pikaOwnerForgetReceipt(){Preferences p;p.begin("pikaFactory",true);String value=p.getString("forgetReceipt","");p.end();return value;}
bool pikaOwnerForget(const String& serial,const String& receipt,String& error){if(!pikaOwnerBound()||serial!=pikaDeviceSerial()||receipt.length()!=36){error="Owner confirmation required";return false;}Preferences p;if(!p.begin("pikaFactory",false)){error="Reset journal unavailable";return false;}bool ok=p.putString("forgetReceipt",receipt)==receipt.length()&&p.putBool("purgeOwner",true)==1;p.end();if(!ok){error="Could not save forget receipt";return false;}bool scheduled=pikaFactoryRequest(String(1000+esp_random()%9000),error);if(!scheduled){Preferences rollback;if(rollback.begin("pikaFactory",false)){if(!rollback.getBool("pending",false)){rollback.remove("purgeOwner");rollback.remove("forgetReceipt");}rollback.end();}}return scheduled;}
String pikaOwnerExportSettings(){JsonDocument out;out["version"]=1;out["serial"]=pikaDeviceSerial();JsonObject all=out["namespaces"].to<JsonObject>();for(auto ns:spaces){nvs_handle_t h;if(nvs_open(ns,NVS_READONLY,&h)!=ESP_OK)continue;nvs_iterator_t it=nullptr;esp_err_t status=nvs_entry_find("nvs",ns,NVS_TYPE_ANY,&it);while(status==ESP_OK){nvs_entry_info_t info;nvs_entry_info(it,&info);JsonObject entry=all[ns][info.key].to<JsonObject>();entry["type"]=int(info.type);size_t size=0;if(info.type==NVS_TYPE_STR){nvs_get_str(h,info.key,nullptr,&size);if(size>50000){nvs_release_iterator(it);nvs_close(h);return "";}char* data=(char*)malloc(size?size:1);if(!data){nvs_release_iterator(it);nvs_close(h);return "";}if(nvs_get_str(h,info.key,data,&size)==ESP_OK)entry["value"]=data;free(data);}else if(info.type==NVS_TYPE_BLOB){nvs_get_blob(h,info.key,nullptr,&size);if(size>32000){nvs_release_iterator(it);nvs_close(h);return "";}uint8_t* data=(uint8_t*)malloc(size?size:1);unsigned char* encoded=(unsigned char*)malloc(size*2+8);if(!data||!encoded){free(data);free(encoded);nvs_release_iterator(it);nvs_close(h);return "";}if(nvs_get_blob(h,info.key,data,&size)==ESP_OK){size_t n=0;mbedtls_base64_encode(encoded,size*2+7,&n,data,size);encoded[n]=0;entry["value"]=(char*)encoded;}free(data);free(encoded);}else{int64_t signedValue=0;uint64_t unsignedValue=0;switch(info.type){case NVS_TYPE_I8:{int8_t v;nvs_get_i8(h,info.key,&v);signedValue=v;break;}case NVS_TYPE_U8:{uint8_t v;nvs_get_u8(h,info.key,&v);unsignedValue=v;break;}case NVS_TYPE_I16:{int16_t v;nvs_get_i16(h,info.key,&v);signedValue=v;break;}case NVS_TYPE_U16:{uint16_t v;nvs_get_u16(h,info.key,&v);unsignedValue=v;break;}case NVS_TYPE_I32:{int32_t v;nvs_get_i32(h,info.key,&v);signedValue=v;break;}case NVS_TYPE_U32:{uint32_t v;nvs_get_u32(h,info.key,&v);unsignedValue=v;break;}case NVS_TYPE_I64:nvs_get_i64(h,info.key,&signedValue);break;case NVS_TYPE_U64:nvs_get_u64(h,info.key,&unsignedValue);break;default:break;}entry["value"]=(int(info.type)&0x10)?String((long long)signedValue):String((unsigned long long)unsignedValue);}status=nvs_entry_next(&it);}nvs_release_iterator(it);nvs_close(h);}String text;serializeJson(out,text);return text.length()<=60000?text:String();}
namespace {
bool validateSettings(JsonVariantConst input,String& error){
 if(!input["namespaces"].is<JsonObjectConst>()||input["namespaces"].size()>6){error="Invalid settings backup";return false;}
 for(JsonPairConst space:input["namespaces"].as<JsonObjectConst>()){
  if(!allowed(currentBrand(space.key().c_str()))||!space.value().is<JsonObjectConst>()){error="Protected settings namespace";return false;}
  for(JsonPairConst pair:space.value().as<JsonObjectConst>()){
   const size_t keySize=strlen(pair.key().c_str());auto entry=pair.value();if(!entry["type"].is<int>()){error="Invalid settings type";return false;}int type=entry["type"]|-1;
   if(!keySize||keySize>15||!entry["value"].is<const char*>()){error="Invalid settings entry";return false;}
   String value=entry["value"].as<String>();
   if(type==NVS_TYPE_STR){if(value.length()>50000){error="Settings value too large";return false;}continue;}
   if(type==NVS_TYPE_BLOB){size_t size=0;int result=mbedtls_base64_decode(nullptr,0,&size,(const unsigned char*)value.c_str(),value.length());if((result!=0&&result!=MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)||size>32000){error="Invalid settings blob";return false;}continue;}
   unsigned bits=0;bool negative=false;switch(type){case NVS_TYPE_I8:negative=true;case NVS_TYPE_U8:bits=8;break;case NVS_TYPE_I16:negative=true;case NVS_TYPE_U16:bits=16;break;case NVS_TYPE_I32:negative=true;case NVS_TYPE_U32:bits=32;break;case NVS_TYPE_I64:negative=true;case NVS_TYPE_U64:bits=64;break;default:error="Unsupported settings type";return false;}
   if(!value.length()){error="Invalid numeric setting";return false;}size_t start=negative&&value[0]=='-'?1:0;if(start==value.length()){error="Invalid numeric setting";return false;}for(size_t n=start;n<value.length();n++)if(value[n]<'0'||value[n]>'9'){error="Invalid numeric setting";return false;}
   errno=0;char* end=nullptr;if(negative){int64_t v=strtoll(value.c_str(),&end,10);if(errno||*end||(bits<64&&(v<-(int64_t(1)<<(bits-1))||v>((int64_t(1)<<(bits-1))-1)))){error="Numeric setting out of range";return false;}}
   else{uint64_t v=strtoull(value.c_str(),&end,10);if(errno||*end||(bits<64&&v>((uint64_t(1)<<bits)-1))){error="Numeric setting out of range";return false;}}
  }
 }
 return true;
}
}
#ifdef PIKA_TEST_SETTINGS_RECOVERY
bool pikaOwnerArmRecoveryProbe(){Preferences p;if(!p.begin("pikaCfgProbe",false))return false;bool ok=p.putBool("resetOnce",true)==1;p.end();return ok;}
static void settingsProbeAfterNamespaceClear(){Preferences p;if(!p.begin("pikaCfgProbe",false))return;bool armed=p.getBool("resetOnce",false);if(armed)p.remove("resetOnce");p.end();if(armed){Serial.println("PIKA_SETTINGS_PROBE_RESET_AFTER_CLEAR");Serial.flush();ESP.restart();}}
#else
static inline void settingsProbeAfterNamespaceClear(){}
#endif
static bool applyImportedSettings(JsonVariantConst input,String& error){if(!validateSettings(input,error))return false;if((input["version"]|0)!=1){error="Unsupported settings backup";return false;}JsonObjectConst all=input["namespaces"].as<JsonObjectConst>();for(const char* ns:spaces){nvs_handle_t handle;if(nvs_open(ns,NVS_READWRITE,&handle)!=ESP_OK){error="Settings unavailable";return false;}bool cleared=nvs_erase_all(handle)==ESP_OK&&nvs_commit(handle)==ESP_OK;nvs_close(handle);if(!cleared){error="Could not replace settings";return false;}settingsProbeAfterNamespaceClear();}for(JsonPairConst space:all){if(!allowed(currentBrand(space.key().c_str()))){error="Protected settings namespace";return false;}nvs_handle_t h;if(nvs_open(currentBrand(space.key().c_str()).c_str(),NVS_READWRITE,&h)!=ESP_OK){error="Settings unavailable";return false;}bool ok=nvs_erase_all(h)==ESP_OK;for(JsonPairConst pair:space.value().as<JsonObjectConst>()){if(strlen(pair.key().c_str())>15){ok=false;break;}auto e=pair.value();const int type=e["type"]|-1;const String value=e["value"]|"";esp_err_t result=ESP_FAIL;const int64_t si=strtoll(value.c_str(),nullptr,10);const uint64_t ui=strtoull(value.c_str(),nullptr,10);switch(type){case NVS_TYPE_STR:result=nvs_set_str(h,pair.key().c_str(),value.c_str());break;case NVS_TYPE_I8:result=nvs_set_i8(h,pair.key().c_str(),si);break;case NVS_TYPE_U8:result=nvs_set_u8(h,pair.key().c_str(),ui);break;case NVS_TYPE_I16:result=nvs_set_i16(h,pair.key().c_str(),si);break;case NVS_TYPE_U16:result=nvs_set_u16(h,pair.key().c_str(),ui);break;case NVS_TYPE_I32:result=nvs_set_i32(h,pair.key().c_str(),si);break;case NVS_TYPE_U32:result=nvs_set_u32(h,pair.key().c_str(),ui);break;case NVS_TYPE_I64:result=nvs_set_i64(h,pair.key().c_str(),si);break;case NVS_TYPE_U64:result=nvs_set_u64(h,pair.key().c_str(),ui);break;case NVS_TYPE_BLOB:{size_t size=0;auto* data=(unsigned char*)malloc(value.length()+1);if(data){if(mbedtls_base64_decode(data,value.length()+1,&size,(const unsigned char*)value.c_str(),value.length())==0)result=nvs_set_blob(h,pair.key().c_str(),data,size);free(data);}break;}default:break;}if(result!=ESP_OK){ok=false;break;}}if(ok)ok=nvs_commit(h)==ESP_OK;nvs_close(h);if(!ok){error="Could not restore settings";return false;}}return true;}

namespace {
struct SettingsJournal {char magic[8];uint32_t length;uint8_t sha256[32];};
const esp_partition_t* settingsPartition(){return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"pikacfg");}
}
bool pikaOwnerImportSettings(JsonVariantConst input,String& error){
 if((input["version"]|0)!=1||!validateSettings(input,error)){if(!error.length())error="Invalid settings backup";return false;}
 const auto* part=settingsPartition();String body;serializeJson(input,body);
 if(!part||body.length()>60000||sizeof(SettingsJournal)+body.length()>part->size){error="Settings journal unavailable";return false;}
 SettingsJournal header{};memcpy(header.magic,"PIKASV7",8);header.length=body.length();mbedtls_sha256((const uint8_t*)body.c_str(),body.length(),header.sha256,0);
 // Write the committed header last. Until it exists, boot keeps old settings.
 if(esp_partition_erase_range(part,0,part->size)!=ESP_OK||esp_partition_write(part,sizeof(header),body.c_str(),body.length())!=ESP_OK||esp_partition_write(part,0,&header,sizeof(header))!=ESP_OK){error="Could not stage settings; current settings kept";return false;}
 return true;
}
bool pikaOwnerRecoverSettings(String& error){
 const auto* part=settingsPartition();if(!part)return true;SettingsJournal header{};
 if(esp_partition_read(part,0,&header,sizeof(header))!=ESP_OK){error="Settings journal read failed";return false;}
 if(memcmp(header.magic,"PIKASV7",8)!=0)return true;
 if(!header.length||header.length>60000||sizeof(header)+header.length>part->size){error="Settings journal length invalid";return false;}
 std::vector<uint8_t> bytes(header.length);if(esp_partition_read(part,sizeof(header),bytes.data(),bytes.size())!=ESP_OK){error="Settings journal interrupted";return false;}
 uint8_t digest[32];mbedtls_sha256(bytes.data(),bytes.size(),digest,0);if(memcmp(digest,header.sha256,sizeof(digest))!=0){error="Settings journal checksum failed";return false;}
 JsonDocument input;if(deserializeJson(input,bytes.data(),bytes.size())||!applyImportedSettings(input.as<JsonVariantConst>(),error))return false;
 // A power loss during application repeats the complete import on next boot.
 // No application settings caches or BLE services have started yet.
 if(esp_partition_erase_range(part,0,part->size)!=ESP_OK){error="Could not finish settings recovery";return false;}
 Serial.println("PIKA_SETTINGS_RECOVERED");return true;
}
