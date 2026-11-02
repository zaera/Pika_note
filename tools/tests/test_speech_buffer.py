from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
source=r'''#include "pcm_stream.h"
#include <cassert>
#include <cstdio>
int main(){bool waiting=false;
assert(!pcmStreamWaitForBuffer(1024,false,65536,waiting));
assert(pcmStreamWaitForBuffer(0,false,65536,waiting));
assert(pcmStreamWaitForBuffer(1024,false,65536,waiting));
assert(pcmStreamWaitForBuffer(23998,false,65536,waiting));
assert(!pcmStreamWaitForBuffer(24000,false,65536,waiting));
assert(!pcmStreamWaitForBuffer(1024,false,65536,waiting));
assert(pcmStreamWaitForBuffer(0,false,65536,waiting));
assert(!pcmStreamWaitForBuffer(100,true,65536,waiting));
assert(!waiting);assert(pcmStreamAvailable(10,0xfffffff0U)==26);
puts("Speech buffer: half-second hysteresis, short EOF, unsigned wrap: PASS");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(source);exe=p/'speech.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-I',str(root/'pika_note/src/app'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
