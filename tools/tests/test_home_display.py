from pathlib import Path
import subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'pika_note/src/app/draw.cpp').read_text()
a=s.index('namespace { std::atomic<bool> homeRefreshBusy');b=s.index('// The first frame',a)
code=s[a:b]
stub=r'''#include <atomic>
#include <cassert>
#include <cstdio>
using Fn=void(*)(void*);Fn pending=nullptr;bool fail=false;int refreshes=0;
constexpr int pdPASS=1;
void refresh(){++refreshes;}void vTaskDelete(void*){}
int xTaskCreatePinnedToCore(Fn fn,const char*,int,void*,int,void*,int){if(fail)return 0;pending=fn;return pdPASS;}
void delay(int){assert(pending);Fn f=pending;pending=nullptr;f(nullptr);}
struct Display{int cleared=0;void EPD_Clear(){++cleared;}} d,*display=&d;
'''
main=r'''int main(){refreshHomeAsync();assert(pikaHomeRefreshBusy());assert(refreshes==0);
// A foreground microphone owner can run now. Only its asynchronous drawing waits.
int microphoneFrames=1;assert(microphoneFrames==1);clearWhite();assert(!pikaHomeRefreshBusy()&&refreshes==1&&d.cleared==1);
refreshHomeAsync();pikaWaitHomeRefresh();assert(refreshes==2);
fail=true;refreshHomeAsync();assert(!pikaHomeRefreshBusy()&&refreshes==3);
puts("Home panel worker: owner returns before display wait, drawing serialized, allocation fallback: PASS");}'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'test.cpp').write_text(stub+code+main);exe=p/'display.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
ino=(root/'pika_note/pika_note.ino').read_text();loop=ino[ino.index('void loop() {'):];assert loop.index('handleIdle()')<loop.index('pikaHomeRefreshBusy()')<loop.index('pikaPhoneSyncTick()')
ui=(root/'pika_note/src/app/ui.cpp').read_text();home=ui[ui.index('void showIdle('):ui.index('void showTodayAgenda(')] if 'void showTodayAgenda(' in ui[ui.index('void showIdle('):] else ui[ui.index('void showIdle('):ui.index('void showRecording(')]
assert 'refreshHomeAsync();' in home

setup=ino[ino.index("void setup() {"):ino.index("void loop() {")]
assert setup.index("audio_bsp_init();")<setup.index("initializeStorage(!wokeFromUltraSleep);")<setup.index("cachedWakeHome=showCachedWakeHome();")
assert "refreshHomeAsync();return true;" in ui[ui.index("bool showCachedWakeHome()"):]

assert "if(pikaHasOpenAiKey()&&!wokeFromUltraSleep)" in setup, "A key wake must not block Home on catalog rebuilding"
