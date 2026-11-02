from pathlib import Path
import subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'pika_note/src/app/phone_link.cpp').read_text('utf-8')
a=s.index('uint32_t uploadCrc(');b=s.index('\n}\nsize_t PhoneSecureClient::write',a);code=s[a:b]
stub=r'''#include <atomic>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cassert>
struct C{std::atomic<int> state{1};std::atomic<uint32_t> uploadToken{123},uploadAccepted{0},uploadAckVersion{0};uint32_t uploadSent=0;}channels[1];
std::atomic<bool> online{true};std::atomic<uint32_t> session{1};std::atomic<uint16_t> mtu{517};int conn=1,txMutex=0;uint16_t sequence=0;uint32_t trafficAt=0,now=0;bool drop=false,corrupt=false,loseAck=false,pressure=false;int sends=0;std::vector<uint8_t> buffer,sink;
uint32_t uploadCrc(const uint8_t*,size_t);
uint32_t read32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
struct Tx{bool notify(const uint8_t* f,size_t,int){if(pressure){pressure=false;return false;}++sends;auto& c=channels[0];const unsigned op=f[0]&127;uint32_t offset=read32(f+10);if(op==47){if(drop){drop=false;return true;}size_t n=(f[4]|(unsigned(f[5])<<8))-8;if(offset==sink.size()+buffer.size()){buffer.insert(buffer.end(),f+14,f+14+n);if(corrupt){buffer.back()^=1;corrupt=false;}}}else{assert(op==49);uint32_t end=read32(f+14),crc=read32(f+18);if(offset==sink.size()&&end-offset==buffer.size()&&uploadCrc(buffer.data(),buffer.size())==crc){sink.insert(sink.end(),buffer.begin(),buffer.end());buffer.clear();if(loseAck){loseAck=false;return true;}}else if(offset==sink.size())buffer.clear();c.uploadAccepted=sink.size();++c.uploadAckVersion;}return true;}}txValue,*tx=&txValue;
struct Log{template<class... A>void printf(const char*,A...){}void println(const char*){}}Serial;
constexpr int pdTRUE=1;int pdMS_TO_TICKS(int n){return n;}int xSemaphoreTake(int,int){return 1;}void xSemaphoreGive(int){}bool finishIndication(bool){return true;}bool preemptBackground(){return false;}uint32_t millis(){return now;}void delay(int n){now+=n;}void vTaskDelay(int n){now+=n;}void pikaActivityLedTraffic(bool){}
'''
main=r'''int main(){std::vector<uint8_t> expected;for(int test=0;test<5;test++){std::vector<uint8_t> b(test==4?44:4096);for(size_t i=0;i<b.size();i++)b[i]=uint8_t(i+test);if(test==0)pressure=true;if(test==1)drop=true;if(test==2)corrupt=true;if(test==3)loseAck=true;assert(sendUploadWindow(0,b.data(),b.size()));expected.insert(expected.end(),b.begin(),b.end());assert(sink==expected);assert(channels[0].uploadSent==sink.size());}online=false;uint8_t b=0;assert(!sendUploadWindow(0,&b,1));puts("Production upload sender: backpressure, dropped/corrupted frame, lost ACK retry, short final window, disconnect: PASS");}'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(stub+code+main);exe=p/'upload.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
