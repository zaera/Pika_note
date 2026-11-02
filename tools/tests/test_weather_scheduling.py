from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
s=(root/'pika_note/src/app/weather.cpp').read_text(encoding='utf-8')
a=s.index('bool pikaWeatherAssigned(');b=s.index('const PikaWeatherLocation&',a)
code='''#include <string>
#include <array>
#include <cassert>
#include <iostream>
constexpr int PIKA_GESTURES=8;
std::array<std::string,8> actions;bool attempted=false,explicitRefresh=false,configured=true;
std::string pikaButtonAction(int slot){return actions[slot];}
bool pikaWeatherConfigured(){return configured;}
'''+s[a:b]+'''
int main(){
 assert(!pikaWeatherAssigned()&&!pikaWeatherPending());
 for(int i=0;i<8;++i){actions[i]="weather";assert(pikaWeatherAssigned()&&!pikaWeatherPending());actions[i]="tasks";assert(!pikaWeatherAssigned()&&!pikaWeatherPending());}
 actions[3]="weather";attempted=true;assert(!pikaWeatherPending());attempted=false;configured=false;assert(!pikaWeatherPending());
 configured=true;actions[3]="chat";assert(!pikaWeatherPending());pikaWeatherRequestRefresh();assert(pikaWeatherPending());attempted=true;assert(!pikaWeatherPending());pikaWeatherRequestRefresh();assert(pikaWeatherPending());
 configured=false;pikaWeatherRequestRefresh();assert(!pikaWeatherPending());
 std::cout<<"Production weather scheduling: all eight assigned gestures skip wake updates; every explicit entry refreshes; disabled weather stays idle: PASS\\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.cpp').write_text(code,encoding='utf-8');exe=p/'weather-policy.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
