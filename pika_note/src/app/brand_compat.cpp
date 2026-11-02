#include "brand_compat.h"
#include <nvs.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <vector>

namespace {
struct Entry {char key[16];uint8_t type;std::vector<uint8_t> bytes;};
uint32_t checksum(const std::vector<uint8_t>& bytes){uint32_t h=2166136261u;for(uint8_t b:bytes)h=(h^b)*16777619u;return h;}
void append(std::vector<uint8_t>& out,const void* data,size_t n){const auto* p=(const uint8_t*)data;out.insert(out.end(),p,p+n);}
bool writeEntries(const String& name,const std::vector<Entry>& entries){nvs_handle_t h;if(nvs_open(name.c_str(),NVS_READWRITE,&h)!=ESP_OK)return false;bool ok=true;
 for(const auto& e:entries){esp_err_t status=ESP_FAIL;switch(e.type){
#define SET(T,t,S) case NVS_TYPE_##T:{S v;if(e.bytes.size()!=sizeof(v)){ok=false;break;}memcpy(&v,e.bytes.data(),sizeof(v));status=nvs_set_##t(h,e.key,v);break;}
 SET(I8,i8,int8_t) SET(U8,u8,uint8_t) SET(I16,i16,int16_t) SET(U16,u16,uint16_t) SET(I32,i32,int32_t) SET(U32,u32,uint32_t) SET(I64,i64,int64_t) SET(U64,u64,uint64_t)
 case NVS_TYPE_STR:if(!e.bytes.empty()&&e.bytes.back()==0)status=nvs_set_str(h,e.key,(const char*)e.bytes.data());break;
 case NVS_TYPE_BLOB:status=nvs_set_blob(h,e.key,e.bytes.data(),e.bytes.size());break;default:break;}
 if(status!=ESP_OK){Serial.printf("[Migration] write type=%u status=%d\n",e.type,status);ok=false;break;}}
 if(ok)ok=nvs_commit(h)==ESP_OK;nvs_close(h);return ok;}
const char* journal="/.pika-brand-settings.bin";
bool recoverNamespace(){if(!SD_MMC.exists(journal))return true;File f=SD_MMC.open(journal);if(!f||f.size()<40||f.size()>65536)return false;std::vector<uint8_t> data(f.size());bool ok=f.read(data.data(),data.size())==data.size();f.close();if(!ok)return false;uint32_t saved;memcpy(&saved,data.data()+data.size()-4,4);data.resize(data.size()-4);if(checksum(data)!=saved||memcmp(data.data(),"BR08",4))return false;
 char oldName[16]={},newName[16]={};memcpy(oldName,data.data()+4,16);memcpy(newName,data.data()+20,16);if(oldName[15]||newName[15])return false;size_t pos=36;std::vector<Entry> entries;
 while(pos<data.size()){if(pos+21>data.size())return false;Entry e{};memcpy(e.key,data.data()+pos,16);pos+=16;e.type=data[pos++];uint32_t n;memcpy(&n,data.data()+pos,4);pos+=4;if(e.key[15]||n>data.size()-pos)return false;e.bytes.assign(data.begin()+pos,data.begin()+pos+n);pos+=n;entries.push_back(std::move(e));}
 nvs_handle_t old;if(nvs_open(oldName,NVS_READWRITE,&old)!=ESP_OK)return false;ok=nvs_erase_all(old)==ESP_OK&&nvs_commit(old)==ESP_OK;nvs_close(old);if(!ok||!writeEntries(newName,entries))return false;return SD_MMC.remove(journal);
}
}
bool pikaBrandNeedsMigration(){Preferences p;if(!p.begin("pikaBrand",true))return true;bool done=p.getBool("settingsV8",false);p.end();return !done;}
bool pikaMigrateSettings(){
 if(!recoverNamespace())return false;
 std::vector<String> namespaces;nvs_iterator_t it=nullptr;
 if(nvs_entry_find("nvs",nullptr,NVS_TYPE_ANY,&it)==ESP_OK){do{nvs_entry_info_t info;nvs_entry_info(it,&info);String name(info.namespace_name);if(name.startsWith(previousBrand())){bool seen=false;for(const auto& v:namespaces)if(v==name)seen=true;if(!seen)namespaces.push_back(name);}}while(nvs_entry_next(&it)==ESP_OK);nvs_release_iterator(it);}
 for(const auto& old:namespaces){nvs_handle_t from;if(nvs_open(old.c_str(),NVS_READONLY,&from)!=ESP_OK)return false;std::vector<Entry> entries;bool ok=true;it=nullptr;
 if(nvs_entry_find("nvs",old.c_str(),NVS_TYPE_ANY,&it)==ESP_OK){do{nvs_entry_info_t info;nvs_entry_info(it,&info);Entry e{};memcpy(e.key,info.key,16);e.type=info.type;esp_err_t status=ESP_FAIL;switch(info.type){
#define GET(T,t,S) case NVS_TYPE_##T:{S v;status=nvs_get_##t(from,info.key,&v);if(status==ESP_OK)append(e.bytes,&v,sizeof(v));break;}
 GET(I8,i8,int8_t) GET(U8,u8,uint8_t) GET(I16,i16,int16_t) GET(U16,u16,uint16_t) GET(I32,i32,int32_t) GET(U32,u32,uint32_t) GET(I64,i64,int64_t) GET(U64,u64,uint64_t)
 case NVS_TYPE_STR:{size_t n=0;status=nvs_get_str(from,info.key,nullptr,&n);e.bytes.resize(n);if(status==ESP_OK)status=nvs_get_str(from,info.key,(char*)e.bytes.data(),&n);break;}
 case NVS_TYPE_BLOB:{size_t n=0;status=nvs_get_blob(from,info.key,nullptr,&n);e.bytes.resize(n);if(status==ESP_OK)status=nvs_get_blob(from,info.key,e.bytes.data(),&n);break;}default:break;}
 if(status!=ESP_OK){ok=false;break;}entries.push_back(std::move(e));
 }while(nvs_entry_next(&it)==ESP_OK);nvs_release_iterator(it);}nvs_close(from);if(!ok)return false;
 std::vector<uint8_t> data;append(data,"BR08",4);char oldName[16]={},newName[16]={};old.toCharArray(oldName,16);currentBrand(old).toCharArray(newName,16);append(data,oldName,16);append(data,newName,16);
 for(const auto& e:entries){append(data,e.key,16);append(data,&e.type,1);uint32_t n=e.bytes.size();append(data,&n,4);append(data,e.bytes.data(),n);}uint32_t sum=checksum(data);append(data,&sum,4);
 File f=SD_MMC.open(String(journal)+".tmp",FILE_WRITE);if(!f)return false;ok=f.write(data.data(),data.size())==data.size();f.flush();f.close();if(!ok||!SD_MMC.rename(String(journal)+".tmp",journal)||!recoverNamespace())return false;
 }Preferences marker;if(!marker.begin("pikaBrand",false))return false;bool saved=marker.putBool("settingsV8",true)==1;marker.end();return saved;
}
bool pikaMigrateCard(){Preferences p;if(!p.begin("pikaBrand",false))return false;if(p.getBool("cardV8",false)){p.end();return true;}
 // System paths are known: avoid walking a large user card during boot.
 const char* roots[]={"/.pika-errors.bin","/.pika-phone-sync.json","/.pika-restore-stage","/.pika-restore-old","/.pika-restore-journal.json","/notes/.pika-answer.wav","/notes/.pika-briefing.wav","/notes/.pika-weekly.wav"};
 for(const char* root:roots)for(const char* suffix:{"",".tmp",".bak"}){String next=String(root)+suffix,old=next;old.replace("pika",previousBrand());if(!SD_MMC.exists(old))continue;if(SD_MMC.exists(next)||!SD_MMC.rename(old,next)){p.end();return false;}}
 bool ok=p.putBool("cardV8",true)==1;p.end();return ok;
}
