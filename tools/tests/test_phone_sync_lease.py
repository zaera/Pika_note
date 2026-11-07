from pathlib import Path
import re,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'pika_note/src/app/phone_sync.cpp').read_text()
functions='\n'.join(re.search(r'^'+signature+r'.*$',s,re.M).group() for signature in ['void pikaPhoneSyncLease','void pikaPhoneSyncProgress','bool pikaPhoneSyncActive'])
source="""#include <atomic>
#include <cassert>
#include <cstdint>
uint32_t now=100;int resets=0;bool online=true;
std::atomic<uint32_t> leaseAt{0},leaseStarted{0};
uint32_t millis(){return now;}void resetActivity(){resets++;}bool pikaPhoneOnline(){return online;}
"""+functions+"""
int main(){assert(!pikaPhoneSyncActive());pikaPhoneSyncLease(true);assert(pikaPhoneSyncActive());for(int i=0;i<100;i++){now+=10000;pikaPhoneSyncProgress();assert(pikaPhoneSyncActive());}now+=15001;assert(!pikaPhoneSyncActive());pikaPhoneSyncLease(true);online=false;assert(!pikaPhoneSyncActive());online=true;pikaPhoneSyncLease(false);assert(!pikaPhoneSyncActive()&&resets==0);now=0xfffffff0;pikaPhoneSyncLease(true);now=32;assert(pikaPhoneSyncActive());}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(source);exe=p/'lease.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
print('Sleep lease: long active transfer, stalled transfer, disconnect, release and millis wrap: PASS')
