from pathlib import Path
import runpy,tempfile,subprocess,sys
root=Path(__file__).resolve().parents[2];base=runpy.run_path(str(root/'tools/tests/test_manager_store.py'))
prefix=base['prefix']
notes=(root/'pika_note/src/app/notes.cpp').read_text(encoding='utf-8');production=notes[notes.index('bool deleteNote(int num)'):notes.index('bool deleteAllNotes()')]
a=prefix.index('bool deleteNote(int num)');b=prefix.index('std::vector<BrainTaskRecord> base;',a)
prefix=prefix[:a]+'bool saveIndex(){return true;}\n'+production+prefix[prefix.index('#include "undo_store.h"',a):b]+prefix[b:]
prefix=prefix.replace('bool remove(const String& path){return files.erase(path.c_str())>0;}', 'bool remove(const String& path){if(path==failRemove){failRemove="";return false;}return files.erase(path.c_str())>0;}').replace('struct FS {','String failRemove;\nstruct FS {')
main=r'''
void put(const char* path,const char* value){auto f=SD_MMC.open(path,FILE_WRITE);f.print(value);}
BrainTaskRecord task(const char* id,int note,bool done=false,const char* type="todo",const char* date="",const char* end=""){BrainTaskRecord t;t.id=id;t.noteNum=note;t.text=id;t.done=done;t.type=type;t.dueUtc=date;t.endUtc=end;return t;}
int main(){
 noteIndex={{1,true},{2,true}};
 put("/notes/note_001.archived","archived");put("/notes/note_001.txt","secret transcript");put("/notes/note_001.wav","wav");put("/notes/note_001.txt.bak","backup");put("/notes/note_001.brain.vec","vector");
 put("/notes/note_002.txt","Keep original memory");put("/notes/note_002.brain.json",R"({"tasks":[{"id":"done","title":"Done secret"},{"id":"open","title":"Open"}]})");
 put("/notes/tasks.state","done|1|time|secret\nopen|0|time|keep\n");
 base={task("child",1),task("done",2,true),task("open",2),task("past",2,false,"event","2026-10-18T12:00:00Z"),task("ongoing",2,false,"event","2026-10-19T12:00:00Z","2026-10-22T12:00:00Z"),task("hidden",2)};base.back().archived=true;
 JsonDocument request;request["kind"]="archive";request["action"]="purge";request["revision"]=managerRevision();String id,error;
 assert(!managerMutation(request.as<JsonVariantConst>(),id,error));assert(managerNoteExists(1)&&managerRevision()==0);
 request["confirmed"]=true;request["revision"]=99;assert(!managerMutation(request.as<JsonVariantConst>(),id,error));assert(managerNoteExists(1));
 request["revision"]=managerRevision();failRemove="/notes/note_001.wav";
 assert(!managerMutation(request.as<JsonVariantConst>(),id,error));assert(error.indexOf('i')>=0); // partial removal is not reported as success
 JsonDocument store;{File f=SD_MMC.open("/notes/manager.json");assert(!deserializeJson(store,f));}assert(store["archivePurge"]["notes"].size()==1);
 request["revision"]=managerRevision();request["clientRequest"]="purge-test";assert(managerMutation(request.as<JsonVariantConst>(),id,error));
 assert(!managerNoteExists(1)&&managerNoteExists(2));assert(!SD_MMC.exists("/notes/note_001.txt")&&!SD_MMC.exists("/notes/note_001.txt.bak")&&!SD_MMC.exists("/notes/note_001.wav")&&!SD_MMC.exists("/notes/note_001.brain.vec"));
 assert(managerReadText(2)=="Keep original memory");assert(*files["/notes/tasks.state"]=="open|0|time|keep\n");
 JsonDocument brain;{File f=SD_MMC.open("/notes/note_002.brain.json");assert(!deserializeJson(brain,f));}assert(brain["tasks"].size()==1&&String(brain["tasks"][0]["id"]|"")=="open");
 base.erase(std::remove_if(base.begin(),base.end(),[](const BrainTaskRecord& t){return t.noteNum==1;}),base.end());
 const auto remaining=loadBrainTasks();assert(remaining.size()==3);auto child=std::find_if(remaining.begin(),remaining.end(),[](const BrainTaskRecord& t){return t.id=="child";});assert(child!=remaining.end()&&child->noteNum==-1&&child->text=="child");
 assert(managerMutation(request.as<JsonVariantConst>(),id,error)); // lost response / duplicate request is safe
 puts("Production archive purge: confirmation, stale revision, full memory files, task bodies, active descendants, ongoing events, interrupted cleanup retry and idempotency: PASS");
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'Arduino.h').write_text(base['arduino']);(p/'WString.h').write_text('#include "Arduino.h"');(p/'test.cpp').write_text(prefix+base['source']+main,encoding='utf-8');exe=p/'archive-test.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-I',str(p),'-I',str(root/'pika_note/src/app'),'-I',str(root/'.pio/libdeps/pika_note_2/ArduinoJson/src'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
