from pathlib import Path
import ast,runpy,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2];base=runpy.run_path(str(root/'tools/tests/test_manager_store.py'))
tree=ast.parse((root/'tools/tests/test_ask_assistant.py').read_text(encoding='utf-8-sig'))
stubs=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='stubs' for t in n.targets))
stubs=stubs.replace('String pikaAskContextPath(){return "/notes/.ask-context.json";}void pikaAskAppendHistory(const String&,const String&){}','')
a=stubs.index('bool openAiJsonPost(');b=stubs.index('bool pikaChatAnswer(',a)
stubs=stubs[:a]+r'''
std::vector<String> requests;int conversations=0,inputFailures=0;
bool openAiJsonPost(const char* path,const String& payload,String& response,uint16_t=0,String* failure=nullptr){
 requests.push_back(payload);if(String(path)=="/v1/conversations"){response=String("{\"id\":\"conv_test")+String(++conversations)+"\"}";return true;}
 if(inputFailures){--inputFailures;if(failure)*failure="organization: HTTP 400 param=input";return false;}
 if(replies.empty())return false;String raw=replies.front();replies.pop_front();JsonDocument doc;
 if(!deserializeJson(doc,raw)&&doc["output"].is<JsonArray>()){response=raw;return true;}
 JsonDocument e;e["status"]="completed";e["result"]=raw;serializeJson(e,response);return true;
}
String responseOutputText(const String& response){JsonDocument e;deserializeJson(e,response);if(e["result"].is<const char*>())return e["result"].as<String>();String out;for(auto item:e["output"].as<JsonArray>())for(auto content:item["content"].as<JsonArray>())if(content["text"].is<const char*>())out+=content["text"].as<String>();return out;}
'''+stubs[b:]
stubs+=r'''
#define FILE_APPEND 2
void loadIndex(){}
bool pikaGoogleHasPendingChanges(){return false;}
template<class T> T constrain(T n,T lo,T hi){return std::max(lo,std::min(n,hi));}
String htmlEscape(const String& text){std::string out;for(size_t i=0;i<text.length();i++){char c=text[i];out+=c=='<'?"&lt;":c=='>'?"&gt;":c=='&'?"&amp;":std::string(1,c);}return out;}
bool pikaAskRenew();String pikaAskContextPath();String pikaAskHistoryPath();void pikaAskAppendHistory(const String&,const String&);
void finalReply(const char* text){JsonDocument r;r["status"]="completed";auto msg=r["output"].add<JsonObject>();msg["type"]="message";msg["content"].add<JsonObject>()["text"]=text;String raw;serializeJson(r,raw);replies.push_back(raw);}
void toolReply(const char* name,const char* arguments,const char* call="call_1"){JsonDocument r;r["status"]="completed";auto tool=r["output"].add<JsonObject>();tool["type"]="function_call";tool["name"]=name;tool["call_id"]=call;tool["arguments"]=arguments;String raw;serializeJson(r,raw);replies.push_back(raw);}
'''
main=r'''
int main(){
 String id,error,answer;std::vector<int> sources;JsonDocument memory;memory["kind"]="memories";memory["record"]["text"]="Repair roof. Buy paint.";memory["record"]["date"]="2026-10-20T10:00";assert(managerMutation(memory.as<JsonVariantConst>(),id,error));
 assert(pikaAskRenew());const String context=pikaAskContextPath(),history=pikaAskHistoryPath();
 finalReply("Hello");assert(pikaUnifiedAsk("Hello",answer,sources,true)&&answer=="Hello");pikaRememberAskTurn("Hello",answer);assert(SD_MMC.exists(history));assert(conversations==1);
 // Fresh personal lookup uses real records in the same cloud conversation.
 toolReply("lookup_records",R"({"kind":"memories","query":"roof","id":"","include_archive":false,"offset":0,"limit":4})");finalReply("Your roof note");assert(pikaUnifiedAsk("Find my roof note",answer,sources,true));assert(sources.size()==1&&sources[0]==1&&conversations==1);
 JsonDocument sent;deserializeJson(sent,requests.back());JsonDocument found;deserializeJson(found,sent["input"][0]["output"].as<String>());assert(found["records"].size()==1&&String(found["records"][0]["text"]|"")=="Repair roof. Buy paint.");pikaRememberAskTurn("Find my roof note",answer);
 // Repeated action-tool calls in one turn cannot create duplicates.
 toolReply("manage_records",R"({"context":"roof note m1"})");route("plan","create","tasks","Make a task from it");auto p=plan();auto op=operation(p,"tasks","create","","Repair roof");op["sourceId"]="m1";op["evidence"]="Repair roof";respond(p);
 toolReply("manage_records",R"({"context":"repeat"})","call_2");finalReply("Created roof task");assert(pikaUnifiedAsk("Make a task from it",answer,sources,true));assert(loadBrainTasks().size()==1);pikaRememberAskTurn("Make a task from it",answer);
 // A model-written success cannot replace a failed/clarifying executor result.
 const auto refusedRevision=managerRevision();toolReply("manage_records",R"({"context":""})");route("change","archive","tasks","Delete the missing task");p=plan();p["status"]="not_found";p["answer"]="Task was not found.";respond(p);finalReply("Successfully deleted, maybe a sync error.");
 assert(pikaUnifiedAsk("Delete the missing task",answer,sources,true));assert(managerRevision()==refusedRevision&&answer=="Task was not found. Nothing was changed.");
 // Archived data cannot be leaked by an unsolicited tool argument.
 toolReply("lookup_records",R"({"kind":"memories","query":"","id":"","include_archive":true,"offset":0,"limit":4})");finalReply("Only active records");assert(pikaUnifiedAsk("List my current notes",answer,sources,true));deserializeJson(sent,requests.back());deserializeJson(found,sent["input"][0]["output"].as<String>());assert(found["ok"]==false);
 // Read-only diagnostic never executes even an explicit create tool call.
 JsonDocument beforeDiagnostic;{File file=SD_MMC.open("/notes/.ask-session.json");assert(!deserializeJson(beforeDiagnostic,file));}const auto beforeConversations=conversations;
 const auto revision=managerRevision();toolReply("manage_records",R"({"context":"create"})");finalReply("Diagnostic only");assert(pikaUnifiedAsk("Create a task",answer,sources,false)&&managerRevision()==revision);
 JsonDocument afterDiagnostic;{File file=SD_MMC.open("/notes/.ask-session.json");assert(!deserializeJson(afterDiagnostic,file));}
 assert(conversations==beforeConversations&&String(afterDiagnostic["conversation"]|"")==String(beforeDiagnostic["conversation"]|""));deserializeJson(sent,requests.back());assert(!sent.containsKey("conversation")&&sent["store"]==false&&sent["input"].size()==3);
 // Post-save network failure returns the factual result, never raw JSON/no-change.
 toolReply("manage_records",R"({"context":""})");route("create","create","tasks","Create another task");p=plan();operation(p,"tasks","create","","Other task");respond(p);assert(pikaUnifiedAsk("Create another task",answer,sources,true));assert(loadBrainTasks().size()==2&&answer.startsWith("Created:"));pikaRememberAskTurn("Create another task",answer);
 const auto keptRevision=managerRevision();assert(pikaAskRenew());assert(pikaAskContextPath()!=context&&pikaAskHistoryPath()!=history);assert(!SD_MMC.exists(history)&&!SD_MMC.exists(context));assert(loadBrainTasks().size()==2&&managerRevision()==keptRevision&&managerReadText(1)=="Repair roof. Buy paint.");
 JsonDocument state;{File f=SD_MMC.open("/notes/.ask-session.json");assert(!deserializeJson(state,f));}JsonDocument snapshot;assert(!deserializeJson(snapshot,state["snapshot"].as<String>()));assert(snapshot["activeTasks"]==2&&snapshot["activeMemories"]==1);assert(String(state["conversation"]|"")=="");
 finalReply("Fresh");assert(pikaUnifiedAsk("Hello again",answer,sources,true)&&conversations==2);assert(replies.empty());
 finalReply("No mutation in stub");assert(pikaUnifiedAsk("Delete task 123",answer,sources,true));deserializeJson(sent,requests.back());assert(sent["tool_choice"]["name"]=="manage_records");
 finalReply("No mutation");assert(pikaUnifiedAsk("Do not delete task 123",answer,sources,true));deserializeJson(sent,requests.back());assert(sent["tool_choice"]=="auto");
 // Interrupted tool-call context is replaced once BEFORE any mutation.
 const auto sessionsBeforeRecovery=conversations;const auto recoveryRevision=managerRevision();inputFailures=1;finalReply("Recovered dialogue");
 assert(pikaUnifiedAsk("Hello after reset",answer,sources,true)&&answer=="Recovered dialogue"&&conversations==sessionsBeforeRecovery+1&&managerRevision()==recoveryRevision);assert(pikaAskHistoryPath()!=String()&&replies.empty());
 puts("Production unified Ask: shared session, live data tools, guarded once-only actions, archive scope, post-save fallback, read-only test mode and Renew preserving records with fresh snapshot: PASS");
}
'''
with tempfile.TemporaryDirectory() as directory:
 d=Path(directory);arduino=base['arduino'].replace(' int indexOf(char c)const{',' int indexOf(const String& s)const{auto p=text.find(s.c_str());return p==std::string::npos?-1:(int)p;}\n int indexOf(char c)const{').replace(' friend bool operator<(const String& a',' friend bool operator>(const String& a,const String& b){return b<a;}\n friend bool operator>=(const String& a,const String& b){return !(a<b);}\n friend bool operator<(const String& a')
 (d/'Arduino.h').write_text(arduino);(d/'WString.h').write_text('#include "Arduino.h"')
 prefix=base['prefix'].replace('if(mode)files[key]=std::make_shared<std::string>();','if(mode==1||(mode==2&&!files.count(key)))files[key]=std::make_shared<std::string>();')
 source=prefix+base['source']+stubs+(root/'pika_note/src/app/ask_assistant.inc').read_text(encoding='utf-8')+(root/'pika_note/src/app/ask_dialogue.inc').read_text(encoding='utf-8')+main
 (d/'test.cpp').write_text(source,encoding='utf-8');exe=d/'unified.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(d/'test.cpp'),'-std=c++17','-I',str(d),'-I',str(root/'pika_note/src/app'),'-I',str(root/'.pio/libdeps/pika_note_2/ArduinoJson/src'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
