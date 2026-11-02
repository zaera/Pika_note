#include "phone_sync.h"
#include "error_log.h"
#include "device_extras.h"
#include "voice_command_policy.h"
#include "phone_link.h"
#include "activity_led.h"
#include "home_buttons.h"
#include "network.h"
#include "mail.h"
#include "device_extras.h"
#include "button_actions.h"
#include "../../config.h"
#include "mail_policy.h"
#include "mail_text.h"
#include "utf8_text.h"
#include "rtc.h"
#include "notes.h"
#include <Preferences.h>
#include <SD_MMC.h>
#include "storage_commit.h"
#include <WiFi.h>

#include <HTTPClient.h>
#include <algorithm>
#include <esp_heap_caps.h>
#include <ctime>
extern const uint8_t pika_ca_bundle_start[] asm("_binary_certs_pika_ca_bundle_pem_start");
namespace {
Preferences prefs;String clientId,clientSecret,refreshToken,accessToken,account,status="Not connected",saved;
const char* cachePath=NOTES_DIR "/.mail-cache.json";
String filters[4];int filterCount=0;time_t tokenExpiry=0;bool pending=false,unreadOnly=true,explicitRefresh=false,accountVerified=false;
std::vector<PikaMailItem> items;
String encode(const String& v){String out;const char* hex="0123456789ABCDEF";for(size_t i=0;i<v.length();i++){uint8_t c=v[i];if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')out+=(char)c;else{out+='%';out+=hex[c>>4];out+=hex[c&15];}}return out;}
String clip(const String& text,size_t bytes){size_t n=pikaUtf8Prefix(text.c_str(),bytes);return text.substring(0,n);}
bool senderValid(const String& s){int at=s.indexOf('@');if(at<=0||at==(int)s.length()-1||s.length()>254)return false;for(size_t i=0;i<s.length();i++){char c=s[i];if(!isalnum((unsigned char)c)&&c!='.'&&c!='_'&&c!='-'&&c!='+'&&c!='@')return false;}return s.indexOf('@',at+1)<0;}
void invalidateMailToken(){accessToken="";tokenExpiry=0;accountVerified=false;prefs.remove("access");prefs.remove("expires");prefs.remove("tokenFor");prefs.remove("checked");}
int request(const String& url,const String* form,JsonDocument& doc,size_t maximum=16384){
 PikaBusyLight busyLight;
 if(pikaBackgroundNetworkMode()&&pikaHomeButtonsBusy())return -5;
 PhoneSecureClient tls;tls.setCACert((const char*)pika_ca_bundle_start);tls.setHandshakeTimeout(5);HTTPClient http;http.useHTTP10(true);
 if(!http.begin(tls,url))return -1;http.setConnectTimeout(5000);http.setTimeout(7000);http.setReuse(false);
 if(form)http.addHeader("Content-Type","application/x-www-form-urlencoded");else http.addHeader("Authorization","Bearer "+accessToken);
 int code=form?http.POST(*form):http.GET();if(code==401)invalidateMailToken();String body;const int length=http.getSize();
 const char* stage=form?"token":url.indexOf("/profile")>=0?"profile":url.indexOf("format=full")>=0?"body":url.indexOf("format=metadata")>=0?"metadata":"list";
 Serial.printf("[MailHTTP] %s status=%d length=%d connected=%d\n",stage,code,length,int(http.connected()));
 // Google partial responses omit resultSizeEstimate; an empty message list
 // can therefore be HTTP 204 rather than a JSON object. There is no stream.
 if(code==204){doc.clear();http.end();return 204;}
 if(code>0&&length<=(int)maximum){auto* stream=http.getStreamPtr();
  // The server can close HTTP/1.0 immediately after headers. HTTPClient then
  // returns nullptr; never dereference it while checking available bytes.
  if(!stream){http.end();Serial.println("[Mail] response stream closed; keeping cached mail");return -2;}
  uint32_t started=millis(),last=started;int consumed=0;
  while((http.connected()||stream->available())&&(length<0||consumed<length)){
   if(pikaBackgroundNetworkMode()&&pikaHomeButtonsBusy()){code=-5;break;}
   if(millis()-started>15000||millis()-last>7000){code=-2;break;}
   int available=stream->available();if(!available){delay(2);continue;}
   char chunk[256];int n=stream->readBytes(chunk,min(available,256));if(n>0){if(body.length()+n>maximum){code=-3;break;}body.concat(chunk,n);consumed+=n;last=millis();}
  }
  if(code>0&&length>=0&&consumed!=length)code=-2;
 }else if(code>0)code=-3;
 http.end();if(code>0&&deserializeJson(doc,body)){pikaError("mail","parse","Invalid Gmail JSON",-4);return -4;}if((code<200||code>=400)&&code!=-5)pikaError("mail","HTTP","Gmail request failed",code);return code;
}
bool authorize(){
 if(accessToken.length()&&time(nullptr)<tokenExpiry-90)return true;
 String form="grant_type=refresh_token&client_id="+encode(clientId)+"&client_secret="+encode(clientSecret)+"&refresh_token="+encode(refreshToken);JsonDocument doc;
 int code=request("https://oauth2.googleapis.com/token",&form,doc);
 if(code!=200||!doc["access_token"].is<const char*>()){status=code==400?"Reconnect Google account":"Google authorization failed (HTTP "+String(code)+")";return false;}
 accessToken=doc["access_token"].as<String>();tokenExpiry=time(nullptr)+(doc["expires_in"]|3600);
 accountVerified=false;prefs.putBool("checked",false);prefs.putString("access",accessToken);prefs.putLong64("expires",tokenExpiry);prefs.putString("tokenFor",clientId+"|"+account);return true;
}
String decoded(const String& value){return String(pikaMailHeader(std::string(value.c_str())).c_str());}
String cacheSource(){String out="threads-v4|"+clientId+"|"+account+"|"+(unreadOnly?"unread7":"all");for(int i=0;i<filterCount;i++)out+="|"+filters[i];return out;}
String header(JsonArrayConst headers,const char* name){for(JsonObjectConst h:headers){String key=h["name"]|"";if(key.equalsIgnoreCase(name))return h["value"]|"";}return "";}
class MailJsonAllocator:public ArduinoJson::Allocator{public:void* allocate(size_t n)override{return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}void deallocate(void* p)override{heap_caps_free(p);}void* reallocate(void* p,size_t n)override{return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}};
void extractBody(JsonObjectConst part,int depth,String& plain,String& html);
bool list(const String& query,int quota,std::vector<PikaMailItem>& out){
 String pageToken;int added=0;std::vector<String> seen;
 const String age=unreadOnly?" newer_than:7d":"";
 for(int page=0;page<16;page++){
  JsonDocument doc;String url="https://gmail.googleapis.com/gmail/v1/users/me/messages?maxResults=32&includeSpamTrash=false&q="+encode(query+" -in:sent -in:drafts -from:me"+age)+"&fields=nextPageToken,messages(id,threadId)";
  if(pageToken.length())url+="&pageToken="+encode(pageToken);
  int code=request(url,nullptr,doc);if(code==204)return true;if(code!=200){status="Mail list failed (HTTP "+String(code)+")";pikaError("mail","operation",status);return false;}
  for(JsonObjectConst candidate:doc["messages"].as<JsonArrayConst>()){
   String id=candidate["id"]|"",thread=candidate["threadId"]|"";if(!id.length()||!thread.length()){status="Invalid mail identifiers";pikaError("mail","operation",status);return false;}
   bool known=false;for(const auto& value:seen)known|=value==thread;if(known)continue;
   int existing=-1;for(int i=0;i<(int)out.size();i++)if(out[i].threadId==thread){existing=i;break;}
   if(existing>=0&&out[existing].id==id)continue;
   bool cachedBody=false;for(const auto& old:items)if(old.id==id&&(old.bodyFetched||old.body.length()))cachedBody=true;
   MailJsonAllocator messageAllocator;JsonDocument message(&messageAllocator);
   const String format=cachedBody?"?format=metadata&metadataHeaders=From&metadataHeaders=Subject&fields=id,threadId,internalDate,labelIds,snippet,payload(headers)":"?format=full&fields=id,threadId,internalDate,labelIds,snippet,payload";
   code=request("https://gmail.googleapis.com/gmail/v1/users/me/messages/"+encode(id)+format,nullptr,message,cachedBody?16384:65536);
   // A large MIME message still has a usable subject/preview; do not lose the whole list.
   bool previewOnly=false;
   if(!cachedBody&&code==-3){
    message.clear();code=request("https://gmail.googleapis.com/gmail/v1/users/me/messages/"+encode(id)+"?format=metadata&metadataHeaders=From&metadataHeaders=Subject&fields=id,threadId,internalDate,labelIds,snippet,payload(headers)",nullptr,message);previewOnly=true;
   }
   if(code!=200){status="Mail details failed (HTTP "+String(code)+")";pikaError("mail","operation",status);return false;}
   bool excluded=false,unread=false;for(JsonVariantConst label:message["labelIds"].as<JsonArrayConst>()){if(label=="SENT"||label=="DRAFT"||label=="SPAM"||label=="TRASH")excluded=true;if(label=="UNREAD")unread=true;}if(excluded)continue;
   PikaMailItem m;m.id=id;m.threadId=thread;m.unread=unread;
   if(previewOnly)m.bodyFetched=true;
   else if(!cachedBody){String plain,html;extractBody(message["payload"].as<JsonObjectConst>(),0,plain,html);m.body=plain.length()?plain:html;m.bodyFetched=true;}
   m.from=clip(decoded(header(message["payload"]["headers"].as<JsonArrayConst>(),"From")),240);
   m.subject=clip(decoded(header(message["payload"]["headers"].as<JsonArrayConst>(),"Subject")),300);
   m.snippet=clip(String(pikaMailSnippet(std::string(message["snippet"]|"")).c_str()),600);
   if(filterCount){bool matches=false;for(int i=0;i<filterCount;i++)matches|=pikaMailSenderMatches(std::string(m.from.c_str()),std::string(filters[i].c_str()));if(!matches)continue;}
   seen.push_back(thread);
   String timestamp=message["internalDate"]|"0";m.received=strtoll(timestamp.c_str(),nullptr,10);if(m.received<=0){status="Invalid mail date";pikaError("mail","operation",status);return false;}
   if(!pikaMailEligible(unreadOnly,m.unread,m.received,int64_t(time(nullptr))*1000))continue;
   if(existing>=0){if(m.received>out[existing].received)out[existing]=m;continue;}
   out.push_back(m);if(++added>=quota||out.size()>=kPikaMailLimit)return true;
  }
  pageToken=doc["nextPageToken"]|"";if(!pageToken.length())return true;
 }
 status="Too many messages; narrow sender filters";pikaError("mail","operation",status);return false;
}

void extractBody(JsonObjectConst part,int depth,String& plain,String& html){
 if(depth>8||String(part["filename"]|"").length())return;
 const String mime=part["mimeType"]|"";
 if((mime=="text/plain"||mime=="text/html")&&part["body"]["data"].is<const char*>()){
  std::string decodedBody=pikaMailBase64Body(part["body"]["data"].as<const char*>());
  String contentType=header(part["headers"].as<JsonArrayConst>(),"Content-Type");contentType.toLowerCase();int charsetAt=contentType.indexOf("charset=");if(charsetAt>=0){String charset=contentType.substring(charsetAt+8);int end=charset.indexOf(';');if(end>=0)charset=charset.substring(0,end);charset.replace("\"","");charset.trim();decodedBody=pikaMailCharset(decodedBody,std::string(charset.c_str()));}
  if(mime=="text/html")decodedBody=pikaMailPlainHtml(decodedBody);
  decodedBody=pikaMailLatestText(decodedBody);
  String& target=mime=="text/plain"?plain:html;
  if(target.length()<8000){if(target.length())target+='\n';target+=clip(String(decodedBody.c_str()),8000-target.length());}
 }
 for(JsonObjectConst child:part["parts"].as<JsonArrayConst>())extractBody(child,depth+1,plain,html);
}
void fetchBody(PikaMailItem& item){
 if(item.bodyFetched)return;
 for(const auto& cached:items)if(cached.id==item.id&&(cached.bodyFetched||cached.body.length())){
  item.body=cached.body;item.brief=cached.brief;item.bodyFetched=true;return;
 }

 MailJsonAllocator allocator;JsonDocument doc(&allocator);
 if(request("https://gmail.googleapis.com/gmail/v1/users/me/messages/"+encode(item.id)+"?format=full&fields=payload",nullptr,doc,65536)!=200)return;
 String plain,html;extractBody(doc["payload"].as<JsonObjectConst>(),0,plain,html);item.body=plain.length()?plain:html;item.bodyFetched=true;
}
void encodeCache(JsonDocument& doc,const std::vector<PikaMailItem>& source,const String& date){doc["source"]=cacheSource();doc["saved"]=date;JsonArray array=doc["items"].to<JsonArray>();for(const auto& m:source){auto o=array.add<JsonObject>();o["id"]=m.id;o["threadId"]=m.threadId;o["from"]=m.from;o["subject"]=m.subject;o["snippet"]=m.snippet;o["body"]=m.body;o["bodyFetched"]=m.bodyFetched;o["briefV1"]=m.brief;o["received"]=m.received;o["unread"]=m.unread;}}
}
void pikaMailBegin(){prefs.begin("pikaMail",false);JsonDocument cfg;if(!deserializeJson(cfg,prefs.getString("config","{}"))){clientId=cfg["clientId"]|"";clientSecret=cfg["clientSecret"]|"";refreshToken=cfg["refreshToken"]|"";account=cfg["account"]|"";unreadOnly=true;for(JsonVariantConst f:cfg["senders"].as<JsonArrayConst>())if(filterCount<4)filters[filterCount++]=f.as<String>();}
 recoverStoredFile(SD_MMC,String(cachePath));File cacheFile=SD_MMC.open(cachePath);JsonDocument cache;if(pikaMailConfigured()&&cacheFile&&cacheFile.size()<=131072&&!deserializeJson(cache,cacheFile)&&String(cache["source"]|"")==cacheSource()){saved=cache["saved"]|"";for(JsonObjectConst o:cache["items"].as<JsonArrayConst>()){if(items.size()==kPikaMailLimit)break;PikaMailItem m;m.id=o["id"]|"";m.threadId=o["threadId"]|"";m.from=o["from"]|"";m.subject=o["subject"]|"";m.snippet=o["snippet"]|"";m.body=o["body"]|"";m.bodyFetched=o["bodyFetched"]|bool(m.body.length());m.brief=o["briefV1"]|"";m.received=o["received"]|int64_t(0);m.unread=o["unread"]|false;if(pikaMailEligible(unreadOnly,m.unread,m.received,int64_t(time(nullptr))*1000))items.push_back(m);}}
 cacheFile.close();
 if(pikaMailConfigured()&&prefs.getString("tokenFor","")==clientId+"|"+account){
  const int64_t expires=prefs.getLong64("expires",0);
  if(time(nullptr)>1700000000&&time(nullptr)<expires-90){accessToken=prefs.getString("access","");tokenExpiry=expires;accountVerified=prefs.getBool("checked",false);}
 }
 status=pikaMailConfigured()?"Configured; refresh pending":"Not connected";pending=pikaMailConfigured();}
bool pikaMailConfigured(){return clientId.length()&&clientSecret.length()&&refreshToken.length();}
void pikaMailSettingsJson(JsonObject settings){settings["mailUnreadCount"]=pikaMailUnreadCount();settings["mailUnreadOnly"]=unreadOnly;settings["mailConfigured"]=pikaMailConfigured();settings["mailAccount"]=account;settings["mailStatus"]=status;settings["mailSaved"]=saved;auto a=settings["mailSenders"].to<JsonArray>();for(int i=0;i<filterCount;i++)a.add(filters[i]);}
bool pikaMailSave(JsonObjectConst request){
 if(!pikaPhoneFileWillChange(cachePath))return false;
 if(!request["senders"].is<JsonArrayConst>()||request["senders"].size()>4)return false;
 String nextFilters[4];int n=0;for(JsonVariantConst f:request["senders"].as<JsonArrayConst>()){if(!f.is<const char*>())return false;String s=f.as<String>();s.trim();s.toLowerCase();if(!s.length())continue;if(!pikaMailFilterValid(std::string(s.c_str())))return false;for(int i=0;i<n;i++)if(nextFilters[i]==s)return false;nextFilters[n++]=s;}
 if(!request["unreadOnly"].isNull()&&!request["unreadOnly"].is<bool>())return false;const bool nextUnread=true;
 String cid=clientId,secret=clientSecret,token=refreshToken,email=account;
 if(request["credentials"].is<JsonObjectConst>()){auto c=request["credentials"].as<JsonObjectConst>();if(String(c["scope"]|"")!="https://www.googleapis.com/auth/gmail.readonly")return false;cid=c["client_id"]|"";secret=c["client_secret"]|"";token=c["refresh_token"]|"";email=c["email"]|"";if(!cid.endsWith(".apps.googleusercontent.com")||secret.length()<8||token.length()<10||!senderValid(email)||cid.length()>512||secret.length()>512||token.length()>2048)return false;}
 JsonDocument cfg;cfg["clientId"]=cid;cfg["clientSecret"]=secret;cfg["refreshToken"]=token;cfg["account"]=email;cfg["unreadOnly"]=nextUnread;auto a=cfg["senders"].to<JsonArray>();for(int i=0;i<n;i++)a.add(nextFilters[i]);String text;serializeJson(cfg,text);if(!prefs.putString("config",text))return false;
 invalidateMailToken();clientId=cid;clientSecret=secret;refreshToken=token;account=email;unreadOnly=nextUnread;filterCount=n;for(int i=0;i<n;i++)filters[i]=nextFilters[i];accessToken="";tokenExpiry=0;items.clear();saved="";SD_MMC.remove(cachePath);SD_MMC.remove(String(cachePath)+".bak");SD_MMC.remove(String(cachePath)+".tmp");pending=pikaMailConfigured();status=pending?"Configured; refresh pending":"Not connected";return true;
}
bool pikaMailDisconnect(){if(!pikaPhoneFileWillChange(cachePath))return false;if(!prefs.putString("config","{}"))return false;invalidateMailToken();SD_MMC.remove(cachePath);SD_MMC.remove(String(cachePath)+".bak");SD_MMC.remove(String(cachePath)+".tmp");clientId=clientSecret=refreshToken=accessToken=account=saved="";items.clear();filterCount=0;unreadOnly=true;pending=false;status="Not connected";return true;}
bool pikaMailAssigned(){for(int slot=0;slot<PIKA_GESTURES;++slot)if(pikaButtonAction(slot)=="mail")return true;return false;}
void pikaMailRequestRefresh(){pending=pikaMailConfigured();explicitRefresh=true;}
bool pikaMailPending(){return pending&&pikaMailConfigured()&&explicitRefresh;}
bool pikaMailRefresh(){
 if(!pikaMailPending()||!pikaPhoneOnline())return false;pending=false;explicitRefresh=false;
 if(time(nullptr)<1700000000){status="Device clock is not set";pikaError("mail","operation",status);return false;}if(!authorize())return false;
 if(!accountVerified){
  JsonDocument profile;const int code=request("https://gmail.googleapis.com/gmail/v1/users/me/profile?fields=emailAddress",nullptr,profile);
  if(code!=200||!profile["emailAddress"].is<const char*>()){status="Could not verify Gmail account";pikaError("mail","operation",status);return false;}
  if(!account.equalsIgnoreCase(profile["emailAddress"].as<String>())){invalidateMailToken();status="Account mismatch; reconnect";pikaError("mail","operation",status);return false;}
  accountVerified=true;prefs.putBool("checked",true);
 }
 std::vector<PikaMailItem> next;
 String query="in:inbox category:primary";
 if(filterCount){query="in:anywhere -in:spam -in:trash {";for(int i=0;i<filterCount;i++){if(i)query+=' ';query+="from:"+(filters[i].startsWith("@")?filters[i].substring(1):filters[i]);}query+='}';}
 if(!list(query,kPikaMailLimit,next))return false;
 std::sort(next.begin(),next.end(),[](const PikaMailItem& a,const PikaMailItem& b){return a.received>b.received;});
 for(auto& item:next)fetchBody(item);
 String stamp=currentUtcIso();MailJsonAllocator cacheAllocator;JsonDocument cache(&cacheAllocator);encodeCache(cache,next,stamp);String text;serializeJson(cache,text);const String temporary=String(cachePath)+".tmp";SD_MMC.remove(temporary);File file=SD_MMC.open(temporary,FILE_WRITE);bool stored=false;if(file){stored=file.print(text)==text.length();file.flush();stored=stored&&file.size()==text.length();file.close();}if(!stored||!commitStoredFile(SD_MMC,String(cachePath),temporary)){status="Could not save mail cache";pikaError("mail","operation",status);return false;}
 const int previousUnread=pikaMailUnreadCount();
 items=std::move(next);if(pikaMailUnreadCount()>previousUnread)pikaQueueMailNotify();saved=stamp;status=items.empty()?"Connected; no matching mail":"Connected; mail updated";return true;
}
const std::vector<PikaMailItem>& pikaMailItems(){return items;}
String pikaMailStatus(){return status;}
String pikaMailSaved(){return saved;}
String pikaMailDate(int64_t ms){time_t seconds=ms/1000;struct tm t={};localtime_r(&seconds,&t);char out[24];strftime(out,sizeof(out),"%d.%m %H:%M",&t);return out;}
String pikaMailItemSpeech(int index,bool includeSubject){
 if(index<0||index>=(int)items.size())return "";
 const auto& m=items[index];String sender=m.from;int angle=sender.indexOf('<');if(angle>0)sender=sender.substring(0,angle);sender.trim();
 String body=m.body.length()?m.body:m.snippet;
 return String(pikaMailSpeechClean(std::string(("От "+sender+". "+(includeSubject?m.subject+". ":String())+clip(body,700)).c_str())).c_str());
}
String pikaMailSpeech(int page){String out;for(int i=page*4;i<(int)items.size()&&i<page*4+4;i++)out+=pikaMailItemSpeech(i)+"\n";return out;}

bool pikaMailSaveBrief(const String& id,const String& speech){
 if(!speech.length()||speech.length()>1800)return false;
 for(auto& item:items)if(item.id==id){
  item.brief=speech;JsonDocument doc;encodeCache(doc,items,saved);
  const String temporary=String(cachePath)+".tmp";SD_MMC.remove(temporary);
  File file=SD_MMC.open(temporary,FILE_WRITE);if(!file)return false;
  const size_t expected=measureJson(doc),written=serializeJson(doc,file);file.flush();
  const bool ok=written==expected&&file.size()==expected;file.close();
  return ok&&commitStoredFile(SD_MMC,String(cachePath),temporary);
 }return false;
}

int pikaMailUnreadCount(){int count=0;for(const auto& m:items)if(m.unread&&pikaMailEligible(true,m.unread,m.received,int64_t(time(nullptr))*1000))++count;return count;}
bool pikaMailQuestion(const String& question){String q=question;q.toLowerCase();const char* words[]={"mail","inbox","email","почт","письм","имейл","емейл","Почт","Письм","Имейл"};for(const auto* word:words)if(q.indexOf(word)>=0)return true;return false;}
String pikaMailAskContext(){
 if(!pikaMailConfigured())return "Gmail is not configured.";
 MailJsonAllocator allocator;JsonDocument doc(&allocator);doc["readOnly"]=true;doc["savedUtc"]=saved;doc["status"]=status;doc["unreadMatchingCount"]=pikaMailUnreadCount();doc["scope"]="Up to four filtered unread messages from the last seven days; not the whole Gmail account.";
 auto rows=doc["messages"].to<JsonArray>();for(const auto& m:items){if(!pikaMailEligible(true,m.unread,m.received,int64_t(time(nullptr))*1000))continue;auto row=rows.add<JsonObject>();row["from"]=m.from;row["subject"]=m.subject;String body=m.body.length()?m.body:m.snippet;row["body"]=String(pikaMailSpeechClean(std::string(clip(body,3000).c_str())).c_str());row["truncated"]=body.length()>3000;}
 String out;serializeJson(doc,out);return out;
}

bool pikaMailModificationRequested(const String& question){if(!pikaMailQuestion(question))return false;const auto q=voiceCommandFold(question.c_str());for(const char* action:{"удали","удалить","сотри","заархивируй","отправь","перешли","пометь","отметь","delete","remove","archive","send","forward","mark","reply"})if(voiceCommandHas(q,action))return true;return false;}
