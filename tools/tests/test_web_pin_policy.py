from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
src=(root/'pika_note/src/app/web_pin.cpp').read_text(encoding='utf-8')
namespace=src[src.index('namespace {'):src.index('void pikaLocalConfigBegin()')]
a=namespace.index('String digest(');b=namespace.index('bool equal(',a)
namespace=namespace[:a]+'String digest(const String& value){return value;}\n'+namespace[b:]
policy=src[src.index('bool pikaWebAccess('):src.index('bool pikaWebChangePin(')]
stubs=r'''
#include "Arduino.h"
#include <cassert>
#include <map>
#include <iostream>
struct Preferences{};
uint32_t tick=100;uint32_t millis(){return tick;}
void esp_fill_random(void* data,size_t n){static unsigned seed=1;auto bytes=(uint8_t*)data;for(size_t i=0;i<n;i++)bytes[i]=(seed+i)%256;seed++;}
enum{HTTP_GET,HTTP_POST};
struct WebServer{String path="/",cookie,pin;int verb=HTTP_GET,status=0;std::map<std::string,String> headers;
String uri(){return path;}int method(){return verb;}String header(const char*){return cookie;}String arg(const char*){return pin;}
void sendHeader(const char* name,const String& value){headers[name]=value;}void send(int code,const String& type="",const String& body=""){status=code;}
};
'''
tests=r'''
int main(){
 pinHash="1234";pinLength=4;
 for(const char* path:{"/","/api/manager","/api/manager/settings","/wav","/txt","/chat-history","/legacy"}){WebServer req;req.path=path;assert(!pikaWebAccess(req));assert(req.status==200||req.status==401);}
 WebServer bad;bad.path="/auth/unlock";bad.verb=HTTP_POST;bad.pin="0000";
 for(int i=0;i<5;i++){pikaWebAccess(bad);assert(bad.status==401);}
 bad.pin="1234";pikaWebAccess(bad);assert(bad.status==429);
 tick+=30001;WebServer login;login.path="/auth/unlock";login.verb=HTTP_POST;login.pin="1234";assert(!pikaWebAccess(login)&&login.status==200);
 String cookie=login.headers["Set-Cookie"];assert(cookie.indexOf("HttpOnly")>=0&&cookie.indexOf("SameSite=Strict")>=0);
 WebServer authorized;authorized.path="/api/manager";authorized.cookie=cookie;assert(pikaWebAccess(authorized));
 tick+=29*60*1000;assert(pikaWebAccess(authorized));
 tick+=30*60*1000;assert(!pikaWebAccess(authorized)&&authorized.status==401);
 tick+=1;pikaWebAccess(login);WebServer logout;logout.path="/auth/logout";logout.verb=HTTP_POST;logout.cookie=login.headers["Set-Cookie"];pikaWebAccess(logout);authorized.cookie=logout.cookie;assert(!pikaWebAccess(authorized));
 pinHash="";WebServer unconfigured;assert(!pikaWebAccess(unconfigured)&&unconfigured.status==503);
 std::cout<<"PIN policy: protected routes, wrong PIN, throttling, cookie flags, idle expiry, logout, fail-closed setup: PASS\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'pin.cpp').write_text(stubs+namespace+policy+tests,encoding='utf-8')
 compiler=sys.argv[1:] or ['c++']
 subprocess.run(compiler+[str(p/'pin.cpp'),'-std=c++17','-I',str(root/'tools/tests/host'),'-o',str(p/'pin.exe')],check=True)
 subprocess.run([str(p/'pin.exe')],check=True)
network=(root/'pika_note/src/app/network.cpp').read_text(encoding='utf-8')
assert network.index('if(!pikaWebAccess(server))')<network.index('if (servePortalSnapshot())')
print('PIN gate runs before cached responses: PASS')
