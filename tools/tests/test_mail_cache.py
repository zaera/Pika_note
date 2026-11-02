from pathlib import Path
import runpy,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2];base=runpy.run_path(str(root/'tools/tests/test_manager_store.py'))
s=(root/'pika_note/src/app/mail.cpp').read_text(encoding='utf-8');a=s.index('void fetchBody(');b=s.index('void encodeCache(',a)
source=r"""
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 0
#define ARDUINOJSON_ENABLE_PROGMEM 0
#include "mail.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
std::vector<PikaMailItem> items;int fetches=0,code=200;
String encode(const String& s){return s;}
struct MailJsonAllocator:ArduinoJson::Allocator{void* allocate(size_t n)override{return malloc(n);}void deallocate(void* p)override{free(p);}void* reallocate(void* p,size_t n)override{return realloc(p,n);}};
int request(const String&,const String*,JsonDocument& d,size_t){++fetches;d["payload"]["body"]="New body";return code;}
void extractBody(JsonObjectConst d,int,String& plain,String&){plain=d["body"]|"";}
"""+s[a:b]+r"""
int main(){
 PikaMailItem cached;cached.id="old";cached.body="Cached body";cached.brief="Cached Russian recap";items.push_back(cached);
 PikaMailItem old;old.id="old";fetchBody(old);assert(fetches==0&&old.body==cached.body&&old.brief==cached.brief);
 PikaMailItem fresh;fresh.id="new";fetchBody(fresh);assert(fetches==1&&fresh.body=="New body"&&fresh.brief.length()==0);
 code=-1;PikaMailItem failed;failed.id="third";fetchBody(failed);assert(fetches==2&&failed.body.length()==0&&items[0].body=="Cached body");
 puts("Gmail cache: unchanged IDs reuse body/recap, new IDs fetch independently, failed reads preserve existing data: PASS");
}
"""
with tempfile.TemporaryDirectory() as directory:
 d=Path(directory);(d/'Arduino.h').write_text(base['arduino']);(d/'WString.h').write_text('#include "Arduino.h"');(d/'test.cpp').write_text(source,encoding='utf-8');exe=d/'mail-cache.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(d/'test.cpp'),'-std=c++17','-I',str(d),'-I',str(root/'pika_note/src/app'),'-I',str(root/'.pio/libdeps/pika_note_2/ArduinoJson/src'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
