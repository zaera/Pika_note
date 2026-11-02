#include "phone_link.h"
#include "deferred_http.h"
#include "activity_led.h"

#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
extern const uint8_t pika_ca_bundle_start[] asm("_binary_certs_pika_ca_bundle_pem_start");
extern const uint8_t weather_ca_bundle_start[] asm("_binary_certs_weather_ca_bundle_pem_start");
namespace {
struct Job {int owner=0;String url,method,body,authorization;bool weather=false;PikaDeferredResponse result;};
Job job;QueueHandle_t jobs=nullptr;SemaphoreHandle_t done=nullptr;bool active=false,discard=false;
class BoundedBody:public Stream {
 public:String text;bool overflow=false;
 size_t write(uint8_t b)override{return write(&b,1);}
 size_t write(const uint8_t* bytes,size_t n)override{if(text.length()+n>16384){overflow=true;return 0;}return text.concat(reinterpret_cast<const char*>(bytes),n)?n:0;}
 int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}void flush()override{}
};
void transport(void*) {
 for(;;){Job* current=nullptr;if(xQueueReceive(jobs,&current,portMAX_DELAY)!=pdTRUE)continue;
  // Transport owns no SD files, settings, display or record state.
  PhoneSecureClient tls;tls.setCACert(reinterpret_cast<const char*>(current->weather?weather_ca_bundle_start:pika_ca_bundle_start));tls.setHandshakeTimeout(4);
  HTTPClient http;current->result={};
  if(http.begin(tls,current->url)){
   http.setConnectTimeout(4000);http.setTimeout(5000);http.setReuse(false);
   http.addHeader("Accept-Encoding","identity");
   http.addHeader("Content-Type",current->authorization.length()?"application/json":"application/x-www-form-urlencoded");
   if(current->authorization.length())http.addHeader("Authorization","Bearer "+current->authorization);
   current->result.code=http.sendRequest(current->method.c_str(),reinterpret_cast<uint8_t*>(const_cast<char*>(current->body.c_str())),current->body.length());
   if(current->result.code>0&&current->result.code!=204){
    if(http.getSize()>16384)current->result.code=-3;
    else {BoundedBody body;const int got=http.writeToStream(&body);if(got<0||body.overflow)current->result.code=-2;else current->result.body=std::move(body.text);}
   }
   http.end();
  }
  xSemaphoreGive(done);
 }
}
}
void pikaDeferredHttpReap(){
 if(active&&discard&&xSemaphoreTake(done,0)==pdTRUE){job={};active=false;discard=false;pikaActivityLedConnection(false);}
}
void pikaDeferredHttpCancel(int owner){if(active&&job.owner==owner)discard=true;pikaDeferredHttpReap();}
bool pikaDeferredHttpBusy(){pikaDeferredHttpReap();return active;}
bool pikaDeferredHttpStart(int owner,const String& url,const String& method,const String& body,const String& authorization,bool weatherCa){
 pikaDeferredHttpReap();if(active)return false;
 if(!jobs){jobs=xQueueCreate(1,sizeof(Job*));done=xSemaphoreCreateBinary();
  if(!jobs||!done){if(jobs)vQueueDelete(jobs);if(done)vSemaphoreDelete(done);jobs=nullptr;done=nullptr;return false;}
  if(xTaskCreate(transport,"portal-http",32*1024,nullptr,1,nullptr)!=pdPASS){vQueueDelete(jobs);vSemaphoreDelete(done);jobs=nullptr;done=nullptr;return false;}
 }
 job={};job.owner=owner;job.url=url;job.method=method;job.body=body;job.authorization=authorization;job.weather=weatherCa;
 Job* pointer=&job;if(xQueueSend(jobs,&pointer,0)!=pdTRUE)return false;
 discard=false;active=true;pikaActivityLedConnection(true);return true;
}
bool pikaDeferredHttpPoll(int owner,PikaDeferredResponse& result){
 if(!active||job.owner!=owner||xSemaphoreTake(done,0)!=pdTRUE)return false;
 result=std::move(job.result);job={};active=false;pikaActivityLedConnection(false);return true;
}
