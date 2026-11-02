from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
s=(root/'pika_note/src/app/mail.cpp').read_text(encoding='utf-8')
request=s[s.index('int request('):s.index('bool authorize()')]
source=r'''
#include "Arduino.h"
#include <cassert>
#include <iostream>
using std::min;
struct PikaBusyLight{};
bool background=false,busy=false;bool pikaBackgroundNetworkMode(){return background;}bool pikaHomeButtonsBusy(){return busy;}
unsigned ticks=0;unsigned millis(){return ticks++;}void delay(int n){ticks+=n;}
struct {void println(const char*){} template<class... T>void printf(const char*,T...){} } Serial;
const unsigned char pika_ca_bundle_start[]="test";
String accessToken="token";int tokenExpiry=100;void invalidateMailToken(){accessToken="";tokenExpiry=0;}
struct JsonDocument{void clear(){}};bool deserializeJson(JsonDocument&,const String& body){return body!="{}";}
struct WiFiClientSecure{void setCACert(const char*){}void setHandshakeTimeout(int){}};
int ended=0,httpCode=200;bool closed=false;String data="{}";
struct Stream{int available(){return data.length();}int readBytes(char* p,int n){memcpy(p,data.c_str(),n);data=data.substring(n);return n;}} stream;
struct HTTPClient{
 void useHTTP10(bool){}bool begin(WiFiClientSecure&,const String&){return true;}void setConnectTimeout(int){}void setTimeout(int){}void setReuse(bool){}void addHeader(const char*,const String&){}
 int POST(const String&){return httpCode;}int GET(){return httpCode;}int getSize(){return 2;}Stream* getStreamPtr(){return closed?nullptr:&stream;}bool connected(){return !closed;}void end(){ended++;}
};
'''+request+r'''
int main(){JsonDocument doc;
 closed=true;assert(request("url",nullptr,doc)==-2);assert(ended==1);
 closed=false;data="{}";assert(request("url",nullptr,doc)==200);assert(ended==2);
 closed=true;httpCode=204;assert(request("url",nullptr,doc)==204);assert(ended==3);
 closed=true;httpCode=401;assert(request("url",nullptr,doc)==-2); // failed body still safely returns
 background=true;busy=true;assert(request("url",nullptr,doc)==-5);assert(ended==4);
 std::cout<<"Production Gmail HTTP: closed/null response stream, normal JSON, cancellation: PASS\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.cpp').write_text(source,encoding='utf-8');exe=p/'mail-http.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+['-I'+str(root/'tools/tests/host'),str(p/'test.cpp'),'-std=c++17','-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)

