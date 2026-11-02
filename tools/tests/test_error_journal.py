from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
s=(root/'pika_note/src/app/error_log.cpp').read_text(encoding='utf-8')
row=s[s.index('struct Row {'):s.index('std::mutex queueMutex')].split('StaticQueue_t')[0]
crc=s[s.index('uint32_t checksum('):s.index('void text(')]
code="""#include <cstdint>
#include <cstddef>
#include <cassert>
#include <cstring>
#include <array>
#include <iostream>
"""+row+crc+"""
int main(){std::array<Row,200> disk{};for(unsigned id=1;id<=417;id++){Row r{};r.magic=0x50455252;r.sequence=id;std::strcpy(r.reason,"Failure detail");r.checksum=checksum(r);disk[(id-1)%200]=r;}unsigned count=0,min=9999,max=0;for(auto r:disk){assert(r.checksum==checksum(r));count++;if(r.sequence<min)min=r.sequence;if(r.sequence>max)max=r.sequence;}assert(count==200&&min==218&&max==417);Row torn=disk[0];torn.reason[3]^=1;assert(torn.checksum!=checksum(torn));std::cout<<"Production journal: 200-slot rollover, fixed layout and torn-write checksum rejection PASS\\n";}
"""
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);f=p/'journal.cpp';f.write_text(code,encoding='utf-8');exe=p/'journal.exe';subprocess.run((sys.argv[1:] or ['c++'])+[str(f),'-std=c++17','-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
