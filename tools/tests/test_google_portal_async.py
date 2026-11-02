from pathlib import Path
import runpy,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2];base=runpy.run_path(str(root/'tools/tests/test_manager_store.py'))
s=(root/'pika_note/src/app/google_calendar.cpp').read_text(encoding='utf-8')
a=s.index('int request(');b=s.index('  if(pikaBackgroundNetworkMode()',a)
request=s[a:b]+'  return -1;\n}\n'
a=s.index('void pikaGoogleSyncTick(bool');b=s.index('void pikaGoogleSyncBatch(',a);tick=s[a:b]
a=s.index('void pikaGooglePortalTick()');portal=s[a:]
stubs=r"""
#include "http_replay.h"
#include "deferred_http.h"
struct PikaBusyLight{};
constexpr int WL_CONNECTED=3;struct{int status(){return WL_CONNECTED;}}WiFi;
bool portalAsync=false,portalWaiting=false,portalDeferred=false;String portalKey;PikaHttpReplay portalReplies;
constexpr int kTransportDeferred=-100;uint32_t nextAttempt=0;bool verified=false,verifyRequested=false,lastTickProgress=false,importActive=false;int preferredBatchNote=-1;
String accessToken="auth",status;time_t tokenExpiry=time(nullptr)+3600;
struct Job{String url,method,body;};std::vector<Job> sent;bool queued=false,ready=false;PikaDeferredResponse response;
bool pikaDeferredHttpStart(int,const String& url,const String& method,const String& body,const String&,bool){if(queued)return false;sent.push_back({url,method,body});queued=true;ready=false;return true;}
bool pikaDeferredHttpPoll(int,PikaDeferredResponse& out){if(!queued||!ready)return false;out=response;queued=ready=false;return true;}
void pikaDeferredHttpCancel(int){queued=ready=false;}
void complete(int code){response.code=code;response.body="{}";ready=true;}
String digest(const String& s){return s;}String urlEncode(const String& s){return s;}
String pikaGoogleServiceJson(){return "service";}String pikaGoogleCalendarId(){return "calendar";}bool pikaGoogleSyncEnabled(){return true;}bool pikaGoogleHasPendingChanges(){return true;}
bool pikaBackgroundNetworkMode(){return false;}bool pikaHomeButtonsBusy(){return false;}
bool authorize(){return false;}void importTick(){}String importedPrefix(){return "g-";}
String eventId(const String& id){return id;}String eventPayload(const BrainTaskRecord& t){return t.text;}
JsonDocument manifest;
bool readManifest(JsonDocument& out){out.set(manifest);return true;}bool writeManifest(JsonDocument& in){manifest.set(in);return true;}
"""
main=r"""
int main(){
 BrainTaskRecord event;event.id="one";event.type="event";event.text="Original";event.dueUtc="2026-10-25T12:00:00Z";base.push_back(event);manifest["calendar"]="calendar";manifest["items"]["one"]="previous";
 pikaGooglePortalTick();assert(queued&&sent.back().method=="PATCH"&&String(manifest["items"]["one"]|"")=="previous");
 complete(404);pikaGooglePortalTick();assert(queued&&sent.back().method=="POST"&&String(manifest["items"]["one"]|"")=="previous");
 complete(201);pikaGooglePortalTick();assert(!queued&&String(manifest["items"]["one"]|"")=="Original");
 base[0].text="Edited";nextAttempt=0;pikaGooglePortalTick();assert(queued&&sent.back().body=="Edited");
 base[0].text="Newer edit";complete(200);pikaGooglePortalTick();assert(queued&&sent.back().body=="Newer edit"&&String(manifest["items"]["one"]|"")=="Original");
 complete(200);pikaGooglePortalTick();assert(!queued&&String(manifest["items"]["one"]|"")=="Newer edit");
 verifyRequested=true;nextAttempt=0;pikaGooglePortalTick();assert(queued&&verifyRequested);complete(200);pikaGooglePortalTick();assert(!queued&&!verifyRequested&&verified);
 base[0].text="Cancel";nextAttempt=0;pikaGooglePortalTick();assert(queued);pikaGooglePortalPause();assert(!queued&&!portalWaiting&&portalReplies.empty());assert(String(manifest["items"]["one"]|"")=="Newer edit");
 base[0].text="Newer edit";BrainTaskRecord second=base[0];second.id="two";second.text="New event";base.push_back(second);nextAttempt=0;pikaGooglePortalTick();assert(queued&&sent.back().method=="POST");complete(201);pikaGooglePortalTick();assert(!queued&&String(manifest["items"]["two"]|"")=="New event");
 puts("Google portal state machine: deferred PATCH/404/POST, journal only after success, edits during flight, verify continuation and cancellation: PASS");
}
"""
with tempfile.TemporaryDirectory() as directory:
 d=Path(directory);arduino=base['arduino'].replace(' bool startsWith(const char* s)const{',' bool startsWith(const String& s)const{return startsWith(s.c_str());}\n bool startsWith(const char* s)const{');(d/'Arduino.h').write_text(arduino);(d/'WString.h').write_text('#include "Arduino.h"')
 (d/'test.cpp').write_text(base['prefix']+base['source']+stubs+request+tick+portal+main,encoding='utf-8');exe=d/'google.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(d/'test.cpp'),'-std=c++17','-I',str(d),'-I',str(root/'pika_note/src/app'),'-I',str(root/'.pio/libdeps/pika_note_2/ArduinoJson/src'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
