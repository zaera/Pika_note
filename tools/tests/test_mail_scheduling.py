from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
s=(root/'pika_note/src/app/mail.cpp').read_text(encoding='utf-8');a=s.index('bool pikaMailAssigned()');b=s.index('bool pikaMailRefresh()',a)
source='''#include <string>
#include <array>
#include <cassert>
#include <iostream>
constexpr int PIKA_GESTURES=8;
std::array<std::string,8> actions;bool pending=true,configured=true,explicitRefresh=false;
std::string pikaButtonAction(int slot){return actions[slot];}
bool pikaMailConfigured(){return configured;}
'''+s[a:b]+'''
int main(){
 assert(!pikaMailPending());
 for(int i=0;i<8;++i){actions[i]="mail";assert(pikaMailAssigned()&&!pikaMailPending());actions[i]="chat";assert(!pikaMailAssigned()&&!pikaMailPending());}
 pikaMailRequestRefresh();assert(pikaMailPending());configured=false;assert(!pikaMailPending());configured=true;pending=false;assert(!pikaMailPending());
 std::cout<<"Production Mail scheduling: only explicit entry, never wake or gesture assignment, configured and pending checks: PASS\\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.cpp').write_text(source,encoding='utf-8');exe=p/'mail-scheduling.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
