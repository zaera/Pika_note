from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
source=r'''#include "recovery_hold.h"
#include <cassert>
#include <iostream>
int main(){PikaRecoveryHold h;
 for(unsigned t=0;t<10000;t+=10)assert(!h.update(t,true,false));
 assert(!h.update(10000,true,true));assert(!h.update(14999,true,true));assert(!h.requested);
 assert(h.update(15000,true,true));assert(h.requested);assert(!h.update(15010,true,true));assert(!h.update(16000,false,false));assert(!h.update(22000,true,true));
 PikaRecoveryHold shortHold;assert(!shortHold.update(0,true,true));assert(!shortHold.update(4999,false,false));assert(!shortHold.update(5000,true,true));assert(!shortHold.update(9999,false,false));assert(!shortHold.requested);
 PikaRecoveryHold wrap;assert(!wrap.update(0xfffffff0u,true,true));assert(wrap.update(0xfffffff0u+5000u,true,true));assert(wrap.requested);assert(!wrap.update(5100u,false,false));assert(!wrap.update(5150u,false,false));
 std::cout<<"Recovery hold: five-second overlap, immediate trigger and no repeated trigger, single key, short holds and timer wrap: PASS\n";}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(source)
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-I',str(root/'pika_note/src/app'),'-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)
