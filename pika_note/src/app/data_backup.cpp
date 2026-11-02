#include "phone_sync.h"
#include "brand_compat.h"
#include <esp_heap_caps.h>
#include <memory>
#include <mbedtls/base64.h>
#include "data_backup.h"
#include "notes.h"
#include "data_files.h"
#include "manager_store.h"
#include "undo_store.h"
#include "web_pin.h"
#include "viewer_request_policy.h"
#include "google_calendar.h"
#include "weather.h"
#include "rtc.h"
#include "storage_commit.h"
#include <SD_MMC.h>
#include <mbedtls/sha256.h>
#include <esp_random.h>
#include <algorithm>
namespace {
const String stageRoot="/.pika-restore-stage",oldRoot="/.pika-restore-old",restoreJournal="/.pika-restore-journal.json";
bool phoneCall=false;String phoneBody,phoneOutput;int phoneCode=200;
WebServer* web=nullptr;
String plainBody(){return phoneCall?phoneBody:web->arg("plain");}
void (*onRestored)()=nullptr;
bool restoring=false,uploadAccepted=false,uploadOwned=false,shaActive=false;String session,lastCommitted,uploadError;
uint32_t initialRevision=0,uploadIndex=0,received=0;JsonDocument restoreManifest;File incoming;mbedtls_sha256_context uploadSha;
std::vector<bool> uploaded;
void json(int code,const String& error=String()) {JsonDocument doc;if(error.length())doc["error"]=error;else{doc["saved"]=true;doc["revision"]=managerRevision();}String body;serializeJson(doc,body);if(phoneCall){phoneCode=code;phoneOutput=body;return;}web->sendHeader("Cache-Control","no-store");web->send(code,"application/json; charset=utf-8",body);}
String stagedPath(size_t index){return stageRoot+"/notes/"+String(restoreManifest["files"][index]["name"]|"");}
bool safeSha(const String& sha){if(sha.length()!=64)return false;for(char c:sha)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
bool validateManifest(JsonDocument& doc,String& error){
 if(currentBrand(String(doc["format"]|""))!="pika-backup"||(doc["version"]|0)!=1||!doc["files"].is<JsonArray>()||doc["files"].size()>4096){error="Unsupported Pika backup";return false;}
 std::vector<String> names;bool catalog=false;uint64_t total=0;
 for(auto row:doc["files"].as<JsonArray>()){
  const String name=row["name"]|"",sha=row["sha256"]|"";
  if(!pikaDataRelativePath(name)||!safeSha(sha)||!row["size"].is<uint32_t>()||row["size"].as<uint32_t>()>0x7fffffffUL||std::find(names.begin(),names.end(),name)!=names.end()){error="Invalid backup file list";return false;}
  names.push_back(name);catalog|=name=="index.csv";total+=row["size"].as<uint32_t>();
 }
 if(!catalog){error="The backup is missing notes/index.csv";return false;}
 const uint64_t available=SD_MMC.totalBytes()-std::min(SD_MMC.totalBytes(),SD_MMC.usedBytes());
 if(total+2*1024*1024ULL>available){error="Not enough free SD space to stage this backup";return false;}
 return true;
}
bool swapRestore(String& error){
 if(!pikaPhoneFileWillChange("/notes")){error="Could not save index recovery intent";return false;}pikaPhoneSyncForceScan();
 if(SD_MMC.exists(stageRoot+"/notes")){
  if(SD_MMC.exists("/notes")){
   if(SD_MMC.exists(oldRoot)){error="Restore rollback already exists";return false;}
   if(!SD_MMC.rename("/notes",oldRoot)){error="Could not preserve the current notes";return false;}
  }
  if(!SD_MMC.rename(stageRoot+"/notes","/notes")){if(!SD_MMC.exists("/notes")&&SD_MMC.exists(oldRoot))SD_MMC.rename(oldRoot,"/notes");error="Could not activate restored notes";return false;}
 }
 if(!SD_MMC.exists("/notes")&&SD_MMC.exists(oldRoot)){if(!SD_MMC.rename(oldRoot,"/notes")){error="Could not recover the previous notes";return false;}}
 if(!SD_MMC.exists("/notes")){error="No complete notes dataset is available";return false;}return true;
}
#pragma pack(push,1)
struct TarHeader {char name[100],mode[8],uid[8],gid[8],size[12],mtime[12],checksum[8],type,link[100],magic[6],version[2],user[32],group[32],major[8],minor[8],prefix[155],padding[12];};
#pragma pack(pop)
static_assert(sizeof(TarHeader)==512,"TAR header size");
void tarHeader(const String& name,uint32_t bytes){
 TarHeader h={};name.toCharArray(h.name,sizeof(h.name));snprintf(h.mode,sizeof(h.mode),"%07o",0600);snprintf(h.uid,sizeof(h.uid),"%07o",0);snprintf(h.gid,sizeof(h.gid),"%07o",0);
 snprintf(h.size,sizeof(h.size),"%011llo",(unsigned long long)bytes);snprintf(h.mtime,sizeof(h.mtime),"%011llo",(unsigned long long)std::max<time_t>(0,time(nullptr)));memset(h.checksum,' ',8);h.type='0';memcpy(h.magic,"ustar",5);memcpy(h.version,"00",2);
 unsigned sum=0;for(size_t i=0;i<512;++i)sum+=reinterpret_cast<const uint8_t*>(&h)[i];snprintf(h.checksum,7,"%06o",sum);h.checksum[7]=' ';
 web->sendContent(reinterpret_cast<const char*>(&h),512);
}
void padding(uint32_t bytes){static const char zeros[512]={};const unsigned n=(512-bytes%512)%512;if(n)web->sendContent(zeros,n);}
void download(){
 if(restoring||pikaUndoActive()){json(409,"Finish the current operation before making a backup");return;}
 std::vector<PikaDataFile> files;String error;if(!pikaDataList("/notes",files,error)){json(500,error);return;}
 JsonDocument doc;doc["format"]="pika-backup";doc["version"]=1;doc["createdUtc"]=currentUtcIso();doc["revision"]=managerRevision();auto rows=doc["files"].to<JsonArray>();
 for(const auto& file:files){auto row=rows.add<JsonObject>();row["name"]=file.name;row["size"]=file.size;row["sha256"]=file.sha256;}
 String body;serializeJson(doc,body);if(body.length()>1024*1024){json(413,"Backup manifest is too large");return;}
 web->sendHeader("Content-Disposition","attachment; filename=\"pika-backup-"+currentUtcIso().substring(0,10)+".tar\"");web->sendHeader("Cache-Control","no-store");web->setContentLength(CONTENT_LENGTH_UNKNOWN);web->send(200,"application/x-tar","");
 tarHeader("pika-backup.json",body.length());web->sendContent(body.c_str(),body.length());padding(body.length());
 uint8_t buffer[2048];for(const auto& entry:files){File file=SD_MMC.open("/notes/"+entry.name);if(!file||file.size()!=entry.size){if(file)file.close();web->client().stop();return;}
  tarHeader("notes/"+entry.name,entry.size);uint32_t copied=0;while(copied<entry.size){const size_t n=file.readBytes(reinterpret_cast<char*>(buffer),std::min<size_t>(sizeof(buffer),entry.size-copied));if(!n||!web->client().connected()){file.close();web->client().stop();return;}web->sendContent(reinterpret_cast<char*>(buffer),n);copied+=n;delay(1);}file.close();padding(entry.size);
 }
 static const char end[1024]={};web->sendContent(end,sizeof(end));web->sendContent("");
}
void beginRestore(){
 if(restoring||pikaUndoActive()){json(409,"Cancel the incomplete restore or finish the current operation first");return;}
 if(plainBody().length()>1024*1024){json(413,"Backup manifest is too large");return;}
 JsonDocument request;if(deserializeJson(request,plainBody())||!request["manifest"].is<JsonObject>()){json(400,"Invalid restore request");return;}
 if(!request["revision"].is<uint32_t>()||request["revision"].as<uint32_t>()!=managerRevision()){json(409,"Data changed. Reload before restoring");return;}
 JsonDocument candidate;candidate.set(request["manifest"]);String error;
 if(!validateManifest(candidate,error)){json(400,error);return;}
 if(!pikaDataRemoveTree(stageRoot)||!pikaDataRemoveTree(oldRoot)||!SD_MMC.mkdir(stageRoot)||!SD_MMC.mkdir(stageRoot+"/notes")){json(500,"Could not prepare restore storage");return;}
 if(!pikaDataWriteJson(stageRoot+"/manifest.json",candidate)){json(500,"Could not save restore manifest");return;}
 pikaGooglePortalPause();pikaWeatherPortalPause();restoreManifest.set(candidate);initialRevision=managerRevision();uploaded.assign(candidate["files"].size(),false);
 uint8_t random[16];esp_fill_random(random,sizeof(random));char key[33];for(int i=0;i<16;i++)sprintf(key+2*i,"%02x",random[i]);key[32]=0;session=key;restoring=true;
 JsonDocument response;response["session"]=session;response["files"]=uploaded.size();String body;serializeJson(response,body);if(phoneCall){phoneCode=200;phoneOutput=body;}else web->send(200,"application/json",body);
}
void upload(){
 auto& part=web->upload();
 if(part.status==UPLOAD_FILE_START){
  uploadAccepted=false;uploadOwned=false;uploadError="";received=0;if(incoming)incoming.close();if(shaActive){mbedtls_sha256_free(&uploadSha);shaActive=false;}
  if(!pikaWebSessionValid(*web)||!viewerWriteAllowed(web->hostHeader().c_str(),web->header("Origin").c_str(),web->header("Referer").c_str(),web->header("Sec-Fetch-Site").c_str())){uploadError="Unlock Pika and open its web page directly";return;}
  if(!restoring||web->arg("session")!=session){uploadError="Restore session expired";return;}
  const String index=web->arg("index");if(!index.length()){uploadError="Missing file index";return;}for(char c:index)if(c<'0'||c>'9'){uploadError="Invalid file index";return;}
  uploadIndex=index.toInt();if(uploadIndex>=uploaded.size()){uploadError="Invalid file index";return;}
  uploadOwned=true;const String path=stagedPath(uploadIndex);if(!pikaDataParents(path)){uploadError="Could not create restore directory";return;}SD_MMC.remove(path);incoming=SD_MMC.open(path,FILE_WRITE);if(!incoming){uploadError="Could not stage file";return;}
  uploaded[uploadIndex]=false;mbedtls_sha256_init(&uploadSha);shaActive=true;if(mbedtls_sha256_starts(&uploadSha,0)){uploadError="Could not verify file";return;}uploadAccepted=true;
 }else if(part.status==UPLOAD_FILE_WRITE&&uploadAccepted){
  const uint32_t expected=restoreManifest["files"][uploadIndex]["size"]|0UL;
  if(part.currentSize>expected-received||incoming.write(part.buf,part.currentSize)!=part.currentSize||mbedtls_sha256_update(&uploadSha,part.buf,part.currentSize)){uploadError="File size, write or checksum error";uploadAccepted=false;return;}received+=part.currentSize;
 }else if(part.status==UPLOAD_FILE_END||part.status==UPLOAD_FILE_ABORTED){
  if(incoming){incoming.flush();const size_t stored=incoming.size();incoming.close();if(stored!=received)uploadError="Incomplete file write";}
  if(part.status==UPLOAD_FILE_ABORTED)uploadError="Upload cancelled";
  const uint32_t expected=restoreManifest["files"][uploadIndex]["size"]|0UL;uint8_t hash[32];char hex[65]={};
  if(shaActive){if(mbedtls_sha256_finish(&uploadSha,hash))uploadError="File checksum failed";else for(int i=0;i<32;i++)sprintf(hex+2*i,"%02x",hash[i]);mbedtls_sha256_free(&uploadSha);shaActive=false;}
  if(uploadAccepted&&(received!=expected||String(hex)!=String(restoreManifest["files"][uploadIndex]["sha256"]|"")))uploadError="Backup file checksum does not match";
  if(uploadAccepted&&!uploadError.length())uploaded[uploadIndex]=true;
  else if(uploadOwned&&restoring&&uploadIndex<uploaded.size()){uploaded[uploadIndex]=false;SD_MMC.remove(stagedPath(uploadIndex));}
  uploadAccepted=false;uploadOwned=false;
 }
}
void uploadedResponse(){if(!restoring||web->arg("session")!=session){json(409,"Restore session expired");return;}if(uploadError.length()||uploadIndex>=uploaded.size()||!uploaded[uploadIndex]){json(400,uploadError.length()?uploadError:String("No verified file received"));return;}json(200);}
void commitRestore(){
 JsonDocument request;if(deserializeJson(request,plainBody())){json(400,"Invalid commit request");return;}const String requested=request["session"]|"";
 if(requested.length()&&requested==lastCommitted){json(200);return;}
 if(!restoring||requested!=session||!(request["confirmed"]|false)){json(409,"Confirm the current restore session");return;}
 if(managerRevision()!=initialRevision){json(409,"Data changed during upload; original notes were kept");return;}
 for(size_t i=0;i<uploaded.size();++i){if(!uploaded[i]){json(400,"Some backup files are missing");return;}String hash;uint32_t size=0;if(!pikaDataHash(stagedPath(i),hash,&size)||size!=restoreManifest["files"][i]["size"].as<uint32_t>()||hash!=String(restoreManifest["files"][i]["sha256"]|"")){json(400,"Staged backup failed verification; original notes were kept");return;}}
 // Reject a damaged data store before replacing the current dataset.
 const String managerPath=stageRoot+"/notes/manager.json";
 if(SD_MMC.exists(managerPath)){JsonDocument store;if(!pikaDataReadJson(managerPath,store)||!store["records"].is<JsonArray>()){json(400,"Invalid manager data in backup; original notes were kept");return;}}
 // A durable intent precedes both directory renames, so boot can finish the swap.
 JsonDocument journal;journal["state"]="prepared";journal["session"]=session;journal["revisionFloor"]=initialRevision;
 if(!pikaDataWriteJson(restoreJournal,journal)){json(500,"Could not prepare restore transaction");return;}
 String error;if(!swapRestore(error)){json(500,error);return;}
 managerInvalidateReadSnapshot();if(!managerBumpRevision(initialRevision)){json(500,"Restored files are safe; restart to finish activation");return;}
 journal["state"]="complete";if(!pikaDataWriteJson(restoreJournal,journal)){json(500,"Restart to finish activating restored data");return;}
 lastCommitted=session;restoring=false;session="";restoreManifest.clear();uploaded.clear();pikaDataRemoveTree(stageRoot);
 if(onRestored)onRestored();json(200);
}
void cancelRestore(){
 JsonDocument request;if(deserializeJson(request,plainBody())||!(request["confirmed"]|false)){json(400,"Confirm cancellation");return;}
 if(request["session"].is<String>()&&String(request["session"]|"")!=session){json(409,"Restore session expired");return;}
 JsonDocument journal;if(pikaDataReadJson(restoreJournal,journal)&&String(journal["state"]|"")=="prepared"){json(409,"Restore activation must finish; restart Pika");return;}
 if(incoming)incoming.close();if(shaActive){mbedtls_sha256_free(&uploadSha);shaActive=false;}
 if(!pikaDataRemoveTree(stageRoot)){json(500,"Could not clean restore staging");return;}restoring=false;session="";restoreManifest.clear();uploaded.clear();json(200);
}
}
bool pikaRestoreBusy(){return restoring;}
bool pikaRestoreRecover(String& error){
 JsonDocument journal;
 if(pikaDataReadJson(restoreJournal,journal)&&String(journal["state"]|"")=="prepared"){
  if(!swapRestore(error))return false;managerInvalidateReadSnapshot();if(!managerBumpRevision(journal["revisionFloor"]|0UL)){error="Could not finalize restore revision";return false;}
  journal["state"]="complete";if(!pikaDataWriteJson(restoreJournal,journal)){error="Could not finalize restore journal";return false;}
 }
 if(String(journal["state"]|"")=="complete")lastCommitted=journal["session"]|"";
 return pikaDataRemoveTree(stageRoot);
}
void pikaBackupRoutes(WebServer& server,void (*restored)()){
 web=&server;onRestored=restored;
 server.on("/api/backup",HTTP_GET,download);
 server.on("/api/backup/restore/begin",HTTP_POST,beginRestore);
 server.on("/api/backup/restore/file",HTTP_POST,uploadedResponse,upload);
 server.on("/api/backup/restore/commit",HTTP_POST,commitRestore);
 server.on("/api/backup/restore/cancel",HTTP_POST,cancelRestore);
}

bool pikaPhoneRestoreCommand(const String& path,const String& body,String& result,int& code){
 phoneCall=true;phoneBody=body;phoneCode=500;phoneOutput="{}";
 if(path.endsWith("/begin"))beginRestore();else if(path.endsWith("/commit"))commitRestore();else if(path.endsWith("/cancel"))cancelRestore();
 else if(path.endsWith("/file")){
  JsonDocument req;if(deserializeJson(req,body)){json(400,"Invalid file request");}
  else if(!restoring||String(req["session"]|"")!=session){json(409,"Restore session expired");}
  else{const size_t index=req["index"]|size_t(4096);const uint32_t offset=req["offset"]|0UL;const String encoded=req["data"]|"";
   if(index>=uploaded.size()||encoded.length()>5464){json(400,"Invalid chunk");}
   else{std::unique_ptr<uint8_t,decltype(&heap_caps_free)> storage(static_cast<uint8_t*>(heap_caps_malloc(8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),&heap_caps_free);uint8_t* bytes=storage.get();size_t n=0;const String path=stagedPath(index);const uint32_t expected=restoreManifest["files"][index]["size"]|0UL;
    if(!bytes){json(503,"Restore buffer unavailable");}
    else if(mbedtls_base64_decode(bytes,4096,&n,(const unsigned char*)encoded.c_str(),encoded.length())||uint64_t(offset)+n>expected){json(400,"Invalid chunk data");}
    else{pikaDataParents(path);File current=SD_MMC.open(path);const uint32_t existing=current?current.size():0;bool duplicate=false;
     if(existing==offset+n&&n){current.seek(offset);uint8_t* check=bytes+4096;duplicate=current.read(check,n)==n&&!memcmp(check,bytes,n);}if(current)current.close();
     bool ok=duplicate;if(existing==offset){File out=SD_MMC.open(path,FILE_APPEND);ok=out&&out.write(bytes,n)==n;if(out){out.flush();out.close();}}
     if(!ok)json(409,"Chunk offset mismatch");else if(offset+n==expected){String hash;uint32_t size;if(pikaDataHash(path,hash,&size)&&size==expected&&hash==String(restoreManifest["files"][index]["sha256"]|"")){uploaded[index]=true;json(200);}else json(400,"File checksum mismatch");}else json(200);
    }
   }
  }
 }else json(404,"Unknown restore operation");
 result=phoneOutput;code=phoneCode;phoneCall=false;phoneBody="";phoneOutput="";return code<400;
}
