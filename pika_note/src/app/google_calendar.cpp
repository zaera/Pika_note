#include "error_log.h"
#include "phone_link.h"
#include "deferred_http.h"
#include "http_replay.h"
#include "activity_led.h"
#include "home_buttons.h"
#include "network.h"
#include "google_calendar.h"
#include "settings.h"
#include "tasks.h"
#include "notes.h"
#include "rtc.h"
#include "manager_store.h"
#include "utf8_text.h"
#include "storage_commit.h"
#include "../../config.h"
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <WiFi.h>

#include <HTTPClient.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <esp_random.h>
#include <esp_mac.h>
#include <algorithm>

extern const uint8_t pika_ca_bundle_start[] asm("_binary_certs_pika_ca_bundle_pem_start");
namespace {
const char* kManifest=NOTES_DIR "/google-sync.json";
String accessToken,status="Not configured";
time_t tokenExpiry=0;
uint32_t nextAttempt=0;
bool verified=false,verifyRequested=false;
bool lastTickProgress=false;
int preferredBatchNote=-1;
constexpr int kTransportDeferred=-100;
bool portalAsync=false,portalWaiting=false,portalDeferred=false;
String portalKey;PikaHttpReplay portalReplies;
bool importActive=false,importStatusVisible=false;
String importFrom,importUntil,importPage;
unsigned importAdded=0,importSkipped=0;
int rng(void*,unsigned char* output,size_t length){esp_fill_random(output,length);return 0;}
String urlEncode(const String& value){const char hex[]="0123456789ABCDEF";String out;for(size_t i=0;i<value.length();++i){const uint8_t c=value[i];if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')out+=(char)c;else{out+='%';out+=hex[c>>4];out+=hex[c&15];}}return out;}
String base64Url(const uint8_t* bytes,size_t count){std::vector<unsigned char> out(((count+2)/3)*4+1);size_t length=0;if(mbedtls_base64_encode(out.data(),out.size(),&length,bytes,count))return "";out[length]=0;String result=(char*)out.data();result.replace("+","-");result.replace("/","_");result.replace("=","");return result;}
String digest(const String& text){uint8_t hash[32];mbedtls_sha256((const unsigned char*)text.c_str(),text.length(),hash,0);const char hex[]="0123456789abcdef";String out;for(uint8_t b:hash){out+=hex[b>>4];out+=hex[b&15];}return out;}
int request(const String& url,const char* method,const String& body,String& response,bool tokenRequest=false){
  PikaBusyLight busyLight;
  if(portalAsync){
    const String key=method+url+digest(tokenRequest?pikaGoogleServiceJson():body)+digest(tokenRequest?String():accessToken);
    int code;if(portalReplies.read(key,response,code)){if(code==401){accessToken="";tokenExpiry=0;verified=false;}return code;}
    portalDeferred=true;
    if(pikaDeferredHttpStart(1,url,method,body,tokenRequest?String():accessToken)){portalWaiting=true;portalKey=key;}
    return kTransportDeferred;
  }
  if(pikaBackgroundNetworkMode()&&pikaHomeButtonsBusy())return -1;
  PhoneSecureClient client;client.setCACert(reinterpret_cast<const char*>(pika_ca_bundle_start));client.setHandshakeTimeout(4);
  HTTPClient http;if(!http.begin(client,url))return -1;
  http.setConnectTimeout(4000);http.setTimeout(5000);http.setReuse(false);
  http.addHeader("Content-Type",tokenRequest?"application/x-www-form-urlencoded":"application/json");
  if(!tokenRequest)http.addHeader("Authorization","Bearer "+accessToken);
  int code=http.sendRequest(method,(uint8_t*)body.c_str(),body.length());
  if(code>0&&http.getSize()<=16384)response=http.getString();
  http.end();
  if(code==401){accessToken="";tokenExpiry=0;verified=false;}
  if(code<200||code>=400)pikaError("calendar","HTTP","Google Calendar request failed",code);
  return code;
}
bool authorize(){
  if(accessToken.length()&&time(nullptr)<tokenExpiry-90)return true;
  if(time(nullptr)<1700000000){status="Device clock is not set";pikaError("google_calendar","operation",status);return false;}
  JsonDocument service;if(deserializeJson(service,pikaGoogleServiceJson())){status="Invalid service account JSON";pikaError("google_calendar","operation",status);return false;}
  JsonDocument claims;claims["iss"]=service["client_email"];claims["scope"]="https://www.googleapis.com/auth/calendar.events";claims["aud"]="https://oauth2.googleapis.com/token";
  claims["iat"]=(long long)time(nullptr);claims["exp"]=(long long)time(nullptr)+3600;
  String data;serializeJson(claims,data);
  const String header="{\"alg\":\"RS256\",\"typ\":\"JWT\"}";
  String signing=base64Url((const uint8_t*)header.c_str(),header.length())+"."+base64Url((const uint8_t*)data.c_str(),data.length());
  const String privateKey=service["private_key"]|"";
  mbedtls_pk_context pk;mbedtls_pk_init(&pk);
  int result=mbedtls_pk_parse_key(&pk,(const unsigned char*)privateKey.c_str(),privateKey.length()+1,nullptr,0,rng,nullptr);
  unsigned char hash[32],signature[512];size_t length=0;
  mbedtls_sha256((const unsigned char*)signing.c_str(),signing.length(),hash,0);
  if(!result)result=mbedtls_pk_sign(&pk,MBEDTLS_MD_SHA256,hash,sizeof(hash),signature,sizeof(signature),&length,rng,nullptr);
  mbedtls_pk_free(&pk);
  if(result){status="Could not sign service account token";pikaError("google_calendar","operation",status);return false;}
  const String jwt=signing+"."+base64Url(signature,length);
  String response;
  const int code=request("https://oauth2.googleapis.com/token","POST","grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer&assertion="+urlEncode(jwt),response,true);
  if(code==kTransportDeferred)return false;
  JsonDocument token;
  if(code!=200||deserializeJson(token,response)||!token["access_token"].is<const char*>()){status="Google authorization failed (HTTP "+String(code)+")";pikaError("google_calendar","operation",status);return false;}
  accessToken=token["access_token"].as<String>();tokenExpiry=time(nullptr)+(token["expires_in"]|3600);status="Authorized; sync queued";return true;
}
bool readManifest(JsonDocument& doc){recoverStoredFile(SD_MMC,String(kManifest));File file=SD_MMC.open(kManifest);if(!file){doc["items"].to<JsonObject>();return true;}const auto error=deserializeJson(doc,file);file.close();return !error;}
bool writeManifest(JsonDocument& doc){const String temp=String(kManifest)+".tmp";SD_MMC.remove(temp);File file=SD_MMC.open(temp,FILE_WRITE);if(!file)return false;const size_t expected=measureJson(doc),written=serializeJson(doc,file);file.flush();bool ok=written==expected&&file.size()==expected;file.close();return ok&&commitStoredFile(SD_MMC,String(kManifest),temp);}
String importedPrefix(){return "gcal-"+digest(pikaGoogleCalendarId()).substring(0,32)+"-";}
String eventId(const String& id){const String prefix=importedPrefix();if(id.startsWith(prefix))return id.substring(prefix.length());uint8_t bytes[6];esp_read_mac(bytes,ESP_MAC_WIFI_STA);char mac[13];snprintf(mac,sizeof(mac),"%02x%02x%02x%02x%02x%02x",bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5]);return String("pika")+mac+digest(id).substring(0,40);}
String shiftUtc(const String& utc,int seconds){struct tm t={};sscanf(utc.c_str(),"%d-%d-%dT%d:%d",&t.tm_year,&t.tm_mon,&t.tm_mday,&t.tm_hour,&t.tm_min);t.tm_year-=1900;t.tm_mon-=1;time_t epoch=utcTmToEpoch(t)+seconds;struct tm out={};gmtime_r(&epoch,&out);char buffer[25];strftime(buffer,sizeof(buffer),"%Y-%m-%dT%H:%M:%SZ",&out);return buffer;}
String nextDay(const String& day){return shiftUtc(day+"T00:00:00Z",86400).substring(0,10);}
String eventPayload(const BrainTaskRecord& task){JsonDocument event;event["summary"]=task.text;event["description"]="Pika event. Priority: "+String(task.priority==3?"Urgent":task.priority==2?"High":task.priority==0?"Low":"Normal");event["extendedProperties"]["private"]["pikaTaskId"]=task.id;
  if(task.datePrecision=="day"){event["start"]["date"]=managerUtcToLocalInput(task.dueUtc,true);const String end=task.endUtc.length()?managerUtcToLocalInput(task.endUtc,true):managerUtcToLocalInput(task.dueUtc,true);event["end"]["date"]=nextDay(end);}
  else{event["start"]["dateTime"]=task.dueUtc;event["start"]["timeZone"]="Europe/Kyiv";event["end"]["dateTime"]=task.endUtc.length()?task.endUtc:shiftUtc(task.dueUtc,3600);event["end"]["timeZone"]="Europe/Kyiv";}
  event["colorId"]=task.priority>=2?"11":task.priority==0?"2":"9";
  String payload;serializeJson(event,payload);return payload;
}
// Google dateTime carries an explicit RFC3339 offset. Convert that instant,
// rather than treating a remote time zone as the device's local zone.
String googleUtc(const String& input){
  if(input.length()<20)return "";
  int y,m,d,h,n,sec;
  if(sscanf(input.c_str(),"%d-%d-%dT%d:%d:%d",&y,&m,&d,&h,&n,&sec)!=6)return "";
  if(y<2020||y>2099||m<1||m>12||d<1||d>31||h>23||n>59||sec>59)return "";
  struct tm value={};value.tm_year=y-1900;value.tm_mon=m-1;value.tm_mday=d;value.tm_hour=h;value.tm_min=n;value.tm_sec=sec;
  time_t epoch=utcTmToEpoch(value);
  int zone=19;while(zone<(int)input.length()&&input[zone]!='Z'&&input[zone]!='+'&&input[zone]!='-')zone++;
  if(zone>=(int)input.length())return "";
  if(input[zone]!='Z'){int zh,zm;if(sscanf(input.c_str()+zone+1,"%d:%d",&zh,&zm)!=2||zh>23||zm>59)return "";epoch+=(input[zone]=='+'?-1:1)*(zh*3600+zm*60);}
  struct tm utc={};gmtime_r(&epoch,&utc);char out[25];strftime(out,sizeof(out),"%Y-%m-%dT%H:%M:%SZ",&utc);return out;
}
void importTick(){
  const String calendar=pikaGoogleCalendarId();
  String url="https://www.googleapis.com/calendar/v3/calendars/"+urlEncode(calendar)+"/events?singleEvents=true&showDeleted=false&maxResults=10&orderBy=startTime&timeMin="+urlEncode(importFrom)+"&timeMax="+urlEncode(importUntil)+"&fields="+urlEncode("nextPageToken,items(id,status,summary,start,end,extendedProperties)");
  if(importPage.length())url+="&pageToken="+urlEncode(importPage);
  String response;const int code=request(url,"GET","",response);
  if(code==kTransportDeferred)return;
  JsonDocument page;
  if(code!=200||deserializeJson(page,response)||!page["items"].is<JsonArray>()){
    importActive=false;status="Import failed (HTTP "+String(code)+"). Saved events are kept; try again.";return;
  }
  JsonDocument journal;if(!readManifest(journal)){importActive=false;status="Sync journal unreadable";pikaError("google_calendar","operation",status);return;}
  if(String(journal["calendar"]|"")!=calendar){journal.clear();journal["calendar"]=calendar;journal["items"].to<JsonObject>();}
  const auto local=loadBrainTasks();
  for(JsonObject remote:page["items"].as<JsonArray>()){
    const String remoteId=remote["id"]|"";
    if(!remoteId.length()||String(remote["status"]|"")=="cancelled"){importSkipped++;continue;}
    bool known=false;for(const auto& task:local)if(task.type=="event"&&eventId(task.id)==remoteId){known=true;break;}
    const String origin=remote["extendedProperties"]["private"]["pikaTaskId"]|"";
    if(origin.length()&&eventId(origin)==remoteId&&managerKnownRecord(origin)){importSkipped++;continue;}
    const String localId=importedPrefix()+remoteId;
    // The stored baseline recovers a journal failure without losing local edits.
    if(known){const String baseline=managerGoogleBaseline(localId);if(baseline.length()&&!journal["items"][remoteId].is<const char*>())journal["items"][remoteId]=baseline;importSkipped++;continue;}
    BrainTaskRecord task;task.id=localId;task.type="event";task.noteNum=-1;task.priority=1;task.text=remote["summary"]|"Untitled event";
    task.text.trim();if(!task.text.length())task.text="Untitled event";
    if(task.text.length()>1500)task.text=task.text.substring(0,pikaUtf8Prefix(task.text.c_str(),1500));
    const bool allDay=remote["start"]["date"].is<const char*>();
    task.datePrecision=allDay?"day":"time";
    if(allDay){
      const String start=remote["start"]["date"]|"",end=remote["end"]["date"]|"";bool valid=false;
      task.dueUtc=managerLocalToUtc(start,true,&valid);if(!valid||!task.dueUtc.length()||end<=start){importSkipped++;continue;}
      const String inclusive=shiftUtc(end+"T00:00:00Z",-86400).substring(0,10);
      task.endUtc=managerLocalToUtc(inclusive,true,&valid);if(!valid||!task.endUtc.length()){importSkipped++;continue;}
    }else{task.dueUtc=googleUtc(remote["start"]["dateTime"]|"");task.endUtc=googleUtc(remote["end"]["dateTime"]|"");}
    if(!task.dueUtc.length()||!task.endUtc.length()||task.endUtc<task.dueUtc){importSkipped++;continue;}
    const String hash=digest(eventPayload(task));String error;
    const int result=managerImportGoogleEvent(task,hash,error);
    if(result<0){importActive=false;status=error;return;}
    if(result==1){journal["items"][remoteId]=hash;importAdded++;}else importSkipped++;
  }
  if(!writeManifest(journal)){importActive=false;status="Import paused: sync journal write failed; try again";pikaError("google_calendar","operation",status);return;}
  importPage=page["nextPageToken"]|"";importActive=importPage.length()>0;
  verified=true;nextAttempt=millis()+1000;
  status=(importActive?"Importing: ":"Import complete: ")+String(importAdded)+" added, "+String(importSkipped)+" existing or skipped";
}

}

String pikaGoogleStatus(){if(!pikaGoogleServiceJson().length())return "Not connected";if(!pikaGoogleSyncEnabled()&&!importStatusVisible)return "Configured; sync disabled";return status;}
bool pikaGoogleVerified(){return verified;}
bool pikaGoogleHasPendingChanges(){
 if(importActive)return true;
 if(!pikaGoogleSyncEnabled()||!pikaGoogleCalendarId().length())return false;
 if(importActive||verifyRequested)return true;
 JsonDocument state;if(!readManifest(state))return true;
 if(String(state["calendar"]|"")!=pikaGoogleCalendarId()){state.clear();state["items"].to<JsonObject>();}
 const auto tasks=loadBrainTasks();
 for(const auto& task:tasks){
  if(task.type!="event"||task.done||!task.dueUtc.length())continue;
  const String id=eventId(task.id);String previous=state["items"][id]|"";
  if(!previous.length()&&task.id.startsWith(importedPrefix()))previous=managerGoogleBaseline(task.id);
  if(previous!=digest(eventPayload(task)))return true;
 }
 for(JsonPair item:state["items"].as<JsonObject>()){
  bool present=false;for(const auto& task:tasks)if(task.type=="event"&&!task.done&&task.dueUtc.length()&&eventId(task.id)==item.key().c_str()){present=true;break;}
  if(!present)return true;
 }
 return false;
}
void pikaGoogleScheduleSync(){nextAttempt=millis()+3000;if(pikaGoogleSyncEnabled())status="Changes queued for sync";}
void pikaGoogleResetSession(){verifyRequested=false;importActive=false;importStatusVisible=false;accessToken="";tokenExpiry=0;verified=false;status="Configured; not verified";nextAttempt=millis()+3000;}
void pikaGoogleSyncTick(bool force){
  lastTickProgress=false;
  if((!pikaGoogleSyncEnabled()&&!importActive)||!pikaGoogleCalendarId().length()||!pikaPhoneOnline())return;
  if(!force&&static_cast<int32_t>(millis()-nextAttempt)<0)return;
  nextAttempt=millis()+15000;
  if(!pikaGoogleHasPendingChanges()){status=verified?"Synced":"Configured; no event changes";return;}
  if(!accessToken.length()||time(nullptr)>=tokenExpiry-90){if(!authorize())nextAttempt=millis()+60000;else lastTickProgress=true;return;}
  if(verifyRequested){
    String response;const int code=request("https://www.googleapis.com/calendar/v3/calendars/"+urlEncode(pikaGoogleCalendarId())+"/events?maxResults=1&fields=kind","GET","",response);
    if(code==kTransportDeferred)return;
    verifyRequested=false;verified=code==200;status=verified?"Connected; sync checked":"Calendar check failed (HTTP "+String(code)+")";lastTickProgress=verified;return;
  }
  if(importActive){importTick();return;}
  JsonDocument state;if(!readManifest(state)){status="Sync journal unreadable";pikaError("google_calendar","operation",status);return;}
  const String calendar=pikaGoogleCalendarId();
  if(String(state["calendar"]|"")!=calendar){state.clear();state["calendar"]=calendar;state["items"].to<JsonObject>();}
  const String prefix="https://www.googleapis.com/calendar/v3/calendars/"+urlEncode(calendar)+"/events";
  auto tasks=loadBrainTasks();
  if(preferredBatchNote>0)std::stable_partition(tasks.begin(),tasks.end(),[](const BrainTaskRecord& task){return task.noteNum==preferredBatchNote;});
  for(const BrainTaskRecord& task:tasks){
    if(task.type!="event"||task.done||!task.dueUtc.length())continue;
    const String id=eventId(task.id),payload=eventPayload(task),hash=digest(payload);
    String previousHash=state["items"][id]|"";
    if(!previousHash.length()&&task.id.startsWith(importedPrefix()))previousHash=managerGoogleBaseline(task.id);
    if(previousHash==hash)continue;
    String response;int code;
    if(previousHash.length())code=request(prefix+"/"+id,"PATCH",payload,response);
    else {JsonDocument create;deserializeJson(create,payload);create["id"]=id;String body;serializeJson(create,body);code=request(prefix,"POST",body,response);}
    if(code==kTransportDeferred)return;
    if(code==404){JsonDocument create;deserializeJson(create,payload);create["id"]=id;String body;serializeJson(create,body);code=request(prefix,"POST",body,response);}
    if(code==409){code=request(prefix+"/"+id,"PATCH",payload,response);}
    if(code==kTransportDeferred)return;
    if(code>=200&&code<300){state["items"][id]=hash;if(!writeManifest(state)){status="Event saved; sync journal write failed";pikaError("google_calendar","operation",status);return;}lastTickProgress=true;verified=true;status="Event synced";}
    else{status="Sync failed (HTTP "+String(code)+"). Check Calendar ID and sharing.";nextAttempt=millis()+60000;}
    return; // At most one event per idle tick.
  }
  String removeId;
  for(JsonPair pair:state["items"].as<JsonObject>()){
    bool present=false;for(const auto& task:tasks)if(task.type=="event"&&!task.done&&task.dueUtc.length()&&eventId(task.id)==pair.key().c_str()){present=true;break;}
    if(!present){removeId=pair.key().c_str();break;}
  }
  if(removeId.length()){
    String response;const int code=request(prefix+"/"+removeId,"DELETE","",response);
    if(code==kTransportDeferred)return;
    if((code>=200&&code<300)||code==404||code==410){state["items"].remove(removeId);if(!writeManifest(state)){status="Sync journal write failed";pikaError("google_calendar","operation",status);return;}lastTickProgress=true;verified=true;status="Event deletion synced";}
    else{status="Delete sync failed (HTTP "+String(code)+")";nextAttempt=millis()+60000;}return;
  }
  status=verified?"Synced":"Configured; no event changes";
}

void pikaGoogleSyncBatch(int preferredNote){
  if(!pikaGoogleSyncEnabled()||!pikaPhoneOnline()||!pikaGoogleHasPendingChanges())return;
  // Auth is a separate tick. Drain a small batch without 15-second idle gaps.
  // Stop on the first failure, no changes, or elapsed budget. The manifest is
  // durable: unacknowledged events are retried on a later wake/connected pass.
  preferredBatchNote=preferredNote;
  const uint32_t started=millis();
  for(unsigned tick=0;tick<5&&millis()-started<12000UL;tick++){
    if(pikaBackgroundNetworkMode()&&pikaHomeButtonsBusy())break;
    pikaGoogleSyncTick(true);
    if(!lastTickProgress)break;
    delay(1);
  }
  preferredBatchNote=-1;
}

bool pikaGoogleRequestImport(const String& fromDay,const String& throughDay){
  if(!pikaGoogleServiceJson().length()||!pikaGoogleCalendarId().length()||!pikaPhoneOnline())return false;
  if(importActive)return true;
  bool valid=false;const String from=managerLocalToUtc(fromDay,true,&valid);if(!valid||!from.length())return false;
  managerLocalToUtc(throughDay,true,&valid);if(!valid)return false;
  const String until=managerLocalToUtc(nextDay(throughDay),true,&valid);if(!valid||!until.length()||from>=until)return false;
  importFrom=from;importUntil=until;importPage="";importAdded=0;importSkipped=0;
  importActive=true;importStatusVisible=true;status="Google import queued";nextAttempt=millis();return true;
}

void pikaGoogleRequestVerify(){verifyRequested=true;nextAttempt=0;}

bool pikaGoogleForgetLocalEvents(const std::vector<String>& ids){
 if(ids.empty())return true;JsonDocument state;if(!readManifest(state))return false;
 for(const String& id:ids)state["items"].remove(eventId(id));
 return writeManifest(state);
}

void pikaGooglePortalTick(){
 if(portalWaiting){PikaDeferredResponse result;if(!pikaDeferredHttpPoll(1,result))return;
  portalReplies.append(portalKey,result.body,result.code);portalWaiting=false;
 }else if(portalReplies.empty()&&int32_t(millis()-nextAttempt)<0)return;
 if(!pikaPhoneOnline()){portalReplies.clear();return;}
 portalAsync=true;portalDeferred=false;portalReplies.begin();
 pikaGoogleSyncTick(true);
 portalAsync=false;
 if(portalDeferred){nextAttempt=millis()+50;status="Syncing in background";}
 else portalReplies.clear();
}

void pikaGooglePortalPause(){pikaDeferredHttpCancel(1);portalWaiting=false;portalReplies.clear();}
