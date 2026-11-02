from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
sleep=(root/'pika_note/src/app/sleep.cpp').read_text()
start=sleep.index('void releasePeripheralPowerHolds()');end=sleep.index('\nvoid enterUltraSleep()',start)
release=sleep[start:end]
stubs=r'''
#include <cassert>
#include <iostream>
using gpio_num_t=int;
const int VBAT_PWR_PIN=17,BTN_PWR=18,EPD_PWR_PIN=1,Audio_PWR_PIN=2;
bool held=true;int latch=0;unsigned writes=0;
void gpio_set_level(int pin,int value){if(pin==17){latch=value;++writes;}}
void gpio_deep_sleep_hold_dis(){}
void gpio_hold_dis(int pin){if(pin==17){held=false;assert(latch==1);}}
void rtc_gpio_hold_dis(int){}
void rtc_gpio_deinit(int){}
'''
main=r'''
int main(){releasePeripheralPowerHolds();assert(!held&&latch==1&&writes>=2);std::cout<<"Battery latch is HIGH before releasing GPIO hold: PASS\n";}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'power.cpp').write_text(stubs+release+main)
 compiler=sys.argv[1:] or ['c++']
 subprocess.run(compiler+[str(p/'power.cpp'),'-std=c++17','-o',str(p/'power.exe')],check=True)
 subprocess.run([str(p/'power.exe')],check=True)
board=(root/'pika_note/src/power/board_power_bsp.cpp').read_text()
assert board.index('gpio_set_level(static_cast<gpio_num_t>(vbat_power_pin),1)')<board.index('gpio_config(&gpio_conf)')
ino=(root/'pika_note/pika_note.ino').read_text();setup=ino[ino.index('void setup()'):]
assert setup.index('board.VBAT_POWER_ON()')<setup.index('pikaSettingsBegin()')
print('Constructor preloads battery output; setup powers latch before NVS: PASS')
