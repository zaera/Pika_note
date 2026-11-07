#include "device_extras.h"
#include "rtc.h"
#include "button_actions.h"
#include "settings.h"
#include <Preferences.h>
#include <esp_attr.h>
#include <ctime>
namespace {
RTC_DATA_ATTR bool clockMode=false;
Preferences preferences;
bool strictCapture=false,voiceRecordActions=false;
int sleepMinutes=1;float batteryCalibration=1.0f;
bool vibrationEnabled=false;int vibrationPatterns[6]={1,2,9,1,1,3};
bool alerts=true;int leadMinutes=30,durationSeconds=60;
bool notify=false;int notifyMelodies[7]={5,6,7,6,8,11,12};int melody=0;String buttonActions[PIKA_GESTURES];
uint64_t acknowledged[32]={};unsigned ackCursor=0;
uint64_t token(const BrainTaskRecord& task){String key=task.id+"|"+task.dueUtc;uint64_t hash=1469598103934665603ULL;for(size_t i=0;i<key.length();i++){hash^=(uint8_t)key[i];hash*=1099511628211ULL;}return hash;}
bool seen(const BrainTaskRecord& task){const auto hash=token(task);for(auto value:acknowledged)if(value==hash)return true;return false;}
time_t eventTime(const BrainTaskRecord& task){if(task.type!="event"||task.done||task.archived||task.datePrecision=="day"||task.dueUtc.length()<19)return 0;struct tm value={};int y,m,d,h,n,s;if(sscanf(task.dueUtc.c_str(),"%d-%d-%dT%d:%d:%d",&y,&m,&d,&h,&n,&s)!=6)return 0;value.tm_year=y-1900;value.tm_mon=m-1;value.tm_mday=d;value.tm_hour=h;value.tm_min=n;value.tm_sec=s;return utcTmToEpoch(value);}
}
void pikaExtrasBegin(){preferences.begin("pikaExtras",false);
 batteryCalibration=preferences.getFloat("batteryCal",1.0f);if(!(batteryCalibration>=0.90f&&batteryCalibration<=1.10f))batteryCalibration=1.0f;
 sleepMinutes=preferences.getInt("sleepMinutes",1);if(sleepMinutes<1||sleepMinutes>5)sleepMinutes=1;
 strictCapture=preferences.getBool("strictCapture",false);
 voiceRecordActions=preferences.getBool("voiceActions",false);
 {String packed=preferences.getString("vibration","");int enabled,values[6];if(sscanf(packed.c_str(),"%d,%d,%d,%d,%d,%d,%d",&enabled,&values[0],&values[1],&values[2],&values[3],&values[4],&values[5])==7){bool valid=enabled==0||enabled==1;for(int value:values)valid=valid&&value>=0&&value<=10;if(valid){vibrationEnabled=enabled;for(int i=0;i<6;i++)vibrationPatterns[i]=values[i];}}}
 pikaVibrationBegin();
 notify=preferences.getBool("notify",false);
 for(int i=0;i<7;i++){String key="notify"+String(i);int value=preferences.getInt(key.c_str(),notifyMelodies[i]);if(value>=-1&&value<13)notifyMelodies[i]=value;}
 // Upgrade the old master switch to independent choices. Preserve muted
 // legacy cues; the newly requested error/wake/sleep signals have own choices.
 if(!preferences.getBool("cueChoicesV2",false)){
   if(!notify)for(int i=0;i<5;i++)if(i!=2)notifyMelodies[i]=-1;
   for(int i=0;i<7;i++){String key="notify"+String(i);preferences.putInt(key.c_str(),notifyMelodies[i]);}
   notify=true;preferences.putBool("notify",true);preferences.putBool("cueChoicesV2",true);
 }
 melody=preferences.getInt("melody",0);if(melody<-1||melody>12)melody=0;
 for(int i=0;i<PIKA_GESTURES;i++){char key[12];snprintf(key,sizeof(key),"button%d",i);String action=preferences.getString(key,pikaDefaultAction(i));if(action=="chat")action="none";buttonActions[i]=pikaValidAction(i,action)?action:String(pikaDefaultAction(i));}
const String packed=preferences.getString("buttonMap","");
 if(packed.length()){
   String loaded[PIKA_GESTURES];int start=0;bool valid=true;
   for(int i=0;i<PIKA_GESTURES;i++){int end=packed.indexOf(',',start);if(i==PIKA_GESTURES-1)end=packed.length();if(end<0){valid=false;break;}loaded[i]=packed.substring(start,end);if(loaded[i]=="chat")loaded[i]="none";valid=valid&&pikaValidAction(i,loaded[i]);start=end+1;}
   if(valid)for(int i=0;i<PIKA_GESTURES;i++)buttonActions[i]=loaded[i];
 }
 alerts=preferences.getBool("alerts",true);leadMinutes=preferences.getInt("lead",30);durationSeconds=preferences.getInt("duration",60);ackCursor=preferences.getUInt("ackCursor",0)%32;for(int i=0;i<32;i++){char key[12];snprintf(key,sizeof(key),"ack%d",i);acknowledged[i]=preferences.getULong64(key,0);}}
bool pikaAlertEnabled(){return alerts;}
int pikaAlertLeadMinutes(){return leadMinutes;}
int pikaAlertDurationSeconds(){return durationSeconds;}
bool pikaSaveAlertSettings(bool enabled,int lead,int duration){if(lead<1||lead>180||duration<10||duration>300)return false;const bool ok=preferences.putBool("alerts",enabled)&&preferences.putInt("lead",lead)&&preferences.putInt("duration",duration);if(ok){alerts=enabled;leadMinutes=lead;durationSeconds=duration;}return ok;}
bool pikaFindDueAlert(BrainTaskRecord& out){if(!alerts||time(nullptr)<1700000000)return false;const time_t now=time(nullptr);time_t earliest=0;for(const auto& task:loadBrainTasks()){const time_t due=eventTime(task);if(due>now&&due-now<=leadMinutes*60&&!seen(task)&&(!earliest||due<earliest)){out=task;earliest=due;}}return earliest>0;}
void pikaMarkAlert(const BrainTaskRecord& task){acknowledged[ackCursor]=token(task);char key[12];snprintf(key,sizeof(key),"ack%d",ackCursor);preferences.putULong64(key,acknowledged[ackCursor]);ackCursor=(ackCursor+1)%32;preferences.putUInt("ackCursor",ackCursor);}
uint64_t pikaNextAlertWakeUs(){if(!alerts||time(nullptr)<1700000000)return 0;const time_t now=time(nullptr);time_t next=0;for(const auto& task:loadBrainTasks()){const time_t due=eventTime(task);if(due<=now||seen(task))continue;const time_t target=due-leadMinutes*60;const time_t delta=target>now?target-now:1;if(!next||delta<next)next=delta;}return next?(uint64_t)next*1000000ULL:0;}
bool pikaClockMode(){return clockMode;}
void pikaSetClockMode(bool enabled){clockMode=enabled;}

int pikaAlertMelody(){return melody;}
bool pikaSaveSoundSettings(int selected,int volume){if(selected<-1||selected>12||volume<20||volume>100)return false;if(!preferences.putInt("melody",selected)||!pikaSetPlaybackVolume(volume))return false;melody=selected;return true;}
String pikaButtonAction(int slot){return slot>=0&&slot<PIKA_GESTURES?buttonActions[slot]:String("none");}
bool pikaSaveButtonActions(const String* actions){
 for(int i=0;i<PIKA_GESTURES;i++)if(!pikaValidAction(i,actions[i]))return false;
 // One string commit prevents a partial gesture map on interrupted settings save.
 String packed;for(int i=0;i<PIKA_GESTURES;i++){if(i)packed+=',';packed+=actions[i];}
 if(!preferences.putString("buttonMap",packed))return false;
 for(int i=0;i<PIKA_GESTURES;i++)buttonActions[i]=actions[i];return true;
}

bool pikaStrictCapture(){return strictCapture;}
bool pikaSaveStrictCapture(bool enabled){if(!preferences.putBool("strictCapture",enabled))return false;strictCapture=enabled;return true;}

bool pikaVoiceRecordActions(){return voiceRecordActions;}
bool pikaSaveVoiceRecordActions(bool enabled){if(!preferences.putBool("voiceActions",enabled))return false;voiceRecordActions=enabled;return true;}

bool pikaNotifyEnabled(){return notify;}
bool pikaSaveNotify(bool enabled){if(!preferences.putBool("notify",enabled))return false;notify=enabled;return true;}

int pikaNotifyMelody(int kind){return kind>=0&&kind<7?((kind<5&&!notify)?-1:notifyMelodies[kind]):-1;}
bool pikaSaveNotifyMelodies(const int* values){for(int i=0;i<7;i++)if(values[i]<-1||values[i]>=13)return false;for(int i=0;i<7;i++){String key="notify"+String(i);if(!preferences.putInt(key.c_str(),values[i]))return false;notifyMelodies[i]=values[i];}return true;}


bool pikaVibrationEnabled(){return vibrationEnabled;}
int pikaVibrationPattern(int kind){return kind>=0&&kind<6?vibrationPatterns[kind]:0;}
bool pikaSaveVibration(bool enabled,const int* values){
 for(int i=0;i<6;i++)if(values[i]<0||values[i]>10)return false;
 String packed=enabled?"1":"0";for(int i=0;i<6;i++)packed+=","+String(values[i]);
 if(preferences.putString("vibration",packed)!=packed.length())return false;
 vibrationEnabled=enabled;for(int i=0;i<6;i++)vibrationPatterns[i]=values[i];return true;
}

int pikaSleepMinutes(){return sleepMinutes;}
uint32_t pikaSleepIdleMs(){return uint32_t(sleepMinutes)*60000UL;}
bool pikaSaveSleepMinutes(int minutes){if(minutes<1||minutes>5)return false;if(preferences.putInt("sleepMinutes",minutes)!=sizeof(int))return false;sleepMinutes=minutes;return true;}

float pikaBatteryCalibration(){return batteryCalibration;}
bool pikaSaveBatteryCalibration(float factor){if(!(factor>=0.90f&&factor<=1.10f)||preferences.putFloat("batteryCal",factor)!=sizeof(float))return false;batteryCalibration=factor;return true;}
