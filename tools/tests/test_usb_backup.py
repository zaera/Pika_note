from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
source=r'''
#include "usb_backup_read.h"
#include <cassert>
#include <vector>
#include <iostream>
int main(){std::vector<uint8_t> card(4096);for(size_t i=0;i<card.size();i++)card[i]=(i*17+3)%251;uint8_t scratch[512];int reads=0;
auto reader=[&](uint8_t* dest,uint32_t block){reads++;assert(block<8);memcpy(dest,card.data()+512*block,512);return true;};
for(uint32_t offset:{0u,1u,511u,512u,700u})for(uint32_t count:{1u,511u,512u,1025u}){std::vector<uint8_t> out(count);assert(pikaBackupRead(1,offset,out.data(),count,8,512,scratch,reader)==count);assert(!memcmp(out.data(),card.data()+512+offset,count));}
uint8_t out[1024];int before=reads;assert(pikaBackupRead(7,510,out,3,8,512,scratch,reader)==-1);assert(pikaBackupRead(UINT32_MAX,UINT32_MAX,out,2,8,512,scratch,reader)==-1);assert(pikaBackupRead(0,0,out,1,8,0,scratch,reader)==-1);assert(reads==before);
assert(pikaBackupRead(0,0,out,1,8,512,scratch,[](uint8_t*,uint32_t){return false;})==-1);std::cout<<"USB backup: unaligned/multisector reads, end-of-card/overflow bounds, I/O failures: PASS\n";}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(source)
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-I',str(root/'pika_note/src/app'),'-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)