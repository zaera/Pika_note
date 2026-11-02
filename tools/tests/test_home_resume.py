from pathlib import Path
import re,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
s=(root/'pika_note/src/app/ui.cpp').read_text(encoding='utf-8')
a=s.index('uint32_t homePixelsHash(');b=s.index('\n}',a)
helpers=s[a:b]
a=s.index('bool showCachedWakeHome(');b=s.index('\n}',a)+2
show=s[a:b]
s=(root/'pika_note/src/app/usb_backup.cpp').read_text(encoding='utf-8')
resume=re.search(r'bool pikaUsbBackupResume\(\).*?\n',s).group(0)
source=r'''#include <cstdint>
#include <cstring>
#include <ctime>
#include <cassert>
#include <iostream>
uint32_t sleepHomeMagic=0,sleepHomeHash=0,resumeMagic=0,resumeCheck=0;
uint8_t sleepHomePixels[5000]={};time_t sleepHomeTime=0;
constexpr uint32_t kResumeMagic=0x55534237;
struct Display{uint8_t pixels[5000]={};uint8_t* getBuffer(){return pixels;}}panel;Display* display=&panel;
int wake=0,reset=3,refreshed=0;constexpr int ESP_SLEEP_WAKEUP_EXT1=3,ESP_RST_DEEPSLEEP=8;
int esp_sleep_get_wakeup_cause(){return wake;}int esp_reset_reason(){return reset;}
void refreshHomeAsync(){refreshed++;}
'''+helpers+show+resume+r'''
int main(){assert(!showCachedWakeHome(false));for(unsigned i=0;i<5000;i++)panel.pixels[i]=(i*17)%251;rememberHomePixels();memset(panel.pixels,0,5000);assert(!showCachedWakeHome(false));assert(showCachedWakeHome(true));assert(refreshed==1);for(unsigned i=0;i<5000;i++)assert(panel.pixels[i]==(i*17)%251);sleepHomePixels[100]^=1;assert(!showCachedWakeHome(true));rememberHomePixels();wake=ESP_SLEEP_WAKEUP_EXT1;assert(showCachedWakeHome(false));sleepHomeMagic=0;assert(!showCachedWakeHome(true));resumeMagic=kResumeMagic;resumeCheck=~kResumeMagic;assert(pikaUsbBackupResume());assert(!pikaUsbBackupResume());resumeMagic=kResumeMagic;resumeCheck=123;assert(!pikaUsbBackupResume());resumeMagic=kResumeMagic;resumeCheck=~kResumeMagic;reset=ESP_RST_DEEPSLEEP;assert(!pikaUsbBackupResume());std::cout<<"Home resume: cached frame/hash, wake/reset gating, one-shot USB marker and corrupt RTC: PASS\n";}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(source)
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)
