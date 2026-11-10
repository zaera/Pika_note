from pathlib import Path
import re,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'pika_note/src/display/epaper_driver_bsp.cpp').read_text()
def method(name):
 a=s.index('void epaper_driver_display::'+name+'(');b=s.index('\n}',a)+2;return s[a:b]
source=r"""#include <cassert>
#include <cstdint>
#include <cstring>
using TickType_t=uint32_t;using gpio_num_t=int;uint32_t now=0,activated=0;bool pending=false;int previousPlaneWrites=0;uint8_t command=0,expectedOld=0x55,expectedNew=0xaa;
#define pdMS_TO_TICKS(n) (n)
#define ESP_LOGE(...) ((void)0)
uint32_t xTaskGetTickCount(){return now;}void vTaskDelay(uint32_t n){now+=n;}
int gpio_get_level(int){return pending&&now>=activated+5&&now<activated+100;}
void pikaError(const char*,const char*,const char*,int,int){}
struct epaper_driver_display{bool busyTimedOut=false;struct{int busy=1,buffer_len=5000;}lcd_spi_data;uint8_t pixels[5000]{};uint8_t* buffer=pixels;uint8_t previous[5000]{};uint8_t* previousBuffer=previous;bool previousValid=true;int Width=200,Height=200;void EPD_SetWindows(int,int,int,int){}void EPD_SetCursor(int,int){}bool EPD_DisplaySleepFrame(){return true;}void EPD_Init_Partial(){}void writeBytes(uint8_t* p,int n){for(int i=0;i<n;i++)assert(p[i]==(command==0x26?expectedOld:expectedNew));}void set_dc_0(){}void set_cs_0(){}void set_cs_1(){}void SPI_SendByte(uint8_t c){command=c;if(c==0x26)previousPlaneWrites++;if(c==0x20){activated=now;pending=true;}}void EPD_SendData(uint8_t){}void read_busy();void EPD_SendCommand(uint8_t);void EPD_TurnOnDisplay();void EPD_TurnOnDisplayPart();void EPD_DisplayPart();};
"""+'\n'.join(method(x) for x in ['read_busy','EPD_SendCommand','EPD_TurnOnDisplay','EPD_TurnOnDisplayPart','EPD_DisplayPart'])+r"""
int main(){epaper_driver_display p;p.EPD_TurnOnDisplay();assert(now>=100&&!p.busyTimedOut);uint32_t start=now;p.EPD_TurnOnDisplayPart();assert(now-start>=100&&!p.busyTimedOut);memset(p.pixels,expectedNew,5000);memset(p.previous,expectedOld,5000);p.EPD_DisplayPart();assert(previousPlaneWrites==1&&!memcmp(p.pixels,p.previous,5000));expectedOld=expectedNew;expectedNew=0xbb;memset(p.pixels,expectedNew,5000);p.EPD_DisplayPart();assert(previousPlaneWrites==2&&!memcmp(p.pixels,p.previous,5000));}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(source);exe=p/'panel.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
print('Panel activation waits for delayed BUSY before returning, full and partial: PASS')
