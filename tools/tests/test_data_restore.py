from pathlib import Path
import runpy,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
b=runpy.run_path(str(root/'tools/tests/test_undo_store.py'))
s=(root/'pika_note/src/app/data_backup.cpp').read_text(encoding='utf-8')
swap=s[s.index('bool swapRestore('):s.index('#pragma pack(push')]
commit=s[s.index('void commitRestore()'):s.index('void cancelRestore()')]
cancel=s[s.index('void cancelRestore()'):s.index('\n}\nbool pikaRestoreBusy')]
recover=s[s.index('bool pikaRestoreRecover('):s.index('void pikaBackupRoutes(')]
fakes=r'''
namespace backupTest {
const String stageRoot="/.pika-restore-stage",oldRoot="/.pika-restore-old",restoreJournal="/.pika-restore-journal.json";
bool restoring=false,shaActive=false;String session,lastCommitted;uint32_t initialRevision=0;JsonDocument restoreManifest;File incoming;int uploadSha;
std::vector<bool> uploaded;int code=0;String lastError;
struct FakeWeb{String body;String arg(const char*){return body;}} server;auto* web=&server;
void (*onRestored)()=nullptr;
void json(int c,const String& error=String()){code=c;lastError=error;}
void mbedtls_sha256_free(int*){}
String stagedPath(size_t i){return stageRoot+"/notes/"+String(restoreManifest["files"][i]["name"]|"");}
'''
main=r'''
void putData(const String& p,const String& text){auto f=SD_MMC.open(p,FILE_WRITE);f.print(text);}
void prepare(){
 pikaUndoClear();files.clear();dirs={"/notes","/.pika-restore-stage","/.pika-restore-stage/notes"};noteIndex.clear();base.clear();managerInvalidateReadSnapshot();restoring=true;session="sessionA";lastCommitted="";restoreManifest.clear();uploaded.clear();code=0;
 putData("/notes/index.csv","old catalog");putData("/notes/keep.wav","old audio");initialRevision=managerRevision();
 putData(stageRoot+"/notes/index.csv","new catalog");putData(stageRoot+"/notes/manager.json",R"({"revision":2,"records":[{"id":"fresh","type":"todo","text":"Restored task","priority":1}]})");
 auto rows=restoreManifest["files"].to<JsonArray>();for(const char* name:{"index.csv","manager.json"}){auto r=rows.add<JsonObject>();r["name"]=name;String hash;uint32_t size;pikaDataHash(stageRoot+"/notes/"+name,hash,&size);r["size"]=size;r["sha256"]=hash;uploaded.push_back(true);}
 web->body=R"({"session":"sessionA","confirmed":true})";
}
void intent(){JsonDocument j;j["state"]="prepared";j["session"]=session;j["revisionFloor"]=initialRevision;assert(pikaDataWriteJson(restoreJournal,j));}
void assertions(){assert(*files["/notes/index.csv"]=="new catalog");assert(*files["/.pika-restore-old/index.csv"]=="old catalog");assert(*files["/.pika-restore-old/keep.wav"]=="old audio");assert(!SD_MMC.exists("/notes/keep.wav"));assert(managerRevision()>initialRevision);}
int run(){
 String error;
 prepare();commitRestore();assert(code==200&&!restoring);assertions();auto revision=managerRevision();commitRestore();assert(code==200&&managerRevision()==revision);
 prepare();putData(stagedPath(0),"tampered");commitRestore();assert(code==400&&*files["/notes/index.csv"]=="old catalog"&&!SD_MMC.exists(oldRoot));
 prepare();uploaded[1]=false;commitRestore();assert(code==400&&!SD_MMC.exists(oldRoot));
 prepare();putData(stagedPath(1),"not JSON");String hash;uint32_t size;pikaDataHash(stagedPath(1),hash,&size);restoreManifest["files"][1]["sha256"]=hash;restoreManifest["files"][1]["size"]=size;commitRestore();assert(code==400&&!SD_MMC.exists(oldRoot));
 prepare();assert(managerBumpRevision(initialRevision));commitRestore();assert(code==409&&!SD_MMC.exists(oldRoot));
 // Boot recovery at each directory-swap boundary.
 for(int boundary=0;boundary<3;boundary++){prepare();intent();if(boundary>=1)assert(SD_MMC.rename("/notes",oldRoot));if(boundary>=2)assert(SD_MMC.rename(stageRoot+"/notes","/notes"));assert(pikaRestoreRecover(error));assertions();JsonDocument j;assert(pikaDataReadJson(restoreJournal,j)&&String(j["state"]|"")=="complete");}
 // A stale browser tab cannot cancel another restore session.
 prepare();web->body=R"({"session":"wrong-session","confirmed":true})";cancelRestore();assert(code==409&&restoring&&SD_MMC.exists(stageRoot));
 web->body=R"({"session":"sessionA","confirmed":true})";cancelRestore();assert(code==200&&!restoring&&!SD_MMC.exists(stageRoot)&&*files["/notes/index.csv"]=="old catalog");
 puts("Real restore activation: checksum/missing/corrupt store/stale revision refusal, atomic swap, all power-loss boundaries, rollback preservation and cancellation: PASS");return 0;
}
}
int main(){return backupTest::run();}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'Arduino.h').write_text(b['b']['arduino']);(p/'WString.h').write_text('#include "Arduino.h"');(p/'test.cpp').write_text(b['prefix']+b['pathfunc']+b['helpers']+b['b']['source']+b['u']+fakes+swap+commit+cancel+recover+main,encoding='utf-8');exe=p/'restore.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-I',str(p),'-I',str(root/'pika_note/src/app'),'-I',str(root/'.pio/libdeps/pika_note_2/ArduinoJson/src'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
