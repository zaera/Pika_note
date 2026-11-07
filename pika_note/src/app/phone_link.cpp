#include "rtc.h"
#include "device_owner.h"
#include "phone_sync.h"
#include "sleep.h"
#include "phone_link.h"
#include "phone_lan.h"
#include "web_pin.h"
#include "activity_led.h"
#include "ui.h"
#include "home_buttons.h"
extern bool pikaBackgroundNetworkMode();
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <atomic>
#include <sys/time.h>
namespace {
constexpr char serviceUuid[]="3a6b0001-54ac-4c91-8f8a-8b22e4f64821",rxUuid[]="3a6b0002-54ac-4c91-8f8a-8b22e4f64821",txUuid[]="3a6b0003-54ac-4c91-8f8a-8b22e4f64821";
constexpr size_t ringSize=65536;struct Channel{std::atomic<int> state{0};uint8_t* ring=nullptr;std::atomic<uint32_t> head{0},tail{0},speechToken{0},speechReceived{0},uploadToken{0},uploadAccepted{0},uploadAckVersion{0};uint32_t uploadSent=0;};Channel channels[8];
NimBLEServer* server=nullptr;NimBLECharacteristic* tx=nullptr;std::atomic<bool> bulkSubscribed{false},indicationSubscribed{false};std::atomic<uint32_t> bulkConfirmed{0};std::atomic<uint8_t> bulkChannel{0};SemaphoreHandle_t bulkAck=nullptr;
std::atomic<bool> uploadFast{false},uploadAllowed{true};std::atomic<bool> online{false},welcome{false},hello{false},secure{false},subscribed{false};std::atomic<uint16_t> mtu{23};uint16_t conn=0xffff,sequence=0;String nonce,rpc,authMessage;uint8_t rpcChannel=0;std::atomic<bool> rpcReady{false};SemaphoreHandle_t txMutex=nullptr,ack=nullptr,rpcMutex=nullptr,slotMutex=nullptr;std::atomic<int> ackCode{0};uint32_t failedUntil=0;unsigned failures=0;std::atomic<uint32_t> trafficAt{0},generation{0};std::atomic<bool> homeDirty{false};std::atomic<uint32_t> homeChangedAt{0};std::atomic<uint32_t> lastSentGeneration{0};bool previouslyBonded=false;uint32_t pairCode=0;bool displayedOnline=false;uint32_t connectedAt=0,forceSyncAt=0;
std::atomic<bool> foreground{false},interrupted{false},shuttingDown{false},foregroundNotice{false},forceNotice{false};std::atomic<uint32_t> session{0},rpcAt{0};QueueHandle_t replyQueue=nullptr;bool indicationPending=false;
struct LiveJob{uint8_t* data;size_t size;uint32_t epoch,token;};QueueHandle_t liveQueue=nullptr;
// One connection and one outstanding authentication. The BLE host callback
// only copies bytes; JSON/HMAC/NVS work runs on the control worker's stack.
struct AuthJob{char body[513],peer[24],challenge[65];uint32_t epoch;bool bonded;};
AuthJob authJob{};std::atomic<bool> authPending{false};String peerIdentity;
struct VoiceAck{uint8_t channel;uint32_t token,received,epoch;};QueueHandle_t voiceAcks=nullptr;
struct ReplyJob{uint8_t channel;int code;uint32_t epoch;size_t size;uint8_t* data;bool bulk;};
bool speechReceiving(){for(const auto& c:channels){const int state=c.state.load();if(c.speechToken.load()!=0&&(state==1||state==2))return true;}return false;}
bool preemptBackground(){if(pikaBackgroundNetworkMode()&&(pikaHomeButtonsBusy()||rpcReady.load())){interrupted=true;return true;}return false;}
bool finishIndication(bool interruptible){
 const uint32_t started=millis();
 while(indicationPending){
  if(xSemaphoreTake(ack,pdMS_TO_TICKS(5))==pdTRUE){indicationPending=false;return ackCode.load()==BLE_HS_EDONE;}
  if(interruptible&&preemptBackground())return false; // The next TX owner drains this ACK.
  if(shuttingDown||conn==0xffff){indicationPending=false;return false;}
  if(millis()-started>=8000){indicationPending=false;if(server&&conn!=0xffff)server->disconnect(conn);return false;}
 }
 return true;
}
bool sendBulk(uint8_t channel,const uint8_t* data,size_t size,uint32_t offset,uint32_t epoch){
 if(xSemaphoreTake(txMutex,pdMS_TO_TICKS(10000))!=pdTRUE)return false;
 if(!finishIndication(false)){xSemaphoreGive(txMutex);return false;}
 bool ok=true;size_t at=0;
 while(at<size&&ok){
  if(!online||session!=epoch){ok=false;break;}
  const size_t count=std::min<size_t>(size-at,std::min<size_t>(500,std::max<int>(1,int(mtu.load())-9)));
  uint8_t frame[506];frame[0]=24|((at+count==size)?128:0);frame[1]=channel;const uint16_t seq=sequence++;frame[2]=seq;frame[3]=seq>>8;frame[4]=count;frame[5]=count>>8;memcpy(frame+6,data+at,count);
  const uint32_t started=millis();while(!tx->notify(frame,count+6,conn)){if(!online||session!=epoch||millis()-started>1000){ok=false;break;}vTaskDelay(pdMS_TO_TICKS(2));}
  if(ok){at+=count;trafficAt=millis();pikaActivityLedTraffic(true);}
 }
 xSemaphoreGive(txMutex);if(!ok)return false;
 const uint32_t expected=offset+size,started=millis();while(online&&session==epoch&&bulkConfirmed.load()<expected&&millis()-started<5000)xSemaphoreTake(bulkAck,pdMS_TO_TICKS(20));
 if(bulkConfirmed.load()!=expected)return false;pikaPhoneSyncProgress();return true;
}
void replyWorker(void*){for(;;){
 ReplyJob job;if(xQueueReceive(replyQueue,&job,portMAX_DELAY)!=pdTRUE)continue;
 auto wait=[&](){while((foreground||pikaHomeButtonsBusy())&&online&&session==job.epoch)vTaskDelay(pdMS_TO_TICKS(5));return online&&session==job.epoch;};
 if(wait()){
  const bool bulk=!pikaLanOnline()&&job.bulk&&bulkSubscribed&&mtu.load()>=185&&job.size>=1024;bulkChannel=job.channel;bulkConfirmed=0;
  JsonDocument doc;doc["status"]=job.code;doc["length"]=job.size;if(bulk)doc["window"]=4000;
  String head;serializeJson(doc,head);if(pikaLanOnline())Serial.printf("LAN_META ch=%u size=%u heap=%u foreground=%d\n",job.channel,unsigned(job.size),ESP.getFreeHeap(),foreground.load());bool ok=pikaPhoneSend(23,job.channel,(const uint8_t*)head.c_str(),head.length());
  for(size_t at=0;ok&&at<job.size;){if(!wait()){ok=false;break;}const size_t n=std::min<size_t>(bulk?4000:(pikaLanOnline()?32000:2000),job.size-at);ok=bulk?sendBulk(job.channel,job.data+at,n,at,job.epoch):pikaPhoneSend(24,job.channel,job.data+at,n);at+=n;vTaskDelay(1);}
  if(pikaLanOnline())Serial.printf("LAN_END ch=%u ok=%d size=%u heap=%u\n",job.channel,ok,unsigned(job.size),ESP.getFreeHeap());if(!ok)Serial.printf("PIKA_REPLY_FAIL channel=%u bytes=%u lan=%d heap=%u\n",job.channel,unsigned(job.size),pikaLanOnline(),ESP.getFreeHeap());if(wait())pikaPhoneSend(25,job.channel,nullptr,0);bulkChannel=0;
 }
 heap_caps_free(job.data);
}}

// Never wait for a phone indication from the UI/button/recording owner loop.
// Coalesced control notices use their own worker; foreground pauses data at once.
void controlWorker(void*){
 LiveJob live{};size_t liveAt=0;
 for(;;){
  if(live.data&&(!online||live.epoch!=session.load())){heap_caps_free(live.data);live={};liveAt=0;}
  if(!live.data&&liveQueue&&xQueueReceive(liveQueue,&live,0)==pdTRUE)liveAt=0;
  if(server&&!shuttingDown){
   if(authPending.load(std::memory_order_acquire)){
    if(authJob.epoch==session.load()&&!online){
     JsonDocument doc;bool parsed=!deserializeJson(doc,authJob.body);
     bool valid=parsed&&(doc["version"]|0)==3&&authJob.bonded&&pikaOwnerAuthenticate(authJob.challenge,doc["proof"]|"",doc["ownerKey"]|"",authJob.peer,previouslyBonded);
     if(authJob.epoch==session.load()){
      Serial.printf("PIKA_OWNER_AUTH ok=%d bound=%d legacy=%d stack=%u\n",valid,pikaOwnerBound(),pikaOwnerLegacyBondOnly(),uxTaskGetStackHighWaterMark(nullptr));
      if(valid){uploadFast=(doc["uploadWindow"]|0)==4096;online=true;welcome=true;nonce="";failures=0;const int64_t epoch=doc["epoch"]|int64_t(0);if(epoch>=1700000000&&epoch<4102444800LL){pikaRtcQueuePhoneTime(epoch);}}
      else if(++failures>=5){failedUntil=millis()+30000;failures=0;server->disconnect(conn);}
     }
    }
    memset(&authJob,0,sizeof(authJob));authPending.store(false,std::memory_order_release);
   }

   if(welcome.exchange(false)&&online){
    resetActivity();JsonDocument reply;reply["authenticated"]=true;reply["version"]=3;reply["protocol"]=4;reply["speechFast"]=true;reply["lanBoost"]=true;reply["serial"]=pikaDeviceSerial();reply["ownerBound"]=true;String body;serializeJson(reply,body);
    pikaPhoneSend(7,0,(const uint8_t*)body.c_str(),body.length());foregroundNotice=true;
   }
   if(hello.exchange(false)&&subscribed&&secure&&!online){
    uint8_t random[32];esp_fill_random(random,sizeof(random));char hex[65];for(int i=0;i<32;i++)sprintf(hex+2*i,"%02x",random[i]);hex[64]=0;nonce=hex;
    JsonDocument doc;doc["nonce"]=nonce;doc["version"]=3;doc["ownerBound"]=pikaOwnerBound();doc["serial"]=pikaDeviceSerial();doc["forgetReceipt"]=pikaOwnerForgetReceipt();doc["device"]=NimBLEDevice::getAddress().toString();String body;serializeJson(doc,body);pikaPhoneSend(0,0,(const uint8_t*)body.c_str(),body.length());
   }
   if(online){
    VoiceAck voiceAck;if(voiceAcks&&xQueueReceive(voiceAcks,&voiceAck,0)==pdTRUE&&voiceAck.epoch==session.load()){
     uint8_t payload[8];for(int i=0;i<4;i++){payload[i]=voiceAck.token>>(8*i);payload[4+i]=voiceAck.received>>(8*i);}pikaPhoneSend(44,voiceAck.channel,payload,sizeof(payload));
    }
    if(foregroundNotice.exchange(false)){const char* body=foreground?"1":"0";if(!pikaPhoneSend(31,0,(const uint8_t*)body,1)&&online)foregroundNotice=true;}
    if(live.data&&live.epoch==session.load()&&!speechReceiving()&&!pikaHomeButtonsBusy()&&(!voiceAcks||uxQueueMessagesWaiting(voiceAcks)==0)){
     uint8_t frame[500];const size_t count=std::min<size_t>(488,live.size-liveAt);const uint32_t values[]={live.token,uint32_t(live.size),uint32_t(liveAt)};for(int i=0;i<3;i++)for(int b=0;b<4;b++)frame[i*4+b]=values[i]>>(b*8);memcpy(frame+12,live.data+liveAt,count);
     if(pikaPhoneSend(52,0,frame,count+12))liveAt+=count;else{heap_caps_free(live.data);live={};liveAt=0;}
     if(live.data&&liveAt==live.size){heap_caps_free(live.data);live={};liveAt=0;}
    }
    if(!foreground&&!pikaHomeButtonsBusy()){
     const uint32_t current=generation.load();const bool forced=forceNotice.exchange(false);
     if(forced||lastSentGeneration.load()!=current){String body=String(current);if(pikaPhoneSend(30,0,(const uint8_t*)body.c_str(),body.length())){lastSentGeneration=current;if(forced)forceSyncAt=millis();}}
    }
   }
  }
  vTaskDelay(pdMS_TO_TICKS(5));
 }
}

class ServerCallbacks:public NimBLEServerCallbacks{
 uint32_t onPassKeyDisplay()override{return pairCode;}
 void onConnect(NimBLEServer*,NimBLEConnInfo& info)override{if(conn!=0xffff&&conn!=info.getConnHandle()){server->disconnect(info.getConnHandle());return;}previouslyBonded=NimBLEDevice::isBonded(info.getIdAddress());peerIdentity=String(info.getIdAddress().toString().c_str());++session;conn=info.getConnHandle();connectedAt=millis();server->updateConnParams(conn,6,12,0,200);}
 void onDisconnect(NimBLEServer*,NimBLEConnInfo& info,int)override{if(info.getConnHandle()!=conn)return;pikaLanStop();++session;conn=0xffff;mtu=23;nonce="";authMessage="";online=false;secure=false;subscribed=false;bulkSubscribed=false;indicationSubscribed=false;uploadFast=false;welcome=false;hello=false;if(xSemaphoreTake(rpcMutex,pdMS_TO_TICKS(20))==pdTRUE){rpc="";rpcReady=false;rpcChannel=0;xSemaphoreGive(rpcMutex);}for(auto& c:channels){if(c.state>0)c.state=-2;else if(c.state==-3)c.state=0;}if(!shuttingDown)NimBLEDevice::startAdvertising();}
 void onMTUChange(uint16_t value,NimBLEConnInfo&)override{mtu=value;}
 void onAuthenticationComplete(NimBLEConnInfo& info)override{secure=info.isEncrypted();if(!secure)server->disconnect(info.getConnHandle());else{peerIdentity=String(info.getIdAddress().toString().c_str());if(subscribed)hello=true;}}
} serverCallbacks;
void receiveFrame(const uint8_t* frame,size_t n,bool bonded){if(n<6)return;const uint8_t* data=frame;
  const uint8_t op=data[0]&127,id=data[1];const size_t length=data[4]|(size_t(data[5])<<8);if(length!=n-6)return;data+=6;trafficAt=millis();pikaActivityLedTraffic(true);
  if(op==1){
   if(online||authPending.load(std::memory_order_acquire)||nonce.length()!=64||(failedUntil&&int32_t(millis()-failedUntil)<0))return;
   if(authMessage.length()+length>512){authMessage="";return;}authMessage.concat((const char*)data,length);if(!(frame[0]&128))return;
   strlcpy(authJob.body,authMessage.c_str(),sizeof(authJob.body));strlcpy(authJob.peer,peerIdentity.c_str(),sizeof(authJob.peer));strlcpy(authJob.challenge,nonce.c_str(),sizeof(authJob.challenge));authJob.epoch=session.load();authJob.bonded=bonded;authMessage="";
   authPending.store(true,std::memory_order_release);return;
  }
  if(!online)return;
  if(op==27&&id==bulkChannel.load()&&length==4){rpcAt=millis();const uint32_t offset=uint32_t(data[0])|(uint32_t(data[1])<<8)|(uint32_t(data[2])<<16)|(uint32_t(data[3])<<24);bulkConfirmed=offset;if(bulkAck)xSemaphoreGive(bulkAck);return;}
  if(id>=1&&id<=8){auto& c=channels[id-1];
   auto read32=[](const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);};
   if(op==48&&length==8&&c.state==1&&read32(data)==c.uploadToken.load()){c.uploadAccepted=read32(data+4);++c.uploadAckVersion;return;}
   if(op==46&&length==8&&c.state==1&&read32(data)==c.speechToken.load()){
    VoiceAck reply{id,c.speechToken.load(),c.speechReceived.load(),session.load()};if(voiceAcks)xQueueSend(voiceAcks,&reply,0);return;
   }
   if(op==3&&c.state==2)c.state=1;else if(op==6&&c.state==-3)c.state=0;else if((op==5||op==6)&&c.state>0)c.state=op==5?-1:-2;
   else if((op==4||op==41||op==45)&&c.ring&&c.state==1){
    size_t wireLength=length;
    if(op==45){if(length<=8||read32(data)!=c.speechToken.load()||read32(data+4)!=c.speechReceived.load())return;data+=8;wireLength-=8;}
    uint8_t pcm[1000];const bool compressed=op==41||op==45;
    if(compressed){if(wireLength>500)return;for(size_t i=0;i<wireLength;i++){unsigned u=(~data[i])&255;int t=(((u&15)<<3)+132)<<((u>>4)&7);int16_t sample=(u&128)?132-t:t-132;pcm[2*i]=sample;pcm[2*i+1]=sample>>8;}data=pcm;}
    const size_t incoming=compressed?wireLength*2:wireLength;size_t offset=0;uint32_t start=millis();
    while(offset<incoming&&c.state==1){const uint32_t tail=c.tail.load(),head=c.head.load();const uint32_t available=ringSize-(head-tail);if(!available){if(millis()-start>5000){c.state=-1;break;}vTaskDelay(1);continue;}const size_t count=std::min<size_t>(incoming-offset,std::min<size_t>(available,ringSize-head%ringSize));memcpy(c.ring+head%ringSize,data+offset,count);c.head.store(head+count);offset+=count;}
    if(op==45&&offset==incoming)c.speechReceived.fetch_add(wireLength);
   }
   return;
  }
  if(id>=128)rpcAt=millis();
  if(id>=128&&op==26&&xSemaphoreTake(rpcMutex,pdMS_TO_TICKS(20))==pdTRUE){if(!rpcReady){if(rpcChannel!=id){rpc="";rpc.reserve(65536);rpcChannel=id;}if(rpc.length()+length<=65536){rpc.concat((const char*)data,length);if(frame[0]&128)rpcReady=true;}else{rpc="";rpcChannel=0;}}xSemaphoreGive(rpcMutex);return;}
  if(id>=128&&op>=20&&op<=22&&xSemaphoreTake(rpcMutex,pdMS_TO_TICKS(20))==pdTRUE){
   if(op==20&&!rpcReady){rpc="";rpcChannel=id;}else if(op==21&&!rpcReady&&id==rpcChannel&&rpc.length()+length<=65536)rpc.concat((const char*)data,length);else if(op==22&&!rpcReady&&id==rpcChannel)rpcReady=true;
   xSemaphoreGive(rpcMutex);
  }
}
class Callbacks:public NimBLECharacteristicCallbacks{
 void onStatus(NimBLECharacteristic*,int code)override{if(code==0)return;ackCode=code;if(ack)xSemaphoreGive(ack);}
 void onSubscribe(NimBLECharacteristic*,NimBLEConnInfo& info,uint16_t value)override{subscribed=(value&3)!=0;indicationSubscribed=(value&2)!=0;bulkSubscribed=(value&1)!=0;secure=info.isEncrypted();if(subscribed&&secure)hello=true;}
 void onWrite(NimBLECharacteristic* ch,NimBLEConnInfo& info)override{
  if(info.getConnHandle()!=conn||!info.isEncrypted())return;const auto value=ch->getValue();const auto* data=(const uint8_t*)value.data();const size_t n=value.size();if(n<6)return;
  receiveFrame(data,n,info.isBonded());
 }
} callbacks;
}
bool pikaPhoneOnline(){return online.load();}
bool pikaPhoneWait(uint32_t timeout){const uint32_t started=millis();timeout=std::min<uint32_t>(timeout,15000);while(!online&&millis()-started<timeout){pikaPhoneService();delay(5);}return online;}
bool pikaPhoneBusy(){if(rpcReady)return true;for(auto& c:channels)if(c.state>0)return true;return false;}
void pikaPhoneBegin(){
 if(server)return;liveQueue=xQueueCreate(1,sizeof(LiveJob));voiceAcks=xQueueCreate(8,sizeof(VoiceAck));replyQueue=xQueueCreate(2,sizeof(ReplyJob));if(replyQueue)xTaskCreate(replyWorker,"phone-reply",6144,nullptr,1,nullptr);txMutex=xSemaphoreCreateMutex();ack=xSemaphoreCreateBinary();bulkAck=xSemaphoreCreateBinary();rpcMutex=xSemaphoreCreateMutex();slotMutex=xSemaphoreCreateMutex();
 NimBLEDevice::init((String("Pika_note ")+pikaDeviceSerial()).c_str());pikaOwnerBegin();NimBLEDevice::setMTU(517);pairCode=100000+(esp_random()%900000);NimBLEDevice::setSecurityPasskey(pairCode);NimBLEDevice::setSecurityAuth(true,true,true);NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
 server=NimBLEDevice::createServer();server->setCallbacks(&serverCallbacks,false); // Static callback: server must not delete it during sleep.
 auto* service=server->createService(serviceUuid);
 auto* rx=service->createCharacteristic(rxUuid,NIMBLE_PROPERTY::WRITE|NIMBLE_PROPERTY::WRITE_NR|NIMBLE_PROPERTY::WRITE_ENC,512);rx->setCallbacks(&callbacks);
 tx=service->createCharacteristic(txUuid,NIMBLE_PROPERTY::INDICATE|NIMBLE_PROPERTY::NOTIFY,512);tx->setCallbacks(&callbacks);server->start();xTaskCreate(controlWorker,"phone-control",8192,nullptr,1,nullptr);auto* adv=NimBLEDevice::getAdvertising();adv->addServiceUUID(serviceUuid);adv->setName((String("Pika_note ")+pikaDeviceSerial()).c_str());adv->enableScanResponse(true);adv->start();Serial.println("PIKA_PHONE_ADVERTISING");
}
bool pikaPhoneSend(uint8_t op,uint8_t channel,const uint8_t* data,size_t size){
 if(!server||!subscribed||!secure||(!online&&op!=0))return false;
 if(op>=2&&op!=7&&op!=30&&op!=31&&pikaLanOnline()){trafficAt=millis();pikaActivityLedTraffic(true);bool ok=pikaLanSend(op,channel,data,size);if(ok&&op>=23&&op<=25){rpcAt=millis();pikaPhoneSyncProgress();}return ok;}
 const bool interruptible=(op==2||op==4||op==5)&&pikaBackgroundNetworkMode()&&!foreground;
 if(interruptible&&preemptBackground())return false;
 const uint32_t waiting=millis();while(xSemaphoreTake(txMutex,pdMS_TO_TICKS(5))!=pdTRUE){if((interruptible&&preemptBackground())||millis()-waiting>=10000)return false;}
 if(!finishIndication(interruptible)){xSemaphoreGive(txMutex);return false;}
 bool ok=true;size_t offset=0;trafficAt=millis();pikaActivityLedTraffic(true);
 do{
  if((!online&&op!=0)||(interruptible&&preemptBackground())){ok=false;break;}
  const size_t capacity=std::min<size_t>(500,std::max<int>(1,int(mtu.load())-9));const size_t n=std::min(capacity,size-offset);
  uint8_t frame[506];frame[0]=op|((offset+n==size)?128:0);frame[1]=channel;const uint16_t seq=sequence++;frame[2]=seq;frame[3]=seq>>8;frame[4]=n;frame[5]=n>>8;if(n)memcpy(frame+6,data+offset,n);xSemaphoreTake(ack,0);ackCode=-1;
  if(indicationSubscribed){tx->setValue(frame,n+6);if(!tx->indicate(conn)){ok=false;break;}indicationPending=true;if(!finishIndication(interruptible)){ok=false;break;}}
  else{const uint32_t started=millis();while(!tx->notify(frame,n+6,conn)){if((!online&&op!=0)||(interruptible&&preemptBackground())||millis()-started>1000){ok=false;break;}vTaskDelay(pdMS_TO_TICKS(2));}if(!ok)break;}
  offset+=n;if(op>=23&&op<=25){rpcAt=millis();pikaPhoneSyncProgress();}
 }while(offset<size);
 xSemaphoreGive(txMutex);return ok;
}
void pikaPhoneService(){
 if(forceSyncAt){if(!online||int32_t(rpcAt.load()-forceSyncAt)>0)forceSyncAt=0;else if(millis()-forceSyncAt>=3000){forceSyncAt=0;if(server&&conn!=0xffff)server->disconnect(conn);}}
 generation=pikaPhoneSyncGeneration();if(server&&conn!=0xffff&&!online&&millis()-connectedAt>30000){server->disconnect(conn);return;}
 pikaActivityLedTraffic(trafficAt&&millis()-trafficAt<180);
 if(displayedOnline!=online.load()){displayedOnline=online;homeChangedAt=millis();homeDirty=true;lastSentGeneration=0;}
}
void pikaPhoneSleep(){pikaLanStop();online=false;shuttingDown=true;foreground=true;if(server&&xSemaphoreTake(txMutex,pdMS_TO_TICKS(10000))==pdTRUE){NimBLEDevice::deinit(true);server=nullptr;tx=nullptr;xSemaphoreGive(txMutex);}}
bool pikaPhoneTakeRpc(String& request,uint8_t& channel){if(!rpcMutex||xSemaphoreTake(rpcMutex,0)!=pdTRUE)return false;const bool ready=rpcReady;if(ready){request=std::move(rpc);channel=rpcChannel;rpcReady=false;rpcChannel=0;}xSemaphoreGive(rpcMutex);return ready;}
void pikaPhoneReply(uint8_t channel,int code,const String& body,bool bulk){ReplyJob job{channel,code,session.load(),body.length(),nullptr,bulk};job.data=(uint8_t*)heap_caps_malloc(std::max<size_t>(1,job.size),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!job.data)return;if(job.size)memcpy(job.data,body.c_str(),job.size);if(!replyQueue||xQueueSend(replyQueue,&job,0)!=pdTRUE)heap_caps_free(job.data);}
int PhoneSecureClient::connect(const char* host,uint16_t port,int32_t timeout){stop();if(preemptBackground())return 0;if(!online||port!=443)return 0;
 if(xSemaphoreTake(slotMutex,pdMS_TO_TICKS(1000))!=pdTRUE)return 0;for(int i=0;i<8;i++)if(channels[i].state==0){channel=i;auto& c=channels[i];if(!c.ring)c.ring=(uint8_t*)heap_caps_malloc(ringSize,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!c.ring){channel=-1;break;}c.head=0;c.tail=0;c.speechToken=0;c.uploadSent=0;c.uploadAccepted=0;c.uploadAckVersion=0;c.uploadToken=esp_random();c.state=2;break;}xSemaphoreGive(slotMutex);if(channel<0)return 0;
 JsonDocument doc;doc["host"]=host;doc["port"]=port;if(!pikaLanOnline()&&uploadFast&&uploadAllowed&&bulkSubscribed&&mtu.load()>=185)doc["uploadToken"]=channels[channel].uploadToken.load();String body;serializeJson(doc,body);if(!pikaPhoneSend(2,channel+1,(const uint8_t*)body.c_str(),body.length())){stop();return 0;}
 const uint32_t started=millis();while(online&&channels[channel].state==2&&millis()-started<uint32_t(std::max<int32_t>(timeout,15000))){if(preemptBackground()){stop();return 0;}delay(2);}if(channels[channel].state!=1){stop();return 0;}return 1;
}
namespace {
uint32_t uploadCrc(const uint8_t* bytes,size_t n){uint32_t crc=~0u;for(size_t i=0;i<n;i++){crc^=bytes[i];for(int bit=0;bit<8;bit++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));}return ~crc;}
bool sendUploadSeal(uint8_t id,const uint8_t* seal,size_t size){
 if(xSemaphoreTake(txMutex,pdMS_TO_TICKS(10000))!=pdTRUE)return false;
 bool ok=finishIndication(false);if(ok){uint8_t frame[22];frame[0]=49|128;frame[1]=id;const uint16_t seq=sequence++;frame[2]=seq;frame[3]=seq>>8;frame[4]=size;frame[5]=0;memcpy(frame+6,seal,size);const uint32_t at=millis(),epoch=session.load();while(!tx->notify(frame,size+6,conn)){if(!online||session!=epoch||millis()-at>=1000){ok=false;break;}delay(2);}}
 xSemaphoreGive(txMutex);return ok;
}
bool sendUploadWindow(int index,const uint8_t* bytes,size_t n){
 auto& c=channels[index];const uint32_t epoch=session.load(),start=c.uploadSent,end=start+n,token=c.uploadToken.load(),crc=uploadCrc(bytes,n);const uint8_t id=index+1;
 auto put32=[](uint8_t* p,uint32_t v){for(int i=0;i<4;i++)p[i]=v>>(8*i);};
 for(int retry=0;retry<4;retry++){
  if(!online||c.state!=1||session!=epoch||preemptBackground())return false;
  if(xSemaphoreTake(txMutex,pdMS_TO_TICKS(10000))!=pdTRUE)return false;
  bool ok=finishIndication(false);size_t at=0;
  while(ok&&at<n){
   if(!online||c.state!=1||session!=epoch){ok=false;break;}
   const size_t count=std::min<size_t>(n-at,std::min<size_t>(492,std::max<int>(1,int(mtu.load())-17)));
   uint8_t frame[506];frame[0]=47|128;frame[1]=id;const uint16_t seq=sequence++;frame[2]=seq;frame[3]=seq>>8;frame[4]=count+8;frame[5]=(count+8)>>8;put32(frame+6,token);put32(frame+10,start+at);memcpy(frame+14,bytes+at,count);
   const uint32_t began=millis();while(!tx->notify(frame,count+14,conn)){if(!online||session!=epoch||millis()-began>1000){ok=false;break;}vTaskDelay(pdMS_TO_TICKS(2));}
   if(ok){at+=count;trafficAt=millis();pikaActivityLedTraffic(true);}
  }
  xSemaphoreGive(txMutex);if(!ok)return false;
  const uint32_t version=c.uploadAckVersion.load();uint8_t seal[16];put32(seal,token);put32(seal+4,start);put32(seal+8,end);put32(seal+12,crc);
  if(!sendUploadSeal(id,seal,sizeof(seal))){Serial.println("[UploadFail] seal notification");return false;}
  const uint32_t began=millis();while(online&&c.state==1&&session==epoch&&c.uploadAckVersion==version&&millis()-began<5000){if(preemptBackground())return false;delay(1);}
  if(!online||c.state!=1||session!=epoch){Serial.printf("[UploadFail] state=%d online=%d start=%lu end=%lu\n",c.state.load(),online.load(),(unsigned long)start,(unsigned long)end);return false;}
  if(c.uploadAccepted==end){c.uploadSent=end;return true;}
  if(c.uploadAccepted!=start){Serial.printf("[UploadFail] offset start=%lu end=%lu ack=%lu version=%lu state=%d\n",(unsigned long)start,(unsigned long)end,(unsigned long)c.uploadAccepted.load(),(unsigned long)c.uploadAckVersion.load(),c.state.load());return false;}
 }
 return false;
}
}
size_t PhoneSecureClient::write(const uint8_t* bytes,size_t n){
 if(channel<0||!online||channels[channel].state!=1)return 0;
 if(pikaLanOnline()||!uploadFast||!uploadAllowed||!bulkSubscribed||mtu.load()<185)return pikaPhoneSend(4,channel+1,bytes,n)?n:0;
 const uint32_t began=millis();size_t at=0;while(at<n){const size_t count=std::min<size_t>(4096,n-at);if(!sendUploadWindow(channel,bytes+at,count)){channels[channel].state=-2;break;}at+=count;}
 if(n>=4096)Serial.printf("[PhoneUpload] bytes=%u ms=%lu window=4096\n",unsigned(at),(unsigned long)(millis()-began));return at;
}
int PhoneSecureClient::available(){if(preemptBackground()){stop();return 0;}if(channel<0)return 0;auto& c=channels[channel];return c.head.load()-c.tail.load();}
int PhoneSecureClient::peek(){if(!available())return -1;auto& c=channels[channel];return c.ring[c.tail.load()%ringSize];}
int PhoneSecureClient::read(){uint8_t b;return read(&b,1)==1?b:-1;}
int PhoneSecureClient::read(uint8_t* out,size_t n){if(channel<0)return 0;auto& c=channels[channel];const uint32_t tail=c.tail.load();n=std::min<size_t>(n,c.head.load()-tail);const size_t first=std::min<size_t>(n,ringSize-tail%ringSize);if(first)memcpy(out,c.ring+tail%ringSize,first);if(n>first)memcpy(out+first,c.ring,n-first);c.tail.store(tail+n);return n;}
size_t PhoneSecureClient::readBytes(char* out,size_t n){size_t readCount=0;uint32_t last=millis();while(readCount<n&&millis()-last<timeoutMs){const int got=read((uint8_t*)out+readCount,n-readCount);if(got>0){readCount+=got;last=millis();}else if(!connected())break;else delay(1);}return readCount;}
uint8_t PhoneSecureClient::connected(){if(preemptBackground()){stop();return 0;}return channel>=0&&(available()>0||(online&&channels[channel].state==1));}
void PhoneSecureClient::stop(){if(channel<0)return;const int old=channel;channel=-1;if(online&&channels[old].state>0){channels[old].state=-3;if(!pikaPhoneSend(5,old+1,nullptr,0))channels[old].state=0;}else channels[old].state=0;}

void pikaPhoneRequestHomeRefresh(){homeChangedAt=millis();homeDirty=true;}
void pikaPhoneChanged(){pikaPhoneSyncInvalidate();pikaPhoneRequestHomeRefresh();}
bool pikaPhoneHomeDirty(){if(millis()-homeChangedAt<350)return false;return homeDirty.exchange(false);}
uint32_t pikaPhoneGeneration(){return generation;}

uint32_t pikaPhonePairCode(){return server&&!pikaOwnerBound()?pairCode:0;}

int PhoneSecureClient::connectSpeech(const String& payload){stop();if(!online)return 0;if(xSemaphoreTake(slotMutex,pdMS_TO_TICKS(1000))!=pdTRUE)return 0;for(int i=0;i<8;i++)if(channels[i].state==0){channel=i;auto& c=channels[i];if(!c.ring)c.ring=(uint8_t*)heap_caps_malloc(ringSize,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!c.ring){channel=-1;break;}c.head=0;c.tail=0;c.speechReceived=0;c.speechToken=esp_random();c.state=2;break;}xSemaphoreGive(slotMutex);if(channel<0)return 0;JsonDocument request;if(deserializeJson(request,payload)){stop();return 0;}request["stream"]=channels[channel].speechToken.load();String message;serializeJson(request,message);if(!pikaPhoneSend(40,channel+1,(const uint8_t*)message.c_str(),message.length())){stop();return 0;}uint32_t at=millis();while(online&&channels[channel].state==2&&millis()-at<30000)delay(2);if(channels[channel].state!=1){stop();return 0;}return 1;}

bool PhoneSecureClient::failed()const{return channel<0||!online||channels[channel].state==-2;}

String pikaPhoneDiagnostics(){return String("PIKA_PHONE online=")+online.load()+" secure="+secure.load()+" peer="+String(conn)+" mtu="+String(mtu.load())+" lan="+String(pikaLanOnline());}

void pikaPhoneForeground(bool active){if(foreground.exchange(active)!=active)foregroundNotice=true;}
bool pikaPhoneForegroundActive(){return foreground;}
bool pikaPhoneBackgroundInterrupted(){return interrupted;}
void pikaPhoneBackgroundReset(){interrupted=false;}
bool pikaPhoneTransferRecent(){return rpcAt&&millis()-rpcAt<1200;}

void pikaPhoneRequestSync(){
 if(!server)pikaPhoneBegin();
 if(online){forceNotice=true;}
 else if(server&&conn!=0xffff)server->disconnect(conn);
 else if(server)NimBLEDevice::startAdvertising();
}

void pikaPhoneUseWindowUpload(bool enabled){uploadAllowed=enabled;}

bool pikaPhoneQueueLiveSnapshot(const String& json){
 if(!online||!liveQueue||!json.length()||json.length()>262144)return false;LiveJob job{nullptr,json.length(),session.load(),esp_random()};job.data=(uint8_t*)heap_caps_malloc(job.size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!job.data)return false;memcpy(job.data,json.c_str(),job.size);LiveJob previous;if(xQueueReceive(liveQueue,&previous,0)==pdTRUE)heap_caps_free(previous.data);if(xQueueSend(liveQueue,&job,0)!=pdTRUE){heap_caps_free(job.data);return false;}return true;
}

void pikaPhoneReceiveLan(const uint8_t* frame,size_t size){if(!online||size<6||(frame[0]&127)==1)return;receiveFrame(frame,size,true);}
void pikaPhoneLanLost(){for(auto& c:channels)if(c.state>0)c.state=-2;if(xSemaphoreTake(rpcMutex,pdMS_TO_TICKS(20))==pdTRUE){if(!rpcReady){rpc="";rpcChannel=0;}xSemaphoreGive(rpcMutex);}}
void pikaPhoneLanTraffic(){trafficAt=millis();pikaActivityLedTraffic(true);}
