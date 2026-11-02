from pathlib import Path
import runpy,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2];base=runpy.run_path(str(root/'tools/tests/test_manager_store.py'))
source=r'''#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 0
#define ARDUINOJSON_ENABLE_PROGMEM 0
#include "phone_inventory.h"
#include <cassert>
#include <cstdio>
PikaDataFile f(const char* name,uint32_t size){PikaDataFile out;out.name=name;out.size=size;out.modified=7;return out;}
int main(){JsonDocument doc;auto files=doc.to<JsonObject>();
assert(pikaUpdatePhoneInventory(files,{f("note_001.wav",100),f("note_002.wav",200)},{},true,1));
assert(!pikaUpdatePhoneInventory(files,{}, {},false,2));assert(!files["note_001.wav"]["deleted"]&&!files["note_002.wav"]["deleted"]);
assert(pikaUpdatePhoneInventory(files,{f("note_001.wav",100),f("note_003.txt",12)},{"note_001.wav","note_003.txt"},false,3));assert(files["note_001.wav"]["version"]==3&&!files["note_002.wav"]["deleted"]&&files["note_003.txt"]["size"]==12);
assert(pikaUpdatePhoneInventory(files,{}, {"note_002.wav"},false,4));assert(files["note_002.wav"]["deleted"]==true&&!files["note_001.wav"]["deleted"]);
assert(pikaUpdatePhoneInventory(files,{f("note_001.wav",100),f("extra.txt",6),f(".ask.wav",123),f("x.tmp",99)}, {},true,5));assert(files["note_003.txt"]["deleted"]==true&&files["extra.txt"]["size"]==6&&files[".ask.wav"].isNull()&&files["x.tmp"].isNull());
puts("Production incremental inventory: untouched files retained; same-size edits/new files/deletion tombstones/full-rescan/private WAV filtering: PASS");}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'Arduino.h').write_text(base['arduino'].replace('String(unsigned long v)', 'String(unsigned int v):text(std::to_string(v)){}String(unsigned long v)'));(p/'test.cpp').write_text(source);exe=p/'inventory.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-I',str(p),'-I',str(root/'pika_note/src/app'),'-I',str(root/'.pio/libdeps/pika_note_2/ArduinoJson/src'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
