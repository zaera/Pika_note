from pathlib import Path
import runpy,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2];base=runpy.run_path(str(root/'tools/tests/test_manager_store.py'))
s=(root/'pika_note/src/app/tasks.cpp').read_text(encoding='utf-8');a=s.index('namespace {bool taskReadCacheReady');b=s.index('\n}',s.index('std::vector<BrainTaskRecord> loadBrainTasks()',a))+2;production=s[a:b]
stubs=r'''#include "Arduino.h"
#include "brain.h"
#include "tasks.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
struct NoteEntry{int num;};std::vector<NoteEntry> noteIndex={{1}};
struct TaskState{String id;bool done;String updatedUtc;String normalizedText;};
std::vector<TaskState> states;BrainMemory memory;std::vector<BrainTaskRecord> manual;
bool statesReadComplete=true;int stateReads=0,memoryReads=0,managerReads=0;bool managerFails=false;
std::vector<TaskState> loadStates(){++stateReads;return states;}
const TaskState* findState(const std::vector<TaskState>& rows,const String& id){for(const auto& row:rows)if(row.id==id)return &row;return nullptr;}
BrainMemory loadBrainMemory(int){++memoryReads;return memory;}
String stableTaskId(int,const String&){return "fallback";}String noteCreatedUtc(int){return "2026-10-28T00:00:00Z";}
bool managerMergeTasks(std::vector<BrainTaskRecord>& rows){++managerReads;if(managerFails)return false;for(const auto& row:manual)rows.push_back(row);return true;}
'''
main=r'''int main(){memory.tasks={"Original"};memory.taskIds={"first"};managerFails=true;assert(loadBrainTasks().empty()&&!brainTaskReadComplete());managerFails=false;stateReads=memoryReads=managerReads=0;auto first=loadBrainTasks();assert(first.size()==1&&first[0].text=="Original");
for(int i=0;i<30;i++){auto rows=loadBrainTasks();assert(rows.size()==1);rows[0].text="local copy";}assert(stateReads==1&&memoryReads==1&&managerReads==1);
memory.tasks[0]="Edited";states.push_back({"first",true,"now",""});BrainTaskRecord added;added.id="manual";added.text="New";manual.push_back(added);invalidateBrainTaskCache();auto fresh=loadBrainTasks();assert(fresh.size()==2&&fresh[0].text=="Edited"&&fresh[0].done&&fresh[1].id=="manual");assert(stateReads==2&&memoryReads==2&&managerReads==2);
managerFails=true;invalidateBrainTaskCache();auto safe=loadBrainTasks();assert(!brainTaskReadComplete()&&safe.size()==2&&safe[0].done&&safe[1].id=="manual");memory.tasks[0]="Changed while failed";auto again=loadBrainTasks();assert(!brainTaskReadComplete()&&again[0].text=="Edited");managerFails=false;auto recovered=loadBrainTasks();assert(brainTaskReadComplete()&&recovered[0].text=="Changed while failed"&&recovered[0].done&&recovered[1].id=="manual");
auto owned=loadBrainTasks();auto* address=owned.data();brainPublishTaskCache(std::move(owned));assert(brainTaskSnapshot().data()==address);
puts("Production task cache: 30 repeated reads avoid SD, explicit invalidation refreshes text/status/manual rows, copies independent; failed overlay never publishes base-only tasks and retries recover: PASS");}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'Arduino.h').write_text(base['arduino']);(p/'test.cpp').write_text(stubs+production+main);exe=p/'tasks.exe'
 subprocess.run((sys.argv[1:] or ['c++'])+[str(p/'test.cpp'),'-std=c++17','-I',str(p),'-I',str(root/'pika_note/src/app'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
assert 'invalidateBrainTaskCache();' in (root/'pika_note/src/app/manager_store.cpp').read_text(encoding='utf-8')
assert 'invalidateBrainTaskCache();' in (root/'pika_note/src/app/brain.cpp').read_text(encoding='utf-8')
assert 'invalidateBrainTaskCache();' in s[s.index('bool setBrainTaskDone') : s.index('int openBrainTaskCount()')]
