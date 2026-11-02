from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2];s=(root/'pika_note/src/app/device_extras.cpp').read_text(encoding='utf-8')
methods='\n'.join(next(l for l in s.splitlines() if l.startswith(name)) for name in ('bool pikaSaveSoundSettings(', 'int pikaNotifyMelody(', 'bool pikaSaveNotifyMelodies(')).replace('String(i)','std::to_string(i)')
code=r"""
#include <string>
#include <cassert>
#include <iostream>
using String=std::string;
struct Pref{int writes=0;int putInt(const char*,int){writes++;return 4;}}preferences;
int melody=0,volume=60,notifyMelodies[7]={5,6,7,6,8,11,12};bool notify=true;
bool pikaSetPlaybackVolume(int v){volume=v;return true;}
"""+methods+r"""
int main(){assert(pikaSaveSoundSettings(-1,60)&&melody==-1);assert(pikaSaveSoundSettings(12,70)&&melody==12&&volume==70);assert(!pikaSaveSoundSettings(13,70));int silent[7]={-1,-1,-1,-1,-1,-1,-1};assert(pikaSaveNotifyMelodies(silent));for(int i=0;i<7;i++)assert(pikaNotifyMelody(i)==-1);assert(pikaNotifyMelody(-1)==-1&&pikaNotifyMelody(7)==-1);int bad[7]={5,6,13,6,8,11,12};int writes=preferences.writes;assert(!pikaSaveNotifyMelodies(bad)&&preferences.writes==writes);int active[7]={5,6,7,6,8,11,12};assert(pikaSaveNotifyMelodies(active));assert(pikaNotifyMelody(5)==11&&pikaNotifyMelody(6)==12);std::cout<<"Production sound choices: seven independent mutes, reminder mute, new patterns, bounds and persistence PASS\n";}
"""
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);f=p/'choices.cpp';f.write_text(code,encoding='utf-8');exe=p/'choices.exe';subprocess.run((sys.argv[1:] or ['c++'])+[str(f),'-std=c++17','-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
