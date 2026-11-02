from pathlib import Path
import runpy,tempfile,subprocess,sys
root=Path(__file__).resolve().parents[2];base=runpy.run_path(str(root/'tools/tests/test_manager_store.py'))
main=r"""
struct NoAllocation:ArduinoJson::Allocator {
 void* allocate(size_t)override{return nullptr;}
 void deallocate(void*)override{}
 void* reallocate(void*,size_t)override{return nullptr;}
};
int main(){
 JsonDocument seed;seed["revision"]=110;seed["records"].add<JsonObject>()["text"]="Saved task";String text;serializeJson(seed,text);files["/notes/manager.json"]=std::make_shared<std::string>(text.c_str());
 NoAllocation publisherFailure;readSnapshot=JsonDocument(&publisherFailure);
 {ManagerReadSnapshot scope;JsonDocument original;assert(readStore(original)&&original["revision"]==110);assert(!readSnapshotReady);JsonDocument retry;assert(readStore(retry)&&retry["revision"]==110);}
 readSnapshot=JsonDocument();
 ManagerReadSnapshot scope;JsonDocument first;assert(readStore(first)&&first["revision"]==110);
 NoAllocation none;JsonDocument failed(&none);assert(!readStore(failed));
 JsonDocument good;assert(readStore(good)&&good["revision"]==110&&good["records"].size()==1);
 puts("Production manager snapshot: failed allocation is an error, not a false empty revision; subsequent reads preserve data: PASS");
}
"""
with tempfile.TemporaryDirectory() as directory:
 d=Path(directory);(d/'Arduino.h').write_text(base['arduino'],encoding='utf-8');(d/'WString.h').write_text('#include "Arduino.h"')
 (d/'test.cpp').write_text(base['prefix']+base['source']+main,encoding='utf-8');exe=d/'snapshot.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(d/'test.cpp'),'-std=c++17','-I',str(d),'-I',str(root/'pika_note/src/app'),'-I',str(root/'.pio/libdeps/pika_note_2/ArduinoJson/src'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
