#include "error_log.h"
#include "home_buttons.h"
#include "buffered_json_file.h"
#include "data_files.h"
#include "storage_commit.h"
#include <SD_MMC.h>
#include <mbedtls/sha256.h>
#include <memory>
#include <algorithm>
bool pikaDataRelativePath(const String& name){
 if(!name.length()||name.length()>90||name[0]=='/'||name.endsWith("/"))return false;
 String segment;
 for(size_t i=0;i<=name.length();++i){
  if(i==name.length()||name[i]=='/'){
   if(!segment.length()||segment=="."||segment==".."||segment.startsWith(".undo-"))return false;segment="";continue;
  }
  const unsigned char c=name[i];if(!isalnum(c)&&c!='.'&&c!='_'&&c!='-')return false;segment+=(char)c;
 }
 return true;
}
bool pikaDataHash(const String& path,String& hash,uint32_t* size){
 File file=SD_MMC.open(path);if(!file||file.isDirectory()||file.size()>0x7fffffffUL){Serial.printf("PIKA_HASH_OPEN_FAIL path=%s errno=%d heap=%u\n",path.c_str(),errno,ESP.getFreeHeap());return false;}
 const uint32_t expected=file.size();uint32_t read=0;uint8_t buffer[2048],digest[32];mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);
 bool ok=mbedtls_sha256_starts(&sha,0)==0;
 while(ok&&read<expected){const size_t n=file.readBytes(reinterpret_cast<char*>(buffer),std::min<size_t>(sizeof(buffer),expected-read));if(!n){ok=false;break;}read+=n;ok=mbedtls_sha256_update(&sha,buffer,n)==0;delay(0);}
 if(ok)ok=mbedtls_sha256_finish(&sha,digest)==0;mbedtls_sha256_free(&sha);file.close();if(!ok||read!=expected){Serial.printf("PIKA_HASH_READ_FAIL path=%s read=%lu expected=%lu ok=%d heap=%u\n",path.c_str(),(unsigned long)read,(unsigned long)expected,ok,ESP.getFreeHeap());return false;}
 char hex[65];for(int i=0;i<32;++i)sprintf(hex+i*2,"%02x",digest[i]);hex[64]=0;hash=hex;if(size)*size=expected;return true;
}
bool pikaDataParents(const String& path){
 for(int i=1;i<(int)path.length();++i)if(path[i]=='/'){const String parent=path.substring(0,i);if(!SD_MMC.exists(parent)&&!SD_MMC.mkdir(parent))return false;}return true;
}
bool pikaDataCopy(const String& from,const String& to){
 File input=SD_MMC.open(from);if(!input||input.isDirectory())return false;if(!pikaDataParents(to)){input.close();return false;}
 if(!pikaPhoneFileWillChange(to.c_str())){input.close();return false;}SD_MMC.remove(to);File output=SD_MMC.open(to,FILE_WRITE);if(!output){input.close();return false;}
 const size_t expected=input.size();size_t copied=0;uint8_t buffer[2048];bool ok=true;
 while(copied<expected){const size_t n=input.readBytes(reinterpret_cast<char*>(buffer),std::min(sizeof(buffer),expected-copied));if(!n||output.write(buffer,n)!=n){ok=false;break;}copied+=n;delay(0);}
 output.flush();ok=ok&&copied==expected&&output.size()==expected;input.close();output.close();if(!ok)SD_MMC.remove(to);else pikaPhoneFileTouched(to.c_str());return ok;
}
bool pikaDataRemoveTree(const String& path){
 if((path!="/notes"&&!path.startsWith("/notes/")&&!path.startsWith("/.pika-restore-"))||path.indexOf("..")>=0)return false;
 if(!SD_MMC.exists(path))return true;File node=SD_MMC.open(path);if(!node)return false;if(!node.isDirectory()){node.close();if(!pikaPhoneFileWillChange(path.c_str()))return false;const bool removed=SD_MMC.remove(path);if(removed)pikaPhoneFileTouched(path.c_str());return removed;}
 std::vector<String> children;File entry;while((entry=node.openNextFile())){String name=entry.name();entry.close();const int slash=name.lastIndexOf('/');if(slash>=0)name=name.substring(slash+1);children.push_back(path+"/"+name);}node.close();
 for(const String& child:children)if(!pikaDataRemoveTree(child))return false;return SD_MMC.rmdir(path);
}
bool pikaDataReadJson(const String& path,JsonDocument& doc,size_t maximum){
 recoverStoredFile(SD_MMC,path);File file=SD_MMC.open(path);if(!file||file.size()>maximum){if(file)file.close();return false;}const auto e=pikaReadJsonBuffered(doc,file);file.close();return !e;
}
bool pikaDataWriteJson(const String& path,const JsonDocument& doc,bool cooperative){
 if(!pikaDataParents(path))return false;const String temp=path+".tmp";SD_MMC.remove(temp);File file=SD_MMC.open(temp,FILE_WRITE);if(!file)return false;
 const size_t expected=measureJson(doc),written=pikaWriteJsonBuffered(doc,file,cooperative?pikaHomeButtonsBusy:nullptr);file.flush();const bool ok=written==expected&&file.size()==expected;file.close();const bool committed=ok&&commitStoredFile(SD_MMC,path,temp);if(!committed&&!(cooperative&&pikaHomeButtonsBusy()))pikaError("storage","writeJson","Atomic file save failed");return committed;
}
namespace {
bool listLevel(const String& root,const String& relative,std::vector<PikaDataFile>& files,String& error,unsigned depth,bool hashes,bool cooperative){
 if(depth>8){error="Backup directory nesting is too deep";return false;}
 File directory=SD_MMC.open(root+(relative.length()?"/"+relative:String()));if(!directory||!directory.isDirectory()){error="Could not read notes directory";return false;}
 File entry;while((entry=directory.openNextFile())){
  if(cooperative&&pikaHomeButtonsBusy()){entry.close();directory.close();error="Input pending";return false;}
  String name=entry.name();const bool dir=entry.isDirectory();const uint32_t bytes=entry.size(),modified=entry.getLastWrite();entry.close();const int slash=name.lastIndexOf('/');if(slash>=0)name=name.substring(slash+1);
  if(!relative.length()&&name.startsWith(".undo-"))continue;
  const String path=relative.length()?relative+"/"+name:name;
  if(!pikaDataRelativePath(path)){error="Unsupported backup filename";return false;}
  if(dir){if(!listLevel(root,path,files,error,depth+1,hashes,cooperative))return false;}
  else{PikaDataFile item;item.name=path;item.size=bytes;item.modified=modified;if(hashes&&!pikaDataHash(root+"/"+path,item.sha256,&item.size)){error="Could not read a backup file";return false;}files.push_back(std::move(item));if(files.size()>4096){error="Too many backup files";return false;}}
 }directory.close();return true;
}
}
bool pikaDataList(const String& root,std::vector<PikaDataFile>& files,String& error,bool hashes,bool cooperative){files.clear();if(!listLevel(root,"",files,error,0,hashes,cooperative))return false;std::sort(files.begin(),files.end(),[](const PikaDataFile& a,const PikaDataFile& b){return a.name<b.name;});return true;}
