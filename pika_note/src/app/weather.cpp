#include "error_log.h"
#include "phone_link.h"
#include "weather.h"
#include "deferred_http.h"
#include "device_extras.h"
#include "button_actions.h"
#include "notes.h"
#include "storage_commit.h"
#include "date_validation.h"
#include "../../config.h"
#include <Preferences.h>
#include <WiFi.h>

#include <HTTPClient.h>
#include <SD_MMC.h>
#include <cmath>
extern const uint8_t weather_ca_bundle_start[] asm("_binary_certs_weather_ca_bundle_pem_start");
namespace {
Preferences prefs;PikaWeatherLocation locations[1];bool attempted=false,explicitRefresh=false;
const char* cachePath=NOTES_DIR "/.weather-cache.json";
class WeatherBody:public Stream{
 public:String text;bool overflow=false;WeatherBody(){text.reserve(2048);}
 size_t write(uint8_t value)override{return write(&value,1);}
 size_t write(const uint8_t* data,size_t length)override{if(text.length()+length>16384){overflow=true;return 0;}return text.concat(reinterpret_cast<const char*>(data),length)?length:0;}
 int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}void flush()override{}
};
bool finiteCoordinate(double value,double limit){return std::isfinite(value)&&value>=-limit&&value<=limit;}
void loadConfig(JsonArrayConst values){auto value=values[0];if(!value["enabled"].as<bool>()&&values.size()>1&&values[1]["enabled"].as<bool>())value=values[1];locations[0].enabled=value["enabled"]|false;locations[0].name=value["name"]|"Location";locations[0].latitude=value["latitude"]|0.0;locations[0].longitude=value["longitude"]|0.0;}

bool parseDays(JsonVariantConst daily,PikaWeatherDay* days){
 for(int i=0;i<2;i++){
  const String date=daily["time"][i]|"";
  int year=0,month=0,day=0;
  if(date.length()!=10||sscanf(date.c_str(),"%d-%d-%d",&year,&month,&day)!=3||!validCalendarDate(year,month,day)||!daily["temperature_2m_min"][i].is<double>()||!daily["temperature_2m_max"][i].is<double>()||!daily["precipitation_sum"][i].is<double>())return false;
  const float low=daily["temperature_2m_min"][i].as<float>(),high=daily["temperature_2m_max"][i].as<float>(),rain=daily["precipitation_sum"][i].as<float>();
  if(!std::isfinite(low)||!std::isfinite(high)||!std::isfinite(rain)||low>high||low<-100||high>70||rain<0||rain>3000)return false;
  days[i]={date,low,high,rain,true};
  days[i].code=daily["weather_code"][i]|-1;
  if(daily["day_temperature"][i].is<double>()&&daily["night_temperature"][i].is<double>()){
    float dt=daily["day_temperature"][i].as<float>(),nt=daily["night_temperature"][i].as<float>();
    if(std::isfinite(dt)&&std::isfinite(nt)&&dt>=-100&&dt<=70&&nt>=-100&&nt<=70){days[i].daytime=dt;days[i].nighttime=nt;days[i].dayNightValid=true;}
  }
 }
 return days[0].date<days[1].date;
}
void applyHourlyTemperatures(JsonVariantConst hourly,PikaWeatherDay* days){
 for(int d=0;d<2;d++){bool foundDay=false,foundNight=false;for(size_t i=0;i<hourly["time"].size();i++){String stamp=hourly["time"][i]|"";if(!hourly["temperature_2m"][i].is<double>())continue;float value=hourly["temperature_2m"][i].as<float>();if(!std::isfinite(value)||value<-100||value>70)continue;if(stamp==days[d].date+"T14:00"){days[d].daytime=value;foundDay=true;}if(stamp==days[d].date+"T02:00"){days[d].nighttime=value;foundNight=true;}}days[d].dayNightValid=foundDay&&foundNight;}
}
bool parseDetails(JsonVariantConst data,PikaWeatherLocation& location){
 auto now=data["current"];
 if(!now["temperature_2m"].is<double>()||!now["relative_humidity_2m"].is<double>()||!now["wind_speed_10m"].is<double>()||!now["wind_direction_10m"].is<double>()||!now["weather_code"].is<int>()||!now["is_day"].is<int>())return false;
 PikaWeatherNow value;value.temperature=now["temperature_2m"].as<float>();value.humidity=now["relative_humidity_2m"].as<float>();value.wind=now["wind_speed_10m"].as<float>();value.direction=now["wind_direction_10m"].as<float>();value.code=now["weather_code"].as<int>();value.day=now["is_day"].as<int>()!=0;
 if(!std::isfinite(value.temperature)||value.temperature<-100||value.temperature>70||!std::isfinite(value.humidity)||value.humidity<0||value.humidity>100||!std::isfinite(value.wind)||value.wind<0||value.wind>150||!std::isfinite(value.direction)||value.direction<0||value.direction>360)return false;
 String stamp=now["time"]|"";if(stamp.length()<16)return false;value.time=stamp;
 PikaWeatherHour hours[4];int count=0;String previous;
 auto hourly=data["hourly"];
 for(size_t i=0;i<hourly["time"].size()&&count<4;i++){
  String time=hourly["time"][i]|"";if(time.length()!=16||!(stamp<time)||!(previous<time))continue;
  int hour=atoi(time.substring(11,13).c_str());if(hour%3)continue;
  if(!hourly["temperature_2m"][i].is<double>()||!hourly["weather_code"][i].is<int>()||!hourly["is_day"][i].is<int>())continue;
  float temp=hourly["temperature_2m"][i].as<float>();if(!std::isfinite(temp)||temp<-100||temp>70)continue;
  auto& point=hours[count++];point.time=time;point.temperature=temp;point.code=hourly["weather_code"][i].as<int>();point.day=hourly["is_day"][i].as<int>()!=0;point.valid=true;previous=time;
 }
 if(count!=4)return false;value.valid=true;location.current=value;for(int i=0;i<4;i++)location.hours[i]=hours[i];return true;
}
void storeDetails(JsonObject item,const PikaWeatherLocation& location){
 if(!location.current.valid)return;
 const auto& value=location.current;auto now=item["current"].to<JsonObject>();now["time"]=value.time;now["temperature_2m"]=value.temperature;now["relative_humidity_2m"]=value.humidity;now["wind_speed_10m"]=value.wind;now["wind_direction_10m"]=value.direction;now["weather_code"]=value.code;now["is_day"]=value.day?1:0;
 for(int i=0;i<4;i++){const auto& point=location.hours[i];item["hourly"]["time"][i]=point.time;item["hourly"]["temperature_2m"][i]=point.temperature;item["hourly"]["weather_code"][i]=point.code;item["hourly"]["is_day"][i]=point.day?1:0;}
}

void saveCache(){JsonDocument doc;for(int i=0;i<1;i++){const auto& location=locations[i];JsonObject item=doc["locations"][i].to<JsonObject>();item["latitude"]=location.latitude;item["longitude"]=location.longitude;item["updated"]=location.updated;storeDetails(item,location);for(int d=0;d<2;d++){const auto& day=location.days[d];if(!day.valid)continue;item["daily"]["time"][d]=day.date;item["daily"]["temperature_2m_min"][d]=day.minimum;item["daily"]["temperature_2m_max"][d]=day.maximum;item["daily"]["precipitation_sum"][d]=day.precipitation;item["daily"]["weather_code"][d]=day.code;if(day.dayNightValid){item["daily"]["day_temperature"][d]=day.daytime;item["daily"]["night_temperature"][d]=day.nighttime;}}}
 const String temp=String(cachePath)+".tmp";SD_MMC.remove(temp);File file=SD_MMC.open(temp,FILE_WRITE);if(!file)return;size_t count=serializeJson(doc,file);file.flush();bool ok=count==measureJson(doc);file.close();if(ok)commitStoredFile(SD_MMC,String(cachePath),temp);
}
}
void pikaWeatherBegin(){prefs.begin("pikaWeather",false);JsonDocument cfg;deserializeJson(cfg,prefs.getString("locations","[]"));loadConfig(cfg.as<JsonArrayConst>());
 recoverStoredFile(SD_MMC,String(cachePath));File file=SD_MMC.open(cachePath);if(!file)return;JsonDocument cache;const auto error=deserializeJson(cache,file);file.close();if(error)return;
 for(int i=0;i<2;i++){auto saved=cache["locations"][i];if(!saved["latitude"].is<double>()||!saved["longitude"].is<double>()||std::abs(saved["latitude"].as<double>()-locations[0].latitude)>0.000001||std::abs(saved["longitude"].as<double>()-locations[0].longitude)>0.000001)continue;PikaWeatherDay days[2];if(parseDays(saved["daily"],days)){for(int d=0;d<2;d++)locations[0].days[d]=days[d];locations[0].updated=saved["updated"]|"";parseDetails(saved,locations[0]);break;}}
}
bool pikaWeatherConfigured(){return locations[0].enabled;}
bool pikaWeatherAssigned(){for(int slot=0;slot<PIKA_GESTURES;++slot)if(pikaButtonAction(slot)=="weather")return true;return false;}
void pikaWeatherRequestRefresh(){attempted=false;explicitRefresh=true;}
bool pikaWeatherPending(){return !attempted&&pikaWeatherConfigured()&&explicitRefresh;}
const PikaWeatherLocation& pikaWeatherLocation(int index){return locations[0];}
void pikaWeatherSettingsJson(JsonObject settings){JsonArray out=settings["weatherLocations"].to<JsonArray>();for(const auto& location:locations){JsonObject item=out.add<JsonObject>();item["enabled"]=location.enabled;item["name"]=location.name;item["latitude"]=location.latitude;item["longitude"]=location.longitude;item["updated"]=location.updated;}}
bool pikaWeatherSave(JsonArrayConst input){if(input.size()!=1)return false;JsonDocument cfg;JsonArray out=cfg.to<JsonArray>();for(JsonObjectConst value:input){if(!value["enabled"].is<bool>()||!value["latitude"].is<double>()||!value["longitude"].is<double>())return false;double lat=value["latitude"].as<double>(),lon=value["longitude"].as<double>();String name=value["name"]|"";name.trim();if(!finiteCoordinate(lat,90)||!finiteCoordinate(lon,180)||name.length()>48)return false;JsonObject item=out.add<JsonObject>();item["enabled"]=value["enabled"].as<bool>();item["name"]=name.length()?name:String("Location ")+String(out.size());item["latitude"]=lat;item["longitude"]=lon;}
 String text;serializeJson(cfg,text);if(!prefs.putString("locations",text))return false;
 for(int i=0;i<1;i++)if(locations[i].latitude!=out[i]["latitude"].as<double>()||locations[i].longitude!=out[i]["longitude"].as<double>()){locations[i].updated="";locations[i].current={};for(auto& hour:locations[i].hours)hour={};for(auto& day:locations[i].days)day.valid=false;}
 loadConfig(out);attempted=false;return true;
}
namespace {
bool weatherPortalWaiting=false;String weatherPortalUrl;
String forecastUrl(const PikaWeatherLocation& location){
 return "https://api.open-meteo.com/v1/forecast?latitude="+String(location.latitude,6)+"&longitude="+String(location.longitude,6)+"&daily=temperature_2m_min,temperature_2m_max,precipitation_sum,weather_code&current=temperature_2m,relative_humidity_2m,weather_code,is_day,wind_speed_10m,wind_direction_10m&hourly=temperature_2m,weather_code,is_day&wind_speed_unit=ms&forecast_days=2&timezone=Europe%2FKyiv";
}
bool applyForecast(const String& body,PikaWeatherLocation& location){
 JsonDocument result;if(deserializeJson(result,body))return false;PikaWeatherDay days[2];
 if(!parseDays(result["daily"],days)||!parseDetails(result,location))return false;
 applyHourlyTemperatures(result["hourly"],days);
 for(int d=0;d<2;d++)location.days[d]=days[d];location.updated=currentUtcIso();return true;
}
}
bool pikaWeatherRefresh(){
 if(!pikaWeatherPending()||!pikaPhoneOnline()||time(nullptr)<1700000000)return false;
 attempted=true;bool changed=false;
 for(auto& location:locations){if(!location.enabled)continue;
  PhoneSecureClient client;client.setCACert(reinterpret_cast<const char*>(weather_ca_bundle_start));client.setHandshakeTimeout(5);HTTPClient http;
  if(!http.begin(client,forecastUrl(location)))continue;http.setConnectTimeout(4000);http.setTimeout(5000);http.addHeader("Accept-Encoding","identity");
  const int code=http.GET();if(code!=200||http.getSize()>16384){pikaError("weather","forecast","Forecast request failed or too large",code);http.end();continue;}
  WeatherBody body;const int received=http.writeToStream(&body);http.end();
  if(received>0&&!body.overflow&&applyForecast(body.text,location))changed=true;
 }
 if(changed)saveCache();
 return changed;
}
void pikaWeatherPortalTick(){
 if(weatherPortalWaiting){PikaDeferredResponse result;if(!pikaDeferredHttpPoll(2,result))return;
  weatherPortalWaiting=false;auto& location=locations[0];
  if(location.enabled&&forecastUrl(location)==weatherPortalUrl&&result.code==200&&applyForecast(result.body,location))saveCache();
  return;
 }
 if(!pikaWeatherPending()||!pikaPhoneOnline()||time(nullptr)<1700000000)return;
 const String url=forecastUrl(locations[0]);
 if(pikaDeferredHttpStart(2,url,"GET","","",true)){attempted=true;weatherPortalWaiting=true;weatherPortalUrl=url;}
}

void pikaWeatherPortalPause(){pikaDeferredHttpCancel(2);if(weatherPortalWaiting)attempted=false;weatherPortalWaiting=false;}
