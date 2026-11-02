from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
source=(root/'pika_note/src/app/phone_link.cpp').read_text(encoding='utf-8');method=next(line for line in source.splitlines() if line.startswith('bool pikaPhoneWait('))
code=r"""
#include <algorithm>
#include <cstdint>
#include <cassert>
#include <iostream>
uint32_t now=0,connectAt=999999;bool online=false;
uint32_t millis(){return now;}void delay(int n){now+=n;}void pikaPhoneService(){if(now>=connectAt)online=true;}
"""+method+r"""
int main(){connectAt=4500;assert(pikaPhoneWait(8000)&&now>=4500&&now<=4510);now=0;online=false;connectAt=999999;assert(!pikaPhoneWait(8000)&&now==8000);now=0;assert(!pikaPhoneWait(60000)&&now==15000);std::cout<<"Production phone wait: late reconnect accepted, finite offline wait and upper safety bound: PASS\n";}
"""
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);f=p/'wait.cpp';f.write_text(code,encoding='utf-8');exe=p/'wait.exe';subprocess.run((sys.argv[1:] or ['c++'])+[str(f),'-std=c++17','-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
