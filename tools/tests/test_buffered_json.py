from pathlib import Path
import subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2]
source=r'''#include "buffered_json_file.h"
#include <cassert>
#include <string>
#include <cstdio>
struct File{std::string bytes;size_t at=0,reads=0,writes=0;bool shortWrite=false;
size_t readBytes(char*,size_t){assert(false&&"File block read must avoid Stream timeout");return 0;}
size_t read(uint8_t* out,size_t count){++reads;count=std::min(count,bytes.size()-at);memcpy(out,bytes.data()+at,count);at+=count;return count;}
size_t write(const uint8_t* data,size_t count){++writes;if(shortWrite)count/=2;bytes.append((const char*)data,count);return count;}};
int main(){JsonDocument doc;doc["body"]=std::string(30000,'x');doc["done"]=false;File f;
auto written=pikaWriteJsonBuffered(doc,f);assert(written==measureJson(doc)&&f.bytes.size()==written&&f.writes<40);
JsonDocument read;assert(!pikaReadJsonBuffered(read,f));assert(read["body"].as<std::string>()==std::string(30000,'x')&&f.reads<40);
File broken;broken.shortWrite=true;assert(pikaWriteJsonBuffered(doc,broken)==0);
File truncated;truncated.bytes=f.bytes.substr(0,f.bytes.size()/2);assert(pikaReadJsonBuffered(read,truncated));
printf("Buffered JSON: 30KB roundtrip in %zu writes/%zu reads; short write and truncated JSON rejected: PASS\n",f.writes,f.reads);}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.cpp').write_text(source);exe=p/'json.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-I',str(root/'pika_note/src/app'),'-I',str(root/'.pio/libdeps/pika_note_2/ArduinoJson/src'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
