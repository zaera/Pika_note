from pathlib import Path
import runpy,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2]
base=runpy.run_path(str(root/'tools/tests/test_manager_store.py'))
prefix=base['prefix'].replace(' bool available(){'," String readStringUntil(char stop){String out;while(available()){char c=char(read());if(c==stop)break;out+=c;}return out;}\n size_t print(char c){return write(uint8_t(c));}\n bool available(){").replace('#define FILE_WRITE 1','#define FILE_WRITE 1\n#define FILE_APPEND 2').replace('if(mode)files[key]=std::make_shared<std::string>();','if(mode==FILE_WRITE||(mode==FILE_APPEND&&!files.count(key)))files[key]=std::make_shared<std::string>();')
stubs=r'''
#include "brain.h"
#include "voice_command_policy.h"
#include <cmath>
#include <deque>
struct {template<class... Args>void printf(const char*,Args...) {} template<class T>void print(const T&){} template<class T>void println(const T&){} } Serial;
int stageDepth=0,stageCalls=0;std::vector<std::string> progress;
bool pikaRunAskStage(void (*callback)(void*),void* context){assert(stageDepth==0);++stageDepth;++stageCalls;callback(context);--stageDepth;return true;}
void showAskActionProgress(const char* phase,const char* kind,int done,int total){assert(stageDepth==1);assert(done>=0&&done<=total);progress.push_back(std::string(phase)+" "+kind+" "+std::to_string(done)+"/"+std::to_string(total));}
String pikaAskContextPath(){return "/notes/.ask-context.json";}void pikaAskAppendHistory(const String&,const String&){}
namespace {bool askReadSession(JsonDocument& d){d["conversation"]="fixture";return true;}bool askSaveSession(JsonDocument&){return true;}}
bool allowed=true,changes=true;bool pikaVoiceMutationContext(){return allowed;}bool pikaVoiceRecordActions(){return changes;}
BrainMemory loadBrainMemory(int n){BrainMemory m;m.noteNum=n;m.title="Renovation";return m;}
String noteCreatedUtc(int){return "2026-10-20T10:00:00Z";}
String shiftedLocalDate(const String&,int){return "2026-10-21";}
String readSmallFile(const String& path,size_t){return files.count(path.c_str())?String(*files[path.c_str()]):String();}
std::deque<String> replies;std::vector<String> inputs;
bool openAiJsonPost(const char*,const String& payload,String& response,uint16_t=0,String* failure=nullptr){JsonDocument q;deserializeJson(q,payload);inputs.push_back(q["input"].as<String>());if(replies.empty())return false;JsonDocument e;e["status"]="completed";e["result"]=replies.front();replies.pop_front();serializeJson(e,response);return true;}
String responseOutputText(const String& response){JsonDocument e;deserializeJson(e,response);return e["result"].as<String>();}
bool pikaChatAnswer(const String&,String& answer){answer="General answer";return true;}
static void invalidatePortalCache(){}void pikaGoogleScheduleSync(){}bool pikaGoogleSyncEnabled(){return false;}void pikaGoogleSyncBatch(){assert(false && "Calendar TLS must run after Ask feedback, outside the executor");}
void route(const char* intent,const char* operation,const char* kind,const char* command){JsonDocument r;r["intent"]=intent;r["operation"]=operation;r["kind"]=kind;r["commandQuote"]=command;r["confidence"]=1;String out;serializeJson(r,out);replies.push_back(out);}
JsonDocument plan(){JsonDocument p;p["status"]="ready";p["operations"].to<JsonArray>();return p;}
JsonObject operation(JsonDocument& p,const char* kind,const char* action,const String& id,const char* title,const char* date="",const char* end=""){auto o=p["operations"].add<JsonObject>();o["kind"]=kind;o["operation"]=action;o["id"]=id;o["text"]=title;o["date"]=date;o["end"]=end;o["allDay"]=false;o["priority"]="normal";o["confidence"]=1;o["fields"].to<JsonArray>();return o;}
void respond(JsonDocument& p){String out;serializeJson(p,out);replies.push_back(out);}
'''
main=r'''
int main(){
 String answer,id,error;std::vector<int> sources;JsonDocument memory;memory["kind"]="memories";memory["record"]["text"]="Repair roof. Buy paint.";memory["record"]["date"]="2026-10-20T10:00";assert(managerMutation(memory.as<JsonVariantConst>(),id,error));
 // Upper recording can never run this companion, even with queued model actions.
 allowed=false;const auto initial=managerRevision();assert(!applyAskAssistant("Сделай задачу",answer,sources));assert(managerRevision()==initial&&inputs.empty());allowed=true;
 // A semantic query is read-only and its action planner is never called.
 route("query","none","auto","Какие заметки?");assert(!applyAskAssistant("Какие заметки?",answer,sources));assert(managerRevision()==initial);
 // USB dry-run validates real actions but never saves them.
 pikaAskSetDryRun(true);route("create","create","tasks","Create a test task");auto dry=plan();operation(dry,"tasks","create","","Test task");respond(dry);assert(applyAskAssistant("Create a test task",answer,sources));assert(managerRevision()==initial);pikaAskSetDryRun(false);
 const int stagesBefore=stageCalls;
 // Planning creates two durable tasks from one unchanged source note.
 route("plan","create","tasks","Разбери заметку в задачи");auto p=plan();for(const char* title:{"Repair roof","Buy paint"}){auto op=operation(p,"tasks","create","",title);op["sourceId"]="m1";op["evidence"]=title;}respond(p);
 assert(applyAskAssistant("Разбери заметку в задачи",answer,sources));auto tasks=loadBrainTasks();assert(tasks.size()==2&&tasks[0].noteNum==1&&tasks[1].noteNum==1);assert(managerReadText(1)=="Repair roof. Buy paint."&&!managerArchived(1));assert(sources.size()==1&&sources[0]==1);
 assert(stageCalls==stagesBefore+3); // planner returns before each separate saving job
 assert(progress.size()==4&&progress.front()=="CREATING TASKS 0/2"&&progress.back()=="CREATING TASKS 2/2");
 // Complete and restore by a selected real ID with verified evidence.
 const String target=tasks[0].id;route("change","archive","tasks","Пометь ремонт выполненным");p=plan();auto op=operation(p,"tasks","archive",target,"Repair roof");op["evidence"]="Repair roof";respond(p);assert(applyAskAssistant("Пометь ремонт выполненным",answer,sources));assert(loadBrainTasks()[0].done);
 route("change","restore","tasks","Верни ремонт в актуальные");p=plan();op=operation(p,"tasks","restore",target,"Repair roof");op["evidence"]="Repair roof";respond(p);assert(applyAskAssistant("Верни ремонт в актуальные",answer,sources));assert(!loadBrainTasks()[0].done);
 // Multiple changes require explicit batch authorization from the semantic router.
 const auto beforeAmbiguous=managerRevision();route("change","archive","tasks","Complete the task");p=plan();for(const auto& t:loadBrainTasks()){auto item=operation(p,"tasks","archive",t.id,t.text.c_str());item["evidence"]=t.text;}respond(p);assert(applyAskAssistant("Complete the task",answer,sources));assert(managerRevision()==beforeAmbiguous);
 // Invalid dates in ANY generated item prevent the whole plan from starting.
 const auto beforeBad=managerRevision();route("create","create","events","Create events");p=plan();operation(p,"events","create","","Valid","2026-10-22T12:00","2026-10-22T13:00");operation(p,"events","create","","Invalid","2026-02-31T12:00","2026-02-31T13:00");respond(p);assert(applyAskAssistant("Create events",answer,sources));assert(managerRevision()==beforeBad);
 // An invented ID/evidence never writes anything.
 route("change","archive","tasks","Complete a task");p=plan();op=operation(p,"tasks","archive","invented","Repair roof");op["evidence"]="Repair roof";respond(p);assert(applyAskAssistant("Complete a task",answer,sources));assert(managerRevision()==beforeBad);
 // Archive isn't exposed to the planner for an ordinary edit/create query.
 JsonDocument hide;hide["kind"]="memories";hide["id"]="m1";hide["action"]="delete";assert(managerMutation(hide.as<JsonVariantConst>(),id,error));route("plan","create","tasks","Plan tasks from notes");p=plan();p["status"]="not_found";p["answer"]="No source";respond(p);assert(applyAskAssistant("Plan tasks from notes",answer,sources));JsonDocument input;assert(!deserializeJson(input,inputs.back()));assert(input["records"].size()==0);
 // A past event cannot be restored with its past date even if the model suggests it.
 JsonDocument event;event["kind"]="events";event["record"]["text"]="Old meeting";event["record"]["date"]="2026-10-18T12:00";event["record"]["end"]="2026-10-18T13:00";assert(managerMutation(event.as<JsonVariantConst>(),id,error));const String eventId=id;const auto beforeRestore=managerRevision();route("change","restore","events","Restore the meeting");p=plan();op=operation(p,"events","restore",eventId,"Old meeting","2026-10-18T12:00","2026-10-18T13:00");op["evidence"]="Old meeting";respond(p);assert(applyAskAssistant("Restore the meeting",answer,sources));assert(managerRevision()==beforeRestore);
 // With an explicit new date it returns to active without changing its ID.
 route("change","restore","events","Restore the meeting tomorrow");p=plan();op=operation(p,"events","restore",eventId,"Old meeting","2026-10-21T12:00","2026-10-21T13:00");op["evidence"]="Old meeting";op["fields"].add("date");op["fields"].add("end");respond(p);assert(applyAskAssistant("Restore the meeting tomorrow",answer,sources));tasks=loadBrainTasks();auto restored=std::find_if(tasks.begin(),tasks.end(),[&](const BrainTaskRecord& t){return t.id==eventId;});assert(restored!=tasks.end()&&!restored->archived&&restored->dueUtc=="2026-10-21T12:00:00Z");
 // Source notes must also be supplied when the router labels a source-based request create.
 hide["action"]="remember";assert(managerMutation(hide.as<JsonVariantConst>(),id,error));
 route("create","create","tasks","Create tasks from my note");p=plan();op=operation(p,"tasks","create","","Buy paint");op["sourceId"]="m1";op["evidence"]="Buy paint";respond(p);const auto beforeSource=loadBrainTasks().size();assert(applyAskAssistant("Create tasks from my note",answer,sources));assert(loadBrainTasks().size()==beforeSource+1&&loadBrainTasks().back().noteNum==1);
 // Regression: router mistakes the SOURCE memory for the creation target kind.
 route("create","create","memories","Create tasks from my only note");p=plan();op=operation(p,"tasks","create","","Repair roof");op["sourceId"]="m1";op["evidence"]="Repair roof";respond(p);const auto beforeMislabel=loadBrainTasks().size();assert(applyAskAssistant("Create tasks from my only note",answer,sources));assert(loadBrainTasks().size()==beforeMislabel+1);
 // Voice-only disambiguation: clarification writes nothing; a short follow-up
 // selects a real source while preserving the pending creation operation.
 route("plan","create","tasks","Create tasks from my note");p=plan();p["status"]="clarify";p["answer"]="Про ремонт крыши или про ремонт кухни?";respond(p);const auto beforeClarify=managerRevision();assert(applyAskAssistant("Create tasks from my note",answer,sources));assert(managerRevision()==beforeClarify);
 pikaRememberAskTurn("Create tasks from my note",answer);
 route("plan","create","tasks","Про крышу");p=plan();op=operation(p,"tasks","create","","Repair roof");op["sourceId"]="m1";op["evidence"]="Repair roof";respond(p);assert(applyAskAssistant("Про крышу",answer,sources));assert(managerRevision()>beforeClarify);assert(inputs.back().indexOf("Create tasks from my note")>=0);
 pikaRememberAskTurn("перенеси на завтра","На какую дату?");assert(files.count("/notes/.ask-context.json"));assert(replies.empty());

 // All-day midnight serialization is equivalent to a date-only value.
 route("create","create","events","Create tomorrow park walk");auto allDayPlan=plan();auto allDayOp=operation(allDayPlan,"events","create","","Park walk","2026-10-21T00:00","");allDayOp["allDay"]=true;respond(allDayPlan);const auto beforeAllDay=managerRevision();assert(applyAskAssistant("Create tomorrow park walk",answer,sources));assert(managerRevision()>beforeAllDay);
 // Whole-day mixed batches are selected locally, including more than eight records.
 for(int i=0;i<12;++i){JsonDocument m;m["kind"]=i%2?"tasks":"events";m["record"]["text"]="Day batch "+String(i);m["record"]["date"]="2026-10-21T12:00";m["record"]["end"]="2026-10-21T13:00";assert(managerMutation(m.as<JsonVariantConst>(),id,error));}
 JsonDocument bulk;bulk["intent"]="change";bulk["operation"]="archive";bulk["kind"]="auto";bulk["batchAuthorized"]=true;bulk["wholeDay"]="2026-10-21";bulk["dayKinds"].add("tasks");bulk["dayKinds"].add("events");bulk["commandQuote"]="Delete all tasks and events tomorrow";bulk["confidence"]=1;String bulkReply;serializeJson(bulk,bulkReply);replies.push_back(bulkReply);
 const auto beforeBatch=managerRevision();assert(applyAskAssistant("Delete all tasks and events tomorrow",answer,sources));assert(managerRevision()>=beforeBatch+12);for(const auto& t:loadBrainTasks())if(t.text.startsWith("Day batch "))assert(t.done||t.archived);assert(!managerArchived(1));assert(replies.empty());

 // Unified tool routing skips the second intent call; "plan" is normalized for direct creation.
 JsonDocument fast;fast["intent"]="plan";fast["operation"]="create";fast["kind"]="tasks";fast["commandQuote"]="Create fast task";fast["confidence"]=1;
 serializeJson(fast,askResolvedRoute);auto fastPlan=plan();operation(fastPlan,"tasks","create","","Fast task");respond(fastPlan);const auto callsBefore=inputs.size();const auto fastRevision=managerRevision();assert(applyAskAssistant("Create fast task",answer,sources));assert(inputs.size()==callsBefore+1&&managerRevision()>fastRevision);askResolvedRoute="";
 fast["commandQuote"]="Different utterance";serializeJson(fast,askResolvedRoute);const auto deniedRevision=managerRevision();assert(applyAskAssistant("Create fast task",answer,sources));assert(managerRevision()==deniedRevision&&replies.empty());askResolvedRoute="";
 // A unique numeric task name maps digit/cardinal dictation locally, saves
 // actual done state, bumps revision and never consumes a second model reply.
 JsonDocument numeric;numeric["kind"]="tasks";numeric["record"]["text"]="123";assert(managerMutation(numeric.as<JsonVariantConst>(),id,error));const String numericId=id;
 undoRecoverNeedsBump=true;const auto numericBefore=managerRevision();const auto requestsBefore=inputs.size();route("change","archive","tasks","Удали задачу один два три");
 assert(applyAskAssistant("Удали задачу один два три",answer,sources));assert(managerRevision()==numericBefore+2&&inputs.size()==requestsBefore+1);
 tasks=loadBrainTasks();auto numericTask=std::find_if(tasks.begin(),tasks.end(),[&](const BrainTaskRecord& t){return t.id==numericId;});assert(numericTask!=tasks.end()&&numericTask->done);assert(replies.empty()&&askExecutionSaved&&!undoRecoverNeedsBump);
 const String importedId="a1234567-1234-1234-1234-123456789abc";assert(pikaAskImportTurn(importedId,"Phone question","Phone reply","2026-10-31T12:00:00Z"));auto feedSize=files["/notes/.ask-feed.jsonl"]->size();assert(pikaAskImportTurn(importedId,"Phone question","Phone reply","2026-10-31T12:00:00Z"));assert(files["/notes/.ask-feed.jsonl"]->size()==feedSize);
 puts("Production Ask executor: read-only query, note planning, complete/restore, invalid batch, fake IDs, archive isolation, dated event restore and persistent context: PASS");
}
'''
with tempfile.TemporaryDirectory() as directory:
 d=Path(directory)
 arduino=base['arduino'].replace(' String(int v):', ' String(uint32_t v,int){char b[16];snprintf(b,sizeof(b),"%x",v);text=b;}\n String(int v):').replace('#pragma once','#pragma once\n#define HEX 16').replace(' int indexOf(char c)const{',' int indexOf(const String& s)const{auto p=text.find(s.c_str());return p==std::string::npos?-1:(int)p;}\n int indexOf(char c)const{').replace(' friend bool operator<(const String& a',' friend bool operator>(const String& a,const String& b){return b<a;}\n friend bool operator>=(const String& a,const String& b){return !(a<b);}\n friend bool operator<(const String& a')
 (d/'Arduino.h').write_text(arduino,encoding='utf-8');(d/'WString.h').write_text('#include "Arduino.h"')
 (d/'test.cpp').write_text(prefix+base['source']+stubs+(root/'pika_note/src/app/ask_assistant.inc').read_text(encoding='utf-8')+main,encoding='utf-8')
 compiler=sys.argv[1:] or ['c++'];exe=d/'ask-test.exe'
 subprocess.run(compiler+[str(d/'test.cpp'),'-std=c++17','-I',str(d),'-I',str(root/'pika_note/src/app'),'-I',str(root/'.pio/libdeps/pika_note_2/ArduinoJson/src'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)

