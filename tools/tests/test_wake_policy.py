from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
gcal=(root/'pika_note/src/app/google_calendar.cpp').read_text(encoding='utf-8');a=gcal.index('bool pikaGoogleHasPendingChanges(){');b=gcal.index('void pikaGoogleScheduleSync()',a);function=gcal[a:b].replace('.startsWith(','.starts_with(')
source=r'''
#include <string>
#include <vector>
#include <cassert>
#include <iostream>
using String=std::string;
#include <ArduinoJson.h>
#include "wake_policy.h"
bool enabled=true,importActive=false,verifyRequested=false;
String calendar="calendar",manifest="{\"calendar\":\"calendar\",\"items\":{}}";
struct Task{String id,type="event",text="Meeting",dueUtc="2026-10-23T10:00",endUtc="2026-10-23T11:00";bool done=false;};std::vector<Task> tasks;
bool pikaGoogleSyncEnabled(){return enabled;}String pikaGoogleCalendarId(){return calendar;}
bool readManifest(JsonDocument& d){return !deserializeJson(d,manifest);}
std::vector<Task> loadBrainTasks(){return tasks;}String importedPrefix(){return "import-";}
String eventId(const String& id){return id;}String eventPayload(const Task& t){return t.text+"|"+t.dueUtc+"|"+t.endUtc;}
String digest(const String& value){return value;}String managerGoogleBaseline(const String&){return tasks.empty()?"":eventPayload(tasks[0]);}
'''+function+r'''
int main(){
 const int64_t now=1792670400;
 assert(pikaDailyClockDue(now,0,0));assert(!pikaDailyClockDue(now,now-86399,0));assert(pikaDailyClockDue(now,now-86400,0));
 assert(!pikaDailyClockDue(now,now-172800,now-30));assert(pikaDailyClockDue(now,now-172800,now-86400));assert(pikaDailyClockDue(0,now,now));assert(pikaDailyClockDue(now,now+100,0));
 assert(!pikaGoogleHasPendingChanges());tasks.push_back({"e1"});assert(pikaGoogleHasPendingChanges());
 JsonDocument state;state["calendar"]=calendar;state["items"]["e1"]=eventPayload(tasks[0]);serializeJson(state,manifest);assert(!pikaGoogleHasPendingChanges());
 tasks[0].text="Changed";assert(pikaGoogleHasPendingChanges());tasks[0].text="Meeting";assert(!pikaGoogleHasPendingChanges());
 tasks[0].dueUtc="2026-10-24T10:00";assert(pikaGoogleHasPendingChanges());tasks[0].dueUtc="2026-10-23T10:00";
 tasks[0].done=true;assert(pikaGoogleHasPendingChanges());tasks.clear();assert(pikaGoogleHasPendingChanges());
 manifest="{\"calendar\":\"calendar\",\"items\":{}}";tasks.push_back({"import-remote"});assert(!pikaGoogleHasPendingChanges());
 tasks.clear();verifyRequested=true;assert(pikaGoogleHasPendingChanges());verifyRequested=false;enabled=false;assert(!pikaGoogleHasPendingChanges());importActive=true;assert(pikaGoogleHasPendingChanges());
 std::cout<<"Daily clock cadence/backward clock/offline attempt; Google only new/changed/deleted events, imports and explicit verification: PASS\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.cpp').write_text(source,encoding='utf-8');exe=p/'wake-policy.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++20','-I',str(root/'pika_note/src/app'),'-I',str(root/'.pio/libdeps/pika_note_2/ArduinoJson/src'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
