from pathlib import Path
import runpy,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
base=runpy.run_path(str(root/'tools/tests/test_manager_store.py'))
prefix=base['prefix'].replace('struct FS {','int fileOpens=0;\nstruct FS {').replace('const std::string key=path.c_str();if(mode)','++fileOpens;const std::string key=path.c_str();if(mode)')
main=r"""
int main(){
 String id,error;JsonDocument create;create["kind"]="tasks";create["record"]["text"]="Read cache test";assert(managerMutation(create.as<JsonVariantConst>(),id,error));
 {ManagerReadSnapshot scope;auto rev=managerRevision();int opened=fileOpens;
  for(int i=0;i<20;++i){assert(managerRevision()==rev);assert(loadBrainTasks().size()==1);}
  assert(fileOpens==opened);
  {ManagerReadSnapshot nested;assert(managerRevision()==rev);}
  assert(fileOpens==opened);
  create["record"]["text"]="A second task";assert(managerMutation(create.as<JsonVariantConst>(),id,error));
  assert(managerRevision()>rev);assert(loadBrainTasks().size()==2);
 }
 int opened=fileOpens;managerRevision();assert(fileOpens>opened);
 puts("Manager snapshot: repeated reads reuse SD data, nested scopes, mutations invalidate, scope exit releases snapshot: PASS");
}
"""
with tempfile.TemporaryDirectory() as directory:
 d=Path(directory);(d/'Arduino.h').write_text(base['arduino']);(d/'WString.h').write_text('#include "Arduino.h"')
 (d/'test.cpp').write_text(prefix+base['source']+main,encoding='utf-8');exe=d/'cache.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(d/'test.cpp'),'-std=c++17','-I',str(d),'-I',str(root/'pika_note/src/app'),'-I',str(root/'.pio/libdeps/pika_note_2/ArduinoJson/src'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
