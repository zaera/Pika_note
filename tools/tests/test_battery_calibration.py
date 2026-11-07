from pathlib import Path
import subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'pika_note/src/app/battery.cpp').read_text(encoding='utf-8')
source='\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
stub=r'''
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <cmath>
#define BAT_ADC_PIN 4
#define ADC_11db 0
struct BatteryReading{uint16_t millivolts=0;int percent=-1;bool valid=false;};
int sample=2050;float correction=1;bool durable=true;
void analogReadResolution(int){}void analogSetPinAttenuation(int,int){}void delay(int){}
int analogReadMilliVolts(int){return sample;}
float pikaBatteryCalibration(){return correction;}
bool pikaSaveBatteryCalibration(float factor){if(!durable||!(factor>=.9&&factor<=1.1))return false;correction=factor;return true;}
'''
main=r'''
int main(){batteryBegin();auto initial=readBattery();assert(initial.millivolts==4100&&initial.percent<100);assert(batteryCalibrateFull(false));assert(fabs(correction-4200.f/4100)<.0001);auto full=readBattery();assert(full.millivolts==4200&&full.percent==100);float first=correction;assert(batteryCalibrateFull(false)&&fabs(first-correction)<.0001);sample=1850;assert(!batteryCalibrateFull(false)&&correction==first);durable=false;assert(!batteryCalibrateFull(true)&&correction==first);durable=true;assert(batteryCalibrateFull(true)&&correction==1);assert(readBattery().millivolts==3700);sample=500;assert(!batteryCalibrateFull(false));}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.cpp').write_text(stub+source+main);exe=p/'battery.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
print('Battery calibration: full charge, repeat, percentage, low/invalid voltage, NVS failure, reset: PASS')
