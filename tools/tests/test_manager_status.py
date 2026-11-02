from pathlib import Path
import runpy,tempfile,subprocess,sys
root=Path(__file__).resolve().parents[2];fixture=runpy.run_path(str(root/'tools/tests/test_manager_store.py'))
prefix=fixture['prefix'].replace('bool setBrainTaskDone(const String& id,bool done){return managerSetTaskDone(id,done)>0;}','bool setBrainTaskDone(const String&,bool){assert(false && "Manager status must not recurse through legacy writer");return false;}')
main=r"""
int main(){
 BrainTaskRecord source;source.id="source-task";source.text="Buy milk";source.noteNum=7;base.push_back(source);
 JsonDocument request;request["kind"]="tasks";request["action"]="toggle";request["id"]="source-task";request["record"]["done"]=true;request["revision"]=managerRevision();String id,error;
 auto before=managerRevision();assert(managerMutation(request.as<JsonVariantConst>(),id,error));assert(id=="source-task"&&managerRevision()==before+1);auto tasks=loadBrainTasks();assert(tasks.size()==1&&tasks[0].done&&tasks[0].noteNum==7&&tasks[0].text=="Buy milk");
 request["revision"]=managerRevision();request["record"]["done"]=false;assert(managerMutation(request.as<JsonVariantConst>(),id,error));assert(managerRevision()==before+2&&!loadBrainTasks()[0].done);
 JsonDocument manual;manual["kind"]="tasks";manual["record"]["text"]="123";assert(managerMutation(manual.as<JsonVariantConst>(),id,error));const String manualId=id;
 request["id"]=manualId;request["record"]["done"]=true;request["revision"]=managerRevision();before=managerRevision();assert(managerMutation(request.as<JsonVariantConst>(),id,error)&&managerRevision()==before+1);
 tasks=loadBrainTasks();auto found=std::find_if(tasks.begin(),tasks.end(),[&](const BrainTaskRecord& t){return t.id==manualId;});assert(found!=tasks.end()&&found->done);
 request["record"]["done"]=false;assert(!managerMutation(request.as<JsonVariantConst>(),id,error)&&managerRevision()==before+1);
 puts("Production task status: manual/source IDs preserved, one commit per Done/Undone, no recursive writer, stale revision still rejected: PASS");
}
"""
with tempfile.TemporaryDirectory() as directory:
 d=Path(directory);(d/'Arduino.h').write_text(fixture['arduino'],encoding='utf-8');(d/'WString.h').write_text('#include "Arduino.h"')
 (d/'test.cpp').write_text(prefix+fixture['source']+main,encoding='utf-8');exe=d/'status.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(d/'test.cpp'),'-std=c++17','-I',str(d),'-I',str(root/'pika_note/src/app'),'-I',str(root/'.pio/libdeps/pika_note_2/ArduinoJson/src'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
