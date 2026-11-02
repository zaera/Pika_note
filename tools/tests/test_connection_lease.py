from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2];s=(root/'pika_note/pika_note.ino').read_text(encoding='utf-8');a=s.index('void stopWifi(bool');b=s.index('\nString localDayKey()',a)
source=r"""
#include <cstdint>
#include <cassert>
#include <cstdio>
uint32_t now=100,wifiLeaseEndsAt=0;uint32_t millis(){return now;}
bool background=false;bool pikaBackgroundNetworkMode(){return background;}
constexpr int WL_CONNECTED=3,WIFI_OFF=0;enum State{STATE_IDLE,STATE_CALENDAR,STATE_WEATHER,STATE_MAIL,STATE_ASKING};State state=STATE_IDLE;
struct {int connected=WL_CONNECTED,stops=0;int status(){return connected;}void disconnect(bool){++stops;connected=0;}void mode(int){}void setSleep(bool){}} WiFi;
"""+s[a:b]+r"""
int main(){
 stopWifi();assert(WiFi.stops==0&&wifiLeaseEndsAt==30100);now=30099;serviceWifiLease();assert(WiFi.stops==0);++now;serviceWifiLease();assert(WiFi.stops==1);
 WiFi.connected=WL_CONNECTED;background=true;stopWifi();assert(WiFi.stops==2&&wifiLeaseEndsAt==0);background=false;
 WiFi.connected=WL_CONNECTED;stopWifi(true);assert(WiFi.stops==3);
 WiFi.connected=WL_CONNECTED;stopWifi();now+=30001;state=STATE_ASKING;serviceWifiLease();assert(WiFi.stops==3);state=STATE_IDLE;serviceWifiLease();assert(WiFi.stops==4);
 WiFi.connected=WL_CONNECTED;now=0xfffff000;stopWifi();now+=30001;serviceWifiLease();assert(WiFi.stops==5);
 puts("Wi-Fi lease: reuse, expiry, hard/background shutdown, no interruption during Ask and timer wrap: PASS");
}
"""
with tempfile.TemporaryDirectory() as directory:
 d=Path(directory);(d/'test.cpp').write_text(source,encoding='utf-8');exe=d/'lease.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(d/'test.cpp'),'-std=c++17','-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
