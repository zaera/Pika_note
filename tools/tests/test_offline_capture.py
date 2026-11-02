from pathlib import Path
import subprocess,tempfile,sys,re
root=Path(__file__).resolve().parents[2];s=(root/'pika_note/pika_note.ino').read_text(encoding='utf-8');a=s.index('  if(!pikaPhoneOnline()){',s.index('bool processCapturedMemoryNow'));b=s.index('  showMemoryProcessing(noteNum, "CONNECTING");',a);branch=s[a:b]
code=r"""
#include <cassert>
#include <cstring>
#include <iostream>
bool online=false,queued=false,shown=false;int waits=0;
bool pikaPhoneOnline(){return online;}
void writeNoteQueueState(int n,const char* state,const char* reason){assert(n==42);queued=std::strcmp(state,"queued")==0;}
void showMemoryProcessing(int n,const char* state){assert(n==42);shown=std::strcmp(state,"QUEUED")==0;}
void delay(int n){waits+=n;}
bool captured(int noteNum){
"""+branch+r"""
return true;}
int main(){assert(!captured(42)&&queued&&shown&&waits==250);online=true;queued=shown=false;waits=0;assert(captured(42)&&!queued&&!shown&&waits==0);std::cout<<"Production offline memory: queued immediately, no reconnect wait; connected path retained PASS\n";}
"""
# Prove the queue branch is reached only after durable WAV and index commit.
capture=s[s.index('void captureMemory('):s.index('bool phoneRequired(')]
assert capture.index('record(holdPin)')<capture.index('saveTag(')<capture.index('processCapturedMemoryNow(')
for name in ('tools/manager.html','android-companion/app/src/main/assets/manager.html'):
 html=(root/name).read_text(encoding='utf-8');assert 'id="wifi-editor"' not in html and 'id="config-network"' not in html and "$('wifi-form')" not in html and "$('wifi-editor')" not in html
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);f=p/'offline.cpp';f.write_text(code,encoding='utf-8');exe=p/'offline.exe';subprocess.run((sys.argv[1:] or ['c++'])+[str(f),'-std=c++17','-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
