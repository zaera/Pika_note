from pathlib import Path
import sys,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'pika_note/src/app/draw.cpp').read_text(encoding='utf8');a=s.index('bool refreshRecordingStart()');b=s.index('\n}',a)+2
stub=r'''#include <cassert>
#include <cstdint>
int waits=0,frames=0,partials=0;bool failFirst=false,failAll=false,failPartial=false,epaperBaseReady=true;int epaperPartialRefreshes=5;
void pikaWaitHomeRefresh(){++waits;}uint32_t millis(){return 100;}
struct Log{template<class...A>void printf(const char*,A...){}}Serial;
struct Panel{bool EPD_DisplaySleepFrame(){++frames;return !failAll&&!(failFirst&&frames==1);}void EPD_Init_Partial(){++partials;}bool hasBusyTimeout(){return failPartial;}}panel;Panel* display=&panel;
'''
main=r'''int main(){assert(refreshRecordingStart());assert(waits==1&&frames==1&&partials==1&&epaperBaseReady&&epaperPartialRefreshes==0);frames=0;failFirst=true;assert(refreshRecordingStart()&&frames==2);frames=0;failAll=true;int before=partials;assert(!refreshRecordingStart()&&!epaperBaseReady&&frames==2&&partials==before);failAll=false;failFirst=false;failPartial=true;assert(!refreshRecordingStart()&&!epaperBaseReady);}'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(stub+s[a:b]+main);exe=p/'frame.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
print('Recording frame: confirmed completion, retry, failure and partial setup timeout PASS')
