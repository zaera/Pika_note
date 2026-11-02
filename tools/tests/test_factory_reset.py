from pathlib import Path
import runpy,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2];b=runpy.run_path(str(root/'tools/tests/test_undo_store.py'))
fake=r'''
std::map<std::string,std::map<std::string,String>> nv;
class Preferences{std::string ns;public:bool begin(const char* name,bool){ns=name;return true;}void end(){}bool getBool(const char* k,bool def){return nv[ns].count(k)?nv[ns][k]=="1":def;}String getString(const char* k,const char* def){return nv[ns].count(k)?nv[ns][k]:String(def);}size_t putString(const char* k,const String& v){nv[ns][k]=v;return v.length();}size_t putBool(const char* k,bool v){nv[ns][k]=v?"1":"0";return 1;}size_t putInt(const char* k,int v){nv[ns][k]=String(v);return 4;}bool clear(){nv[ns].clear();return true;}bool remove(const char* k){return nv[ns].erase(k)>0;}};
int mbedtls_sha256(const unsigned char*,size_t,unsigned char* out,int){memset(out,42,32);return 0;}
struct esp_partition_t{size_t size=65536;};constexpr int ESP_PARTITION_TYPE_DATA=1,ESP_PARTITION_SUBTYPE_ANY=0,ESP_OK=0;esp_partition_t part;int erased=0;
const esp_partition_t* esp_partition_find_first(int,int,const char*){return &part;}int esp_partition_erase_range(const esp_partition_t*,int,size_t){++erased;return ESP_OK;}
String failTree;
'''
helpers=b['helpers'].replace('bool pikaDataRemoveTree(const String& path){','bool pikaDataRemoveTree(const String& path){if(path==failTree){failTree="";return false;}')
s=(root/'pika_note/src/app/factory_reset.cpp').read_text();production=s[s.index('namespace {'):]
main=r'''
void putData(const char* path,const char* data){auto f=SD_MMC.open(path,FILE_WRITE);f.print(data);}
int main(){
 String error;assert(!pikaFactoryConfigSuppressed());for(const char* p:{"","123","12345","12a4"})assert(!pikaFactoryRequest(p,error));assert(nv["pikaFactory"].count("pending")==0);
 for(const char* ns:{"pika","pikaPin","pikaMail","pikaWeather","pikaExtras","pikaLed","pikaClock"})nv[ns]["private"]="secret";
 assert(pikaFactoryRequest("1234",error));assert(pikaFactoryConfigSuppressed());assert(nv["pika"].count("private"));assert(pikaFactoryPrepareBoot());assert(erased==2);for(const char* ns:{"pika","pikaPin","pikaMail","pikaWeather","pikaExtras","pikaLed","pikaClock"})assert(!nv[ns].count("private"));assert(nv["pikaPin"]["hash"].length()==64&&nv["pikaPin"]["length"]=="4");
 dirs.insert("/.pika-restore-old");dirs.insert("/.pika-restore-stage");putData("/notes/note_001.wav","recording");putData("/notes/.undo-live/before-0","undo");putData("/.pika-restore-old/old.txt","rollback");putData("/.pika-restore-stage/staged.txt","stage");putData("/.pika-restore-journal.json","journal");putData("/holiday.jpg","keep");
 failTree="/.pika-restore-old";assert(!pikaFactoryRecoverData(error));assert(nv["pikaFactory"]["pending"]=="1");assert(pikaFactoryPrepareBoot());assert(pikaFactoryRecoverData(error));assert(nv["pikaFactory"]["pending"]=="0"&&!nv["pikaFactory"].count("newPin"));assert(SD_MMC.exists("/notes")&&SD_MMC.exists("/holiday.jpg")&&!SD_MMC.exists("/.pika-restore-old")&&!SD_MMC.exists("/.pika-restore-stage")&&!SD_MMC.exists("/.pika-restore-journal.json"));assert(files.size()==1);assert(pikaFactoryConfigSuppressed());
 nv["pika"]["newSettings"]="keep";assert(pikaFactoryPrepareBoot()&&pikaFactoryRecoverData(error));assert(nv["pika"]["newSettings"]=="keep");puts("Production factory reset: PIN validation, durable intent, clear settings, new PIN hash, private partitions, interrupted SD deletion/retry, unrelated files and subsequent settings preserved: PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'Arduino.h').write_text(b['b']['arduino'].replace(' const char* c_str()', ' auto begin()const{return text.begin();} auto end()const{return text.end();} const char* c_str()'));(p/'WString.h').write_text('#include "Arduino.h"');(p/'test.cpp').write_text(b['prefix']+b['pathfunc']+fake+helpers+b['b']['source']+b['u']+production+main,encoding='utf-8');exe=p/'factory.exe';subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-I',str(p),'-I',str(root/'pika_note/src/app'),'-I',str(root/'.pio/libdeps/pika_note_2/ArduinoJson/src'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
