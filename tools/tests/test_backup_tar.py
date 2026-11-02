from pathlib import Path
import runpy,subprocess,tempfile,sys,tarfile,io
root=Path(__file__).resolve().parents[2];b=runpy.run_path(str(root/'tools/tests/test_manager_store.py'))
s=(root/'pika_note/src/app/data_backup.cpp').read_text(encoding='utf-8');production=s[s.index('#pragma pack(push'):s.index('void padding(')]
arduino=b['arduino'].replace(' const char* c_str()', ' void toCharArray(char* out,size_t n)const{if(n){strncpy(out,text.c_str(),n-1);out[n-1]=0;}}\n const char* c_str()')
cpp='#include "Arduino.h"\n#include <ctime>\nstruct Web {void sendContent(const char* p,size_t n){fwrite(p,1,n,stdout);}} server;auto* web=&server;\n'+production+'\nint main(){tarHeader("notes/note_001.txt",3);fwrite("abc",1,3,stdout);char zero[1533]={};fwrite(zero,1,sizeof(zero),stdout);}\n'
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'Arduino.h').write_text(arduino);(p/'test.cpp').write_text(cpp);exe=p/'tar.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-I',str(p),'-o',str(exe)],check=True)
 data=subprocess.check_output([str(exe)])
 with tarfile.open(fileobj=io.BytesIO(data)) as archive:
  assert archive.getnames()==['notes/note_001.txt'];assert archive.extractfile('notes/note_001.txt').read()==b'abc'
print('Production TAR writer: Python standard TAR reader validates header, checksum, file name and payload: PASS')
