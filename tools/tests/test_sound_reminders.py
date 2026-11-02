from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
s=(root/'pika_note/src/app/device_extras.cpp').read_text(encoding='utf-8')
def line(name):return next(x for x in s.splitlines() if x.startswith(name))
code=r'''
#include <cassert>
#include <ctime>
#include <cstdio>
#include <string>
#include <vector>
#include <set>
#include <iostream>
using String=std::string;
struct BrainTaskRecord{String id,type,dueUtc,datePrecision;bool done=false,archived=false;};
std::vector<BrainTaskRecord> tasks;std::set<String> ack;bool alerts=true;int leadMinutes=30;time_t clockNow=1800000000;
time_t fakeTime(time_t*){return clockNow;}
#define time fakeTime
time_t utcTmToEpoch(tm& v){return _mkgmtime(&v);}
bool seen(const BrainTaskRecord& v){return ack.count(v.id);}
const std::vector<BrainTaskRecord>& loadBrainTasks(){return tasks;}
'''+line('time_t eventTime(')+'\n'+line('bool pikaFindDueAlert(')+'\n'+line('uint64_t pikaNextAlertWakeUs(')+r'''
String iso(time_t t){tm v;gmtime_s(&v,&t);char s[32];strftime(s,sizeof(s),"%Y-%m-%dT%H:%M:%SZ",&v);return s;}
int main(){BrainTaskRecord out;tasks.push_back({"1","event",iso(clockNow+3600),"minute"});assert(pikaNextAlertWakeUs()==1800ULL*1000000);assert(!pikaFindDueAlert(out));clockNow+=1801;assert(pikaFindDueAlert(out)&&out.id=="1");ack.insert("1");assert(!pikaFindDueAlert(out)&&pikaNextAlertWakeUs()==0);ack.clear();tasks[0].archived=true;assert(!pikaFindDueAlert(out)&&!pikaNextAlertWakeUs());tasks[0].archived=false;tasks[0].datePrecision="day";assert(!pikaNextAlertWakeUs());tasks[0].datePrecision="minute";tasks[0].done=true;assert(!pikaNextAlertWakeUs());tasks[0].done=false;alerts=false;assert(!pikaFindDueAlert(out)&&!pikaNextAlertWakeUs());alerts=true;clockNow+=4000;assert(!pikaFindDueAlert(out)&&!pikaNextAlertWakeUs());std::cout<<"Production reminder scheduling: lead, sleep wake, acknowledgement, archived/done/all-day exclusion: PASS\n";}
'''
audio=(root/'pika_note/src/app/device_sounds.cpp').read_text(encoding='utf-8');audio='\n'.join(x for x in audio.splitlines() if not x.startswith('#include'))
pre=r'''
#include <cassert>
#include <atomic>
#include <vector>
#include <cstdio>
#include <iostream>
struct SerialMock{template<class... T>void printf(const char*,T...) {}}Serial;
bool sound=true,notify=true;int volume=0,closed=0,silenceBytes=0,selected[7]={5,6,7,6,8,11,12};std::vector<int> tones;
bool pikaSoundIsEnabled(){return sound;}void pikaSoundSetEnabled(bool v){sound=v;}
void audio_playback_set_vol(int v){volume=v;}void audio_idle_suspend(){assert(silenceBytes>=12288);silenceBytes=0;closed++;}
int audio_playback_write(void*,unsigned len){silenceBytes+=len;return 0;}
void playToneUI(float hz,int ms,float gain){assert(volume==70);tones.push_back((int)hz);}
void delay(int){}int pikaAlertMelody(){return 0;}int pikaPlaybackVolume(){return 70;}
bool pikaNotifyEnabled(){return notify;}int pikaNotifyMelody(int k){return k<5&&!notify?-1:selected[k];}
void pikaPlayNotify(int);
void pikaError(const char*,const char*,const char*){}
'''
post=r'''
int main(){pikaQueueSoundTest(10,70);assert(tones.empty());pikaServiceSoundTest();assert(tones.size()==2&&closed==1&&volume==0);tones.clear();notify=false;pikaPlayNotify(0);assert(tones.empty());notify=true;pikaPlayNotify(0);assert(tones.size()==1&&tones[0]==880);tones.clear();pikaPlayNotify(2);assert(tones.size()==2&&tones[1]==294);tones.clear();selected[3]=-1;pikaPlayNotify(3);assert(tones.empty());pikaQueueMailNotify();pikaQueueMailNotify();pikaServiceSoundTest();assert(tones.size()==2);pikaServiceSoundTest();assert(tones.size()==2);tones.clear();pikaPlayNotify(5);assert(tones.size()==2&&tones[0]==784);tones.clear();pikaPlayNotify(6);assert(tones.size()==2&&tones[1]==440);tones.clear();selected[5]=-1;pikaPlayNotify(5);assert(tones.empty());selected[6]=-1;pikaPlayNotify(6);assert(tones.empty());pikaQueueSoundTest(-1,70);pikaServiceSoundTest();assert(tones.empty());std::cout<<"Production sound queue: test patterns, notifications, mute, mail coalescing and codec close: PASS\n";}
'''
with tempfile.TemporaryDirectory() as tmp:
 for name,src in [('reminders',code),('sound',pre+audio+post)]:
  p=Path(tmp);f=p/(name+'.cpp');f.write_text(src,encoding='utf-8');exe=p/(name+'.exe');subprocess.run((sys.argv[1:] or ['c++'])+[str(f),'-std=c++17','-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
ino=(root/'pika_note/pika_note.ino').read_text(encoding='utf-8');assert 'pikaServiceSoundTest();' in ino[ino.index('void loop()'):];assert 'pikaPlayAlertMelody();' in ino[ino.index('void runEventReminder('):ino.index('void handleClockMode()')]
print('BLE owner loop services queued sound; reminder loop plays selected melody: PASS')
