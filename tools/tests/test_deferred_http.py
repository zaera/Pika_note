from pathlib import Path
import ast,runpy,tempfile,subprocess,sys
root=Path(__file__).resolve().parents[2]
base=runpy.run_path(str(root/'tools/tests/test_manager_store.py'))
tree=ast.parse((root/'tools/tests/test_ask_worker.py').read_text());stubs=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='stubs' for t in n.targets))
stubs=stubs.replace('inline int xSemaphoreTake(SemaphoreHandle_t s,int){','inline int xSemaphoreTake(SemaphoreHandle_t s,int wait){').replace('std::unique_lock<std::mutex> lock(s->mutex);s->cv.wait(lock,[&]{return s->ready;});return 1;','std::unique_lock<std::mutex> lock(s->mutex);if(!wait&&!s->ready)return 0;s->cv.wait(lock,[&]{return s->ready;});s->ready=false;return 1;')
stubs+=r"""
#include <chrono>
class Stream{public:virtual ~Stream(){}virtual size_t write(uint8_t)=0;virtual size_t write(const uint8_t*,size_t)=0;virtual int available()=0;virtual int read()=0;virtual int peek()=0;virtual void flush()=0;};
inline int lights=0;inline void pikaActivityLedConnection(bool on){lights+=on?1:-1;}
inline const uint8_t pika_ca_bundle_start[]={0},weather_ca_bundle_start[]={0};
inline int responseCode=200,responseSize=2;
struct WiFiClientSecure{void setCACert(const char*){}void setHandshakeTimeout(int){}};
struct HTTPClient{
 bool begin(WiFiClientSecure&,const String&){return true;}void setConnectTimeout(int){}void setTimeout(int){}void setReuse(bool){}void addHeader(const char*,const String&){}
 int sendRequest(const char*,uint8_t*,size_t){std::this_thread::sleep_for(std::chrono::milliseconds(80));return responseCode;}
 int getSize(){return responseSize;}int writeToStream(Stream* out){return int(out->write(reinterpret_cast<const uint8_t*>("{}"),2));}void end(){}
};
"""
source=(root/'pika_note/src/app/deferred_http.cpp').read_text();source=source.replace('extern const uint8_t pika_ca_bundle_start[] asm("_binary_certs_pika_ca_bundle_pem_start");','').replace('extern const uint8_t weather_ca_bundle_start[] asm("_binary_certs_weather_ca_bundle_pem_start");','')
main=r"""
#include "http_replay.h"
int main(){
 failTask=true;assert(!pikaDeferredHttpStart(1,"url","GET","",""));assert(!pikaDeferredHttpBusy());failTask=false;
 auto start=std::chrono::steady_clock::now();assert(pikaDeferredHttpStart(1,"url","GET","",""));
 assert(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(50));
 PikaDeferredResponse result;assert(!pikaDeferredHttpPoll(1,result));assert(!pikaDeferredHttpPoll(2,result));assert(!pikaDeferredHttpStart(2,"url","GET","",""));
 while(!pikaDeferredHttpPoll(1,result))std::this_thread::sleep_for(std::chrono::milliseconds(2));assert(result.code==200&&result.body=="{}"&&lights==0);
 assert(pikaDeferredHttpStart(2,"url","GET","","",true));pikaDeferredHttpCancel(2);
 while(pikaDeferredHttpBusy())std::this_thread::sleep_for(std::chrono::milliseconds(2));assert(lights==0);
 responseSize=20000;assert(pikaDeferredHttpStart(1,"url","GET","",""));while(!pikaDeferredHttpPoll(1,result))std::this_thread::sleep_for(std::chrono::milliseconds(2));assert(result.code==-3&&result.body.length()==0);
 PikaHttpReplay replay;String body;int code;
 replay.begin();assert(!replay.read("patch",body,code));replay.append("patch","missing",404);
 replay.begin();assert(replay.read("patch",body,code)&&code==404);assert(!replay.read("post",body,code));replay.append("post","created",201);
 replay.begin();assert(replay.read("patch",body,code)&&code==404);assert(replay.read("post",body,code)&&code==201);
 replay.begin();assert(!replay.read("changed payload",body,code));assert(replay.empty());
 puts("Deferred HTTP: nonblocking dispatch/poll, owner isolation, cancellation, size limits, allocation failure and PATCH/POST replay: PASS");fflush(stdout);std::_Exit(0);
}
"""
with tempfile.TemporaryDirectory() as directory:
 d=Path(directory);(d/'Arduino.h').write_text(base['arduino']+stubs);(d/'freertos').mkdir()
 for name in ['FreeRTOS.h','queue.h','semphr.h']:(d/'freertos'/name).write_text('#include "Arduino.h"')
 for name in ['WiFiClientSecure.h','HTTPClient.h','activity_led.h']:(d/name).write_text('#include "Arduino.h"')
 (d/'test.cpp').write_text(source+main,encoding='utf-8');exe=d/'transport.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(d/'test.cpp'),'-std=c++17','-I',str(d),'-I',str(root/'pika_note/src/app'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=15)
