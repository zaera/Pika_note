#include "phone_lan.h"
#include "phone_link.h"
#include "activity_led.h"
#include <WiFi.h>
#include <NetworkClient.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <vector>
#include <mutex>

namespace {
struct Offer{String ssid,password;IPAddress host;uint16_t port=0;uint8_t key[32]{};};
Offer offer;std::mutex settingsMutex,wireMutex,cryptoMutex;std::atomic<bool> configured{false},ready{false},running{false},stop{false};
NetworkClient socket;mbedtls_gcm_context encryptCtx,decryptCtx;uint8_t sessionKey[32];uint64_t sent=0,received=0;
uint8_t *txFrame=nullptr,*txWire=nullptr,*rxWire=nullptr;
bool exact(uint8_t* bytes,size_t count,uint32_t timeout){size_t at=0;uint32_t began=millis();while(at<count&&!stop&&pikaPhoneOnline()){int n=socket.available();if(n>0){int got=socket.read(bytes+at,std::min<size_t>(count-at,n));if(got<=0)return false;at+=got;began=millis();}else{if(!socket.connected()||millis()-began>=timeout)return false;delay(1);}}return at==count;}
void nonce(uint8_t* out,uint32_t direction,uint64_t counter){for(int i=0;i<4;i++)out[i]=direction>>(24-8*i);for(int i=0;i<8;i++)out[4+i]=counter>>(56-8*i);}
// Feed hardware AES from small aligned internal buffers, rather than asking
// its DMA layer to allocate large bounce buffers for PSRAM records mid-transfer.
int boundedCrypt(mbedtls_gcm_context* ctx,int mode,const uint8_t* input,size_t size,const uint8_t* iv,uint8_t* output,uint8_t* tag){
 int result=mbedtls_gcm_starts(ctx,mode,iv,12);if(result)return result;
 result=mbedtls_gcm_update_ad(ctx,nullptr,0);if(result)return result;
 alignas(16) uint8_t plain[256],cipher[272];size_t at=0;
 while(at<size){size_t n=std::min<size_t>(sizeof(plain),size-at),written=0;memcpy(plain,input+at,n);result=mbedtls_gcm_update(ctx,plain,n,cipher,sizeof(cipher),&written);if(result||written!=n)return result?result:-1;memcpy(output+at,cipher,written);at+=n;}
 size_t finalSize=0;result=mbedtls_gcm_finish(ctx,cipher,sizeof(cipher),&finalSize,tag,16);
 memset(plain,0,sizeof(plain));memset(cipher,0,sizeof(cipher));return result?result:(finalSize?-1:0);
}
bool writeRecord(const uint8_t* data,size_t size){if(size>4006||!txWire)return false;uint32_t began=millis();uint8_t iv[12],tag[16];nonce(iv,0x50494b41,sent++);uint8_t* bytes=txWire;uint32_t n=size+16;for(int i=0;i<4;i++)bytes[i]=n>>(24-8*i);std::unique_lock<std::mutex> crypto(cryptoMutex);int result=boundedCrypt(&encryptCtx,MBEDTLS_GCM_ENCRYPT,data,size,iv,bytes+4,tag);crypto.unlock();if(result){Serial.printf("PIKA_LAN_ENCRYPT_FAIL code=%d heap=%u\n",result,ESP.getFreeHeap());return false;}memcpy(bytes+4+size,tag,16);uint32_t encrypted=millis();size_t written=socket.write(bytes,size+20);if(millis()-began>50)Serial.printf("LAN_SLOW crypt=%lu write=%lu size=%u\n",(unsigned long)(encrypted-began),(unsigned long)(millis()-encrypted),unsigned(size));if(written!=size+20)Serial.printf("PIKA_LAN_WRITE_FAIL got=%u expected=%u wifi=%d heap=%u\n",unsigned(written),unsigned(size+20),int(WiFi.status()),ESP.getFreeHeap());return written==size+20;}
bool readRecord(std::vector<uint8_t>& frame,uint32_t timeout){uint8_t header[4];if(!exact(header,4,timeout))return false;uint32_t size=0;for(int i=0;i<4;i++)size=(size<<8)|header[i];if(size<16||size>4022)return false;uint8_t* bytes=rxWire;if(!exact(bytes,size,8000))return false;frame.resize(size-16);uint8_t iv[12];nonce(iv,0x50484f4e,received++);std::unique_lock<std::mutex> crypto(cryptoMutex);uint8_t tag[16]{};int result=boundedCrypt(&decryptCtx,MBEDTLS_GCM_DECRYPT,bytes,frame.size(),iv,frame.data(),tag);unsigned mismatch=0;for(unsigned i=0;i<16;++i)mismatch|=tag[i]^bytes[frame.size()+i];if(!result&&mismatch)result=MBEDTLS_ERR_GCM_AUTH_FAILED;if(result)memset(frame.data(),0,frame.size());crypto.unlock();if(result)Serial.printf("PIKA_LAN_DECRYPT_FAIL code=%d heap=%u\n",result,ESP.getFreeHeap());return result==0;}
void worker(void*){
 txFrame=(uint8_t*)heap_caps_malloc(4006,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);txWire=(uint8_t*)heap_caps_malloc(4026,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);rxWire=(uint8_t*)heap_caps_malloc(4022,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!txFrame||!txWire||!rxWire){heap_caps_free(txFrame);heap_caps_free(txWire);heap_caps_free(rxWire);configured=false;running=false;vTaskDelete(nullptr);return;}
 mbedtls_gcm_init(&encryptCtx);mbedtls_gcm_init(&decryptCtx);Offer current;{std::lock_guard<std::mutex> lock(settingsMutex);current=offer;}
 WiFi.mode(WIFI_STA);WiFi.setSleep(false);WiFi.setAutoReconnect(false);WiFi.begin(current.ssid.c_str(),current.password.c_str());uint32_t started=millis();
 while(!stop&&pikaPhoneOnline()&&WiFi.status()!=WL_CONNECTED&&millis()-started<8000){pikaPhoneLanTraffic();delay(10);}
 Serial.printf("PIKA_LAN_JOIN wifi=%d ms=%lu\n",int(WiFi.status()),(unsigned long)(millis()-started));int phase=0;bool authenticated=false;
 if(!stop&&pikaPhoneOnline()&&WiFi.status()==WL_CONNECTED&&WiFi.SSID()==current.ssid&&socket.connect(current.host,current.port,2000)){
  phase=1;socket.setNoDelay(true);socket.setTimeout(5);uint8_t challenge[32];std::vector<uint8_t> reply;
  if(exact(challenge,sizeof(challenge),3000)&&!mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),current.key,32,challenge,32,sessionKey)){
   if(mbedtls_gcm_setkey(&encryptCtx,MBEDTLS_CIPHER_ID_AES,sessionKey,256)||mbedtls_gcm_setkey(&decryptCtx,MBEDTLS_CIPHER_ID_AES,sessionKey,256)){stop=true;}phase=2;sent=received=0;const char hello[]="PIKA-LAN-1";
   if(!stop&&writeRecord((const uint8_t*)hello,sizeof(hello)-1)&&readRecord(reply,10000)&&reply.size()==2&&!memcmp(reply.data(),"OK",2)){
    phase=3;
    // BLE remains the control channel. Never move an in-flight stream to TCP.
    uint32_t wait=millis();while((pikaPhoneBusy()||pikaPhoneForegroundActive())&&!stop&&millis()-wait<3000)delay(5);
    if(!pikaPhoneBusy()&&!pikaPhoneForegroundActive()&&!stop&&writeRecord((const uint8_t*)"READY",5)){ready=true;authenticated=true;Serial.printf("PIKA_LAN_READY join_ms=%lu rssi=%d\n",(unsigned long)(millis()-started),int(WiFi.RSSI()));}
   }
  }
 }
 memset(current.key,0,sizeof(current.key));current.password="";
 std::vector<uint8_t> incoming;incoming.reserve(4006);while(ready&&!stop&&pikaPhoneOnline()&&socket.connected()){
  if(!socket.available()){delay(2);continue;}if(!readRecord(incoming,8000))break;pikaPhoneReceiveLan(incoming.data(),incoming.size());
 }
 {std::lock_guard<std::mutex> lock(wireMutex);ready=false;socket.stop();memset(sessionKey,0,sizeof(sessionKey));mbedtls_gcm_free(&encryptCtx);mbedtls_gcm_free(&decryptCtx);heap_caps_free(txFrame);heap_caps_free(txWire);heap_caps_free(rxWire);txFrame=txWire=rxWire=nullptr;}
 if(authenticated&&pikaPhoneOnline())pikaPhoneLanLost();
 configured=false;WiFi.disconnect(true);running=false;Serial.printf("PIKA_LAN_CLOSED phase=%d\n",phase);vTaskDelete(nullptr);
}
}
bool pikaLanConfigure(JsonVariantConst input){
 if(running)return false;Offer next;next.ssid=String(input["ssid"]|"");next.password=String(input["password"]|"");String key=input["key"]|"";next.port=input["port"]|0;String host=input["host"]|"";
 if(String(input["phoneSsid"]|"")!=next.ssid)return false;
 if(!next.ssid.length()||next.ssid.length()>32||next.password.length()>63||key.length()!=64||next.port<1024||!next.host.fromString(host)||next.host[0]==0||next.host[0]>=224||next.host[0]==127)return false;
 for(int i=0;i<32;i++){auto hex=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};int a=hex(key[2*i]),b=hex(key[2*i+1]);if(a<0||b<0)return false;next.key[i]=(a<<4)|b;}
 {std::lock_guard<std::mutex> lock(settingsMutex);offer=next;}stop=false;configured=true;running=true;
 if(xTaskCreate(worker,"phone-lan",8192,nullptr,1,nullptr)!=pdPASS){running=false;configured=false;return false;}return true;
}
bool pikaLanOnline(){return ready;}
bool pikaLanConfigured(){return configured;}
bool pikaLanSend(uint8_t op,uint8_t channel,const uint8_t* data,size_t size){
 std::lock_guard<std::mutex> lock(wireMutex);if(!ready)return false;size_t at=0;
 do{size_t n=std::min<size_t>(4000,size-at);uint8_t* frame=txFrame;frame[0]=op|((at+n==size)?128:0);frame[1]=channel;frame[2]=frame[3]=0;frame[4]=n;frame[5]=n>>8;if(n)memcpy(frame+6,data+at,n);if(!writeRecord(frame,n+6)){ready=false;stop=true;return false;}at+=n;}while(at<size);return true;
}
void pikaLanStop(){stop=true;ready=false;}
bool pikaLanStopAndWait(uint32_t timeoutMs){pikaLanStop();const uint32_t at=millis();while(running&&millis()-at<timeoutMs)delay(2);return !running;}
