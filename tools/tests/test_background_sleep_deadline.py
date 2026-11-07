from pathlib import Path
import re,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'pika_note/src/app/phone_link.cpp').read_text(encoding='utf8')
production=re.search(r'^bool preemptBackground\(\).*$',s,re.M).group()
source=r'''#include <atomic>
#include <cassert>
#include <cstdint>
uint32_t now=100,lastActivityMs=100;bool background=false,buttons=false,lease=false;std::atomic<bool> rpcReady{false},interrupted{false};
uint32_t millis(){return now;}uint32_t pikaSleepIdleMs(){return 60000;}bool pikaBackgroundNetworkMode(){return background;}bool pikaHomeButtonsBusy(){return buttons;}bool pikaPhoneSyncActive(){return lease;}
'''+production+r'''
int main(){now=100000;assert(!preemptBackground());background=true;now=60099;assert(!preemptBackground());now=60100;assert(preemptBackground()&&interrupted);interrupted=false;lease=true;assert(!preemptBackground());rpcReady=true;assert(preemptBackground());rpcReady=false;lease=false;lastActivityMs=0xfffffff0u;now=0xfffffff0u+59999u;assert(!preemptBackground());now=0xfffffff0u+60000u;assert(preemptBackground());}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(source);exe=p/'deadline.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
print('Background sleep deadline: exact boundary, foreground immunity, active-transfer protection, button/RPC priority and timer wrap: PASS')
