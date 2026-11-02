from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
weather=(root/'pika_note/src/app/weather.cpp').read_text()
a=weather.index('bool parseDays(');b=weather.index('\nvoid saveCache',a)
parse=weather[a:b]
calendar=(root/'pika_note/src/app/ui.cpp').read_text();a=calendar.index('void showMonthCalendar()');b=calendar.index('\nnamespace {',a);calendar=calendar[a:b]
stubs=r'''
#include "Arduino.h"
#include "date_validation.h"
#include "button_actions.h"
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 0
#define ARDUINOJSON_ENABLE_PROGMEM 0
#include <ArduinoJson.h>
#include <cmath>
#include <ctime>
#include <cassert>
#include <iostream>
#include <vector>
#include "weather.h"
struct Cell{int day,x,y,color;};std::vector<Cell> cells;int W=200,BLACK=0,WHITE=1;time_t forced;
time_t fakeTime(time_t*){return forced;}
struct tm* localtime_r(const time_t* input,struct tm* output){*output=*std::localtime(input);return output;}
void clearWhite(){cells.clear();}void drawHeader(const char*,int){}void labelLeft(int,const char*,int,int){}void drawFooter(const char*,const char*,int){}void refresh(){}void fillCircle(int,int,int,int){}
void drawStrC(int x,int y,const char* value,int,int color){if(y>=83&&value[0]>='0'&&value[0]<='9')cells.push_back({atoi(value),x,y,color});}
#define time fakeTime
'''
tests=r'''
#undef time
void month(int year,int month,int today,int count,int firstX){struct tm date={};date.tm_year=year-1900;date.tm_mon=month-1;date.tm_mday=today;date.tm_hour=12;date.tm_isdst=-1;forced=mktime(&date);showMonthCalendar();assert(cells.size()==count&&cells[0].x==firstX);for(auto c:cells){assert(c.y<=153&&c.x>=26&&c.x<=170);assert(c.color==(c.day==today?WHITE:BLACK));}}
int main(){
 for(int slot=0;slot<8;slot++){assert(pikaValidAction(slot,"calendar"));assert(pikaValidAction(slot,"weather"));}
 month(2024,2,15,29,26+3*24);month(2025,2,15,28,26+5*24);month(2025,6,15,30,26+6*24);
 JsonDocument doc;assert(!deserializeJson(doc,R"({"time":["2026-10-21","2026-10-22"],"temperature_2m_min":[-2,3],"temperature_2m_max":[9,11],"precipitation_sum":[0,4.5]})"));PikaWeatherDay days[2];assert(parseDays(doc.as<JsonVariantConst>(),days));assert(days[0].minimum==-2&&days[1].precipitation==4.5);
 JsonDocument hourly;assert(!deserializeJson(hourly,R"({"time":["2026-10-21T02:00","2026-10-21T14:00","2026-10-22T02:00","2026-10-22T14:00"],"temperature_2m":[4,12,5,14]})"));applyHourlyTemperatures(hourly.as<JsonVariantConst>(),days);assert(days[0].dayNightValid&&days[0].daytime==12&&days[0].nighttime==4&&days[1].daytime==14);hourly["temperature_2m"][3]=nullptr;applyHourlyTemperatures(hourly.as<JsonVariantConst>(),days);assert(!days[1].dayNightValid);
 doc["temperature_2m_min"][0]=nullptr;assert(!parseDays(doc.as<JsonVariantConst>(),days));doc["temperature_2m_min"][0]=-2;
 doc["time"][0]="2026-02-30";assert(!parseDays(doc.as<JsonVariantConst>(),days));doc["time"][0]="2026-10-21";
 doc["precipitation_sum"][1]=-1;assert(!parseDays(doc.as<JsonVariantConst>(),days));

 JsonDocument detail;auto current=detail["current"].to<JsonObject>();current["time"]="2026-10-21T21:15";current["temperature_2m"]=7.0;current["relative_humidity_2m"]=64;current["wind_speed_10m"]=3;current["wind_direction_10m"]=180;current["weather_code"]=3;current["is_day"]=0;
 const char* times[]={"2026-10-21T21:00","2026-10-21T22:00","2026-10-22T00:00","2026-10-22T03:00","2026-10-22T06:00","2026-10-22T09:00"};for(int i=0;i<6;i++){detail["hourly"]["time"][i]=times[i];detail["hourly"]["temperature_2m"][i]=i-2;detail["hourly"]["weather_code"][i]=61;detail["hourly"]["is_day"][i]=0;}
 PikaWeatherLocation location;assert(parseDetails(detail.as<JsonVariantConst>(),location));assert(location.current.valid&&location.current.humidity==64);assert(location.hours[0].time=="2026-10-22T00:00"&&location.hours[3].time=="2026-10-22T09:00");
 JsonDocument cached;storeDetails(cached.to<JsonObject>(),location);PikaWeatherLocation restored;assert(parseDetails(cached.as<JsonVariantConst>(),restored));assert(restored.hours[3].temperature==3&&restored.current.wind==3);
 detail["current"]["temperature_2m"]=nullptr;assert(!parseDetails(detail.as<JsonVariantConst>(),location));assert(location.current.temperature==7);detail["current"]["temperature_2m"]=7;detail["hourly"]["temperature_2m"][5]=nullptr;assert(!parseDetails(detail.as<JsonVariantConst>(),location));
 std::cout<<"Month screen: Monday-first, leap years, six rows, today highlight; weather: two valid days, null/invalid data rejected; assignable gestures: PASS\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'screens.cpp').write_text(stubs+calendar+"\n#undef time\n"+parse+tests)
 compiler=sys.argv[1:] or ['c++']
 subprocess.run(compiler+[str(p/'screens.cpp'),'-std=c++17','-I',str(root/'tools/tests/host'),'-I',str(root/'pika_note/src/app'),'-I',str(root/'.pio/libdeps/pika_note_2/ArduinoJson/src'),'-o',str(p/'screens.exe')],check=True)
 subprocess.run([str(p/'screens.exe')],check=True)
